#include "MainWindow.h"
#include "VideoWidget.h"
#include "NdiReceiver.h"
#include "WebcamCapture.h"
#include "DeckLinkCapture.h"
#include "PsnSender.h"
#include "PsnReceiver.h"
#include "SessionManager.h"
#include "ui/SidebarWidget.h"
#include "ui/CollapsibleSection.h"
#include "ui/TrackersPanel.h"
#include "ui/TrackerBar.h"
#include "ui/StreamSourcePanel.h"
#include "ui/NetworkSettingsPanel.h"
#include "ui/StatsPanel.h"
#include "ui/CalibrationPanel.h"
#include "ui/SessionPanel.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QWidget>
#include <QCloseEvent>
#include <QFileDialog>
#include <QSettings>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <oclero/qlementine/widgets/AboutDialog.hpp>
#include <QDir>
#include <QFileInfo>
#include <QAction>
#include <QEvent>
#include <QSet>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace {
static constexpr bool kEnableIncomingPsn = true;
}

MainWindow::MainWindow(NdiReceiver* ndi, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("mouse-posi");
    resize(1440, 810);

    // ── Central layout: tracker bar + (video + sidebar) ──────────────────
    video_ = new VideoWidget;
    video_->setCalibration(&calibration_);

    sidebar_ = new SidebarWidget;
    trackerBar_ = new TrackerBar;

    auto* leftPane = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(trackerBar_);
    leftLayout->addWidget(video_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(leftPane);
    splitter->addWidget(sidebar_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1160, 280});

    setCentralWidget(splitter);

    // ── Sidebar panels ────────────────────────────────────────────────────
    ndi_         = ndi;
    ndi_->setParent(this);
    webcam_      = new WebcamCapture(this);
    decklink_    = new DeckLinkCapture(this);
    psnSender_   = new PsnSender(this);
    psnReceiver_ = new PsnReceiver(this);
    sessionMgr_  = new SessionManager(this);

    sessionPanel_    = new SessionPanel(sessionMgr_);
    streamPanel_     = new StreamSourcePanel(ndi_);
    calibrationPanel_= new CalibrationPanel(video_, ndi_, this);
    trackersPanel_   = new TrackersPanel;
    networkPanel_    = new NetworkSettingsPanel;
    statsPanel_      = new StatsPanel;

    sidebar_->addPanel("Session",         sessionPanel_,     false);
    sidebar_->addPanel("Stream Source",   streamPanel_,      true);
    calibrationSection_ = sidebar_->addPanel("Calibration", calibrationPanel_, false);
    sidebar_->addPanel("Trackers",        trackersPanel_,    true);
    sidebar_->addPanel("Network",         networkPanel_,     false);
    sidebar_->addPanel("Stats",           statsPanel_,       false);

    // ── Status bar ────────────────────────────────────────────────────────
    auto makeSep = []() {
        auto* line = new QFrame;
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Plain);
        line->setFixedWidth(1);
        line->setFixedHeight(14);
        line->setStyleSheet("color: palette(mid);");
        return line;
    };

    statusTracker_ = new QLabel("No tracker — press 1–9 to select");
    statusNdi_     = new QLabel("No NDI source");
    statusPos_     = new QLabel("--");
    statusCalib_   = new QLabel;
    statusTracker_->setContentsMargins(4, 0, 8, 0);
    statusNdi_->setContentsMargins(8, 0, 8, 0);
    statusPos_->setContentsMargins(8, 0, 4, 0);
    statusCalib_->setContentsMargins(8, 0, 4, 0);
    statusBar()->addWidget(statusTracker_);
    statusBar()->addWidget(makeSep());
    statusBar()->addWidget(statusNdi_);
    statusBar()->addWidget(makeSep());
    statusBar()->addWidget(statusPos_);
    statusBar()->addWidget(makeSep());
    statusBar()->addWidget(statusCalib_);

    statusPsnOut_ = new QLabel("● PSN Out");
    statusPsnOut_->setStyleSheet("color: #cc3333; padding: 0 8px;");
    statusBar()->addPermanentWidget(statusPsnOut_);

    statusSession_ = new QLabel;
    leaveSessionBtn_ = new QPushButton("Leave Session");
    leaveSessionBtn_->setFlat(true);
    leaveSessionBtn_->setStyleSheet(
        "QPushButton { border: 1px solid palette(mid); border-radius: 3px;"
        " padding: 1px 8px; margin: 1px 2px;"
        " color: palette(windowText); background: transparent; }"
        "QPushButton:hover { background: rgba(128,128,128,40); }");
    leaveSessionBtn_->setVisible(false);
    connect(leaveSessionBtn_, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, "Leave Session",
                "Leave the current session?",
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            sessionMgr_->leaveSession();
    });
    statusBar()->addPermanentWidget(leaveSessionBtn_);
    statusBar()->addPermanentWidget(statusSession_);

    // ── Signal wiring ─────────────────────────────────────────────────────
    connect(ndi_, &NdiReceiver::frameReady, this, &MainWindow::onFrameReady);
    connect(webcam_, &WebcamCapture::frameReady, this, &MainWindow::onWebcamFrameReady);
    connect(decklink_, &DeckLinkCapture::frameReady, this, &MainWindow::onDecklinkFrameReady);

    connect(trackerBar_, &TrackerBar::trackerSelected,
            this, [this](int id) { selectTracker(id); });

    connect(trackersPanel_, &TrackersPanel::trackersChanged,
            this, [this](const QList<TrackerConfig>& t) {
        project_.trackers = t;
        trackerBar_->setTrackers(t);
        {
            QSet<int> validIds;
            for (const auto& tr : project_.trackers) validIds.insert(tr.id);
            for (auto it = trackerPositions_.begin(); it != trackerPositions_.end(); ) {
                if (!validIds.contains(it.key())) it = trackerPositions_.erase(it);
                else ++it;
            }
        }
        if (sessionMgr_->state() == SessionManager::State::Hosting ||
            (sessionMgr_->state() == SessionManager::State::Joined &&
             sessionMgr_->localRole() == SessionRole::Admin))
            sessionMgr_->broadcastProjectState(project_);
    });

    connect(trackersPanel_, &TrackersPanel::trackerAccessChanged,
            this, [this](const QString& peerName, const QList<int>& ids) {
        sessionMgr_->setTrackerAccess(peerName, ids);
    });

    connect(streamPanel_, &StreamSourcePanel::ndiSourceSelected, this, &MainWindow::setNdiSource);
    connect(streamPanel_, &StreamSourcePanel::webcamSourceSelected, this, &MainWindow::setWebcamSource);
    connect(streamPanel_, &StreamSourcePanel::decklinkSourceSelected, this, &MainWindow::setDecklinkSource);

    connect(networkPanel_, &NetworkSettingsPanel::configChanged,
            this, [this](const NetworkConfig& cfg) {
        project_.network = cfg;
        psnSender_->configure(cfg);
        if (kEnableIncomingPsn) {
            psnReceiver_->stop();
            psnReceiver_->wait();
            psnReceiver_->startListening(cfg.multicastIp, cfg.port,
                                         cfg.psnMode == PsnMode::Multicast);
        } else {
            psnReceiver_->stop();
            psnReceiver_->wait();
        }
    });

    connect(calibrationPanel_, &CalibrationPanel::calibrationChanged,
            this, [this](const CalibrationData& cal) {
        project_.calibration = cal;
        if (cal.isValid()) calibration_.fromList(cal.homography);
        if (calibrationSection_ && cal.isValid())
            calibrationSection_->setExpanded(false);
        updateCalibStatus();
        if (sessionMgr_->state() == SessionManager::State::Hosting ||
            (sessionMgr_->state() == SessionManager::State::Joined &&
             sessionMgr_->localRole() == SessionRole::Admin))
            sessionMgr_->broadcastProjectState(project_);
    });

    connect(video_, &VideoWidget::mousePosInFrame, this, [this](QPointF framePt) {
        if (video_->mouseHeld() && calibration_.isValid()) {
            int id = trackersPanel_->activeTrackerId();
            if (id >= 0) {
                QPointF s = calibration_.pixelToStage(framePt);
                trackerPositions_[id] = {float(s.x()), float(s.y())};
                statusPos_->setText(QString("X: %1m  Z: %2m")
                    .arg(s.x(), 0, 'f', 2).arg(s.y(), 0, 'f', 2));
                log(QString("DRAG  tracker=%1  frame=(%2,%3)  stage=(%4,%5)")
                    .arg(id)
                    .arg(framePt.x(), 0, 'f', 1).arg(framePt.y(), 0, 'f', 1)
                    .arg(s.x(), 0, 'f', 4).arg(s.y(), 0, 'f', 4));
            }
        }
    });
    connect(video_, &VideoWidget::mouseLeftPressed, this, [this](QPointF framePt) {
        if (!calibration_.isValid()) return;
        int id = trackersPanel_->activeTrackerId();
        if (id < 0) return;
        QPointF s = calibration_.pixelToStage(framePt);
        trackerPositions_[id] = {float(s.x()), float(s.y())};
        statusPos_->setText(QString("X: %1m  Z: %2m")
            .arg(s.x(), 0, 'f', 2).arg(s.y(), 0, 'f', 2));
        log(QString("CLICK tracker=%1  frame=(%2,%3)  stage=(%4,%5)")
            .arg(id)
            .arg(framePt.x(), 0, 'f', 1).arg(framePt.y(), 0, 'f', 1)
            .arg(s.x(), 0, 'f', 4).arg(s.y(), 0, 'f', 4));
    });

    auto toggleFullscreen = [this]() {
        if (isFullScreen()) showNormal(); else showFullScreen();
    };
    connect(video_, &VideoWidget::fullscreenRequested, this, toggleFullscreen);
    connect(trackerBar_, &TrackerBar::fullscreenClicked, this, toggleFullscreen);

    // ── Session system wiring ─────────────────────────────────────────────
    connect(sessionMgr_, &SessionManager::stateChanged, this,
            [this](SessionManager::State state) {
        bool isUserStation = (state == SessionManager::State::Joined &&
                              sessionMgr_->localRole() == SessionRole::User);
        sidebar_->setVisible(!isUserStation);
        menuBar()->setVisible(true);
        if (!isUserStation && state == SessionManager::State::Idle)
            video_->setAssignedTrackers({}, 255);  // restore full opacity
        if (state == SessionManager::State::Hosting)
            sessionMgr_->broadcastProjectState(project_);  // seed sharedProject_ before any peer joins
        updateSessionStatus();
        updateTrackerBarRestriction();
        updateTrackersPanelPeers();
    });
    connect(sessionMgr_, &SessionManager::peerJoined, this, [this](SessionPeer) {
        updateTrackersPanelPeers();
    });
    connect(sessionMgr_, &SessionManager::peerLeft, this, [this](QString) {
        updateTrackersPanelPeers();
    });
    connect(sessionMgr_, &SessionManager::peerRoleChanged, this, [this](SessionPeer) {
        updateTrackersPanelPeers();
    });
    connect(sessionMgr_, &SessionManager::projectStateReceived,
            this, [this](Project p, int unassignedAlpha) {
        log(QString("SESSION_PROJECT_UPDATE  trackers=%1  calibValid=%2  unassignedAlpha=%3")
            .arg(p.trackers.size())
            .arg(p.calibration.isValid() ? "yes" : "no")
            .arg(unassignedAlpha));
        loadProject(p);
        video_->setAssignedTrackers(sessionMgr_->localAssignedTrackers(), unassignedAlpha);
    });
    connect(sessionMgr_, &SessionManager::trackerAccessReceived,
            this, [this](QList<int> ids) {
        video_->setAssignedTrackers(ids, sessionMgr_->unassignedAlpha());
        updateTrackerBarRestriction();
    });
    connect(sessionMgr_, &SessionManager::unassignedAlphaChanged,
            this, [this](int alpha) {
        video_->setAssignedTrackers(sessionMgr_->localAssignedTrackers(), alpha);
    });
    connect(sessionMgr_, &SessionManager::errorOccurred,
            this, [this](const QString& msg) {
        statusBar()->showMessage("Session: " + msg, 5000);
    });

    // Debug log file — ~/mouse-posi-debug.log
    logFile_.setFileName(QDir::homePath() + "/mouse-posi-debug.log");
    if (logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logStream_ = new QTextStream(&logFile_);
        log("=== session started ===");
    }

    // 60 Hz position stream timer
    connect(&timer_, &QTimer::timeout, this, &MainWindow::onTimer);
    timer_.start(16);

    // 1 Hz stats update timer
    statsElapsed_.start();
    connect(&statsTimer_, &QTimer::timeout, this, &MainWindow::updateStatsTimer);
    statsTimer_.start(1000);

    // ── Keyboard shortcuts ────────────────────────────────────────────────
    for (int i = 1; i <= 9; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_0 + i), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, i]() { selectTracker(i); });
    }
    {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this]() { selectTracker(-1); });
    }

    // ── Menus ────────────────────────────────────────────────────────────
    auto* fileMenu  = menuBar()->addMenu("&File");
    auto* actNew    = fileMenu->addAction("&New Project");
    auto* actOpen   = fileMenu->addAction("&Open Project...");
    auto* actSave   = fileMenu->addAction("&Save Project");
    auto* actSaveAs = fileMenu->addAction("Save Project &As...");
    actNew->setShortcut(QKeySequence::New);
    actOpen->setShortcut(QKeySequence::Open);
    actSave->setShortcut(QKeySequence::Save);
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(actNew,    &QAction::triggered, this, &MainWindow::onNewProject);
    connect(actOpen,   &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(actSave,   &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(actSaveAs, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    // Ensure shortcuts work even if the menu bar is hidden by the OS/window mode.
    addAction(actNew);
    addAction(actOpen);
    addAction(actSave);
    addAction(actSaveAs);

    auto* recentMenu = fileMenu->addMenu("Recent Projects");
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        recentMenu->clear();
        for (const auto& path : recentProjects()) {
            auto* a = recentMenu->addAction(path);
            connect(a, &QAction::triggered, this, [this, path]() {
                projectPath_ = path;
                loadProject(Project::load(path));
                saveRecent(path);
            });
        }
    });
    fileMenu->addSeparator();
    auto* actExit = fileMenu->addAction("E&xit");
    connect(actExit, &QAction::triggered, qApp, &QApplication::quit);

    auto* viewMenu  = menuBar()->addMenu("&View");
    auto* actToggleSidebar = viewMenu->addAction("Toggle Sidebar");
    actToggleSidebar->setShortcut(QKeySequence("Ctrl+\\"));
    connect(actToggleSidebar, &QAction::triggered, this, [this]() {
        sidebar_->setVisible(!sidebar_->isVisible());
    });

    auto* helpMenu  = menuBar()->addMenu("&Help");
    auto* actAbout  = helpMenu->addAction("About mouse-posi...");
    actAbout->setMenuRole(QAction::AboutRole);
    connect(actAbout, &QAction::triggered, this, [this]() {
        oclero::qlementine::AboutDialog dlg(this);
        dlg.setApplicationName("mouse-posi");
        dlg.setApplicationVersion("1.0");
        dlg.setDescription("Camera-based mouse follow spot position tracker and PSN sender.");
        dlg.setCopyright("© 2026 Hannes Rüger");
        dlg.exec();
    });

    loadProject(Project::defaultProject());
    updateSessionStatus();
}

MainWindow::~MainWindow() {
    timer_.stop();
    statsTimer_.stop();
    psnReceiver_->stop();
    ndi_->stop();
    psnReceiver_->wait();
    ndi_->wait();
    log("=== session ended ===");
    delete logStream_;
}

void MainWindow::log(const QString& msg) {
    if (!logStream_) return;
    *logStream_ << QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ") << msg << "\n";
    logStream_->flush();
}

void MainWindow::selectTracker(int id) {
    if (id >= 0) {
        bool exists = false;
        for (const auto& t : project_.trackers) {
            if (t.id == id) { exists = true; break; }
        }
        if (!exists) {
            statusBar()->showMessage(
                QString("Tracker %1 is not configured (add it in Trackers).").arg(id),
                4000);
            id = -1;
        }
    }
    if (id >= 0 && !isTrackerAllowed(id)) {
        statusBar()->showMessage(
            QString("Tracker %1 is not assigned to you in this session.").arg(id), 3000);
        return;
    }
    trackersPanel_->setActiveTrackerId(id);
    trackerBar_->setActiveTrackerId(id);
    QColor color = id >= 0 ? trackersPanel_->activeColor() : Qt::white;
    video_->setActiveTracker(id, color);
    if (id >= 0)
        statusTracker_->setText(QString("Tracker %1 — hold left mouse to send").arg(id));
    else
        statusTracker_->setText("No tracker — press 1–9 to select");
}

bool MainWindow::isTrackerAllowed(int id) const {
    if (id < 0) return true;
    auto state = sessionMgr_->state();
    if (state != SessionManager::State::Joined) return true;
    if (sessionMgr_->localRole() == SessionRole::Admin) return true;
    return sessionMgr_->localAssignedTrackers().contains(id);
}

void MainWindow::updateTrackerBarRestriction() {
    bool isUser = (sessionMgr_->state() == SessionManager::State::Joined &&
                   sessionMgr_->localRole() == SessionRole::User);
    if (isUser) {
        trackerBar_->setAllowedTrackers(sessionMgr_->localAssignedTrackers());
    } else {
        trackerBar_->clearRestriction();
    }
}

void MainWindow::updateTrackersPanelPeers() {
    auto state = sessionMgr_->state();
    bool isAdmin = (state == SessionManager::State::Hosting ||
                    (state == SessionManager::State::Joined &&
                     sessionMgr_->localRole() == SessionRole::Admin));

    QStringList peerNames;
    QMap<QString, QList<int>> assignments;
    for (const auto& p : sessionMgr_->peers()) {
        peerNames.append(p.displayName);
        assignments[p.displayName] = p.assignedTrackerIds;
    }
    trackersPanel_->setSessionContext(isAdmin, peerNames, assignments);
}

void MainWindow::loadProject(const Project& p) {
    project_ = p;
    applyProject();
}

void MainWindow::setNdiSource(const QString& source) {
    project_.ndiSource = source;

    videoSourceKind_ = VideoSourceKind::Ndi;
    videoSourceName_ = source;

    webcam_->stop();
    decklink_->stop();
    ndi_->connectToSource(source);
    statusNdi_->setText(source.isEmpty() ? "No NDI source" : "NDI: " + source + " (connecting…)");
    streamPanel_->setCurrentNdiSource(source);
    video_->setNdiSourceConfigured(!source.isEmpty());
}

void MainWindow::setWebcamSource(const QString& device) {
#if WEBCAM_AVAILABLE
    videoSourceKind_ = VideoSourceKind::Webcam;
    videoSourceName_ = device;

    // Stop NDI decoding to reduce CPU/network usage when using the webcam.
    ndi_->disconnectFromSource();
    decklink_->stop();

    webcam_->setDeviceDescription(device);
    webcam_->start();

    statusNdi_->setText(device.isEmpty() ? "No webcam" : "Webcam: " + device + " (starting…)");
    video_->setNdiSourceConfigured(!device.isEmpty());
#else
    (void)device;
    statusNdi_->setText("Webcam support unavailable (install Qt Multimedia)");
    video_->setNdiSourceConfigured(false);
#endif
}

void MainWindow::setDecklinkSource(const QString& device) {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    videoSourceKind_ = VideoSourceKind::DeckLink;
    videoSourceName_ = device;

    // Stop other sources to keep CPU/network use low.
    ndi_->disconnectFromSource();
    webcam_->stop();

    decklink_->setDeviceName(device);
    decklink_->start();

    statusNdi_->setText(device.isEmpty() ? "No DeckLink device" : "DeckLink: " + device + " (starting…)");
    video_->setNdiSourceConfigured(!device.isEmpty());
#else
    (void)device;
    statusNdi_->setText("DeckLink support unavailable");
    video_->setNdiSourceConfigured(false);
#endif
}

void MainWindow::applyProject() {
    updateWindowTitle();
    trackersPanel_->setTrackers(project_.trackers);
    trackerBar_->setTrackers(project_.trackers);
    networkPanel_->setConfig(project_.network);
    psnSender_->configure(project_.network);
    if (kEnableIncomingPsn) {
        psnReceiver_->stop();
        psnReceiver_->wait();
        psnReceiver_->startListening(project_.network.multicastIp,
                                     project_.network.port,
                                     project_.network.psnMode == PsnMode::Multicast);
    } else {
        psnReceiver_->stop();
        psnReceiver_->wait();
    }
    video_->setNdiSourceConfigured(!project_.ndiSource.isEmpty());
    if (!project_.ndiSource.isEmpty())
        setNdiSource(project_.ndiSource);
    if (project_.calibration.isValid()) {
        calibration_.fromList(project_.calibration.homography);
        const auto& h = project_.calibration.homography;
        if (h.size() == 9)
            log(QString("CALIBRATION  H=[%1 %2 %3 | %4 %5 %6 | %7 %8 %9]")
                .arg(h[0],0,'g',6).arg(h[1],0,'g',6).arg(h[2],0,'g',6)
                .arg(h[3],0,'g',6).arg(h[4],0,'g',6).arg(h[5],0,'g',6)
                .arg(h[6],0,'g',6).arg(h[7],0,'g',6).arg(h[8],0,'g',6));
        calibrationPanel_->setCalibration(project_.calibration);
        if (calibrationSection_)
            calibrationSection_->setExpanded(false);
    }
    updateCalibStatus();
}

void MainWindow::updateWindowTitle() {
    const QString base = QStringLiteral("mouse-posi");
    const QString projectName = project_.name.isEmpty() ? QStringLiteral("New Project") : project_.name;

    if (projectPath_.isEmpty()) {
        setWindowTitle(QString("%1 — %2").arg(base, projectName));
        return;
    }

    const QString fileName = QFileInfo(projectPath_).fileName();
    if (fileName.isEmpty()) {
        setWindowTitle(QString("%1 — %2").arg(base, projectName));
        return;
    }

    setWindowTitle(QString("%1 — %2 (%3)").arg(base, projectName, fileName));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!projectPath_.isEmpty())
        project_.save(projectPath_);
    e->accept();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    // Hide sidebar when entering native fullscreen
    if (e->type() == QEvent::WindowStateChange) {
        sidebar_->setFullscreenMode(isFullScreen());
    }
}

void MainWindow::onTimer() {
    if (trackerPositions_ != lastLoggedPositions_) {
        for (auto it = trackerPositions_.cbegin(); it != trackerPositions_.cend(); ++it) {
            auto prev = lastLoggedPositions_.value(it.key(), {1e9f, 1e9f});
            if (it.value() != prev)
                log(QString("POSITION  tracker=%1  stage=(%2,%3)  delta=(%4,%5)")
                    .arg(it.key())
                    .arg(it.value().first, 0, 'f', 4).arg(it.value().second, 0, 'f', 4)
                    .arg(it.value().first  - prev.first,  0, 'f', 4)
                    .arg(it.value().second - prev.second, 0, 'f', 4));
        }
        for (auto it = lastLoggedPositions_.cbegin(); it != lastLoggedPositions_.cend(); ++it) {
            if (!trackerPositions_.contains(it.key()))
                log(QString("POSITION  tracker=%1  removed").arg(it.key()));
        }
        lastLoggedPositions_ = trackerPositions_;
    }
    video_->setOwnPositions(trackerPositions_, project_.trackers);
    if (!trackerPositions_.isEmpty())
        psnSender_->sendPositions(trackerPositions_, project_.trackers);
    frameCount_++;
}

void MainWindow::updateStatsTimer() {
    double elapsed = statsElapsed_.restart() / 1000.0;
    currentFps_  = frameCount_ / elapsed;
    frameCount_  = 0;

    // PSN stats
    int txRate = 0, rxRate = 0;
    if (elapsed > 0.0) {
        quint64 txTotal = psnSender_->totalPacketsSent();
        quint64 rxTotal = psnReceiver_->totalBinaryPacketsReceived();
        txRate = std::max(0, static_cast<int>(std::llround((txTotal - lastPsnTxPackets_) / elapsed)));
        rxRate = std::max(0, static_cast<int>(std::llround((rxTotal - lastPsnRxPackets_) / elapsed)));
        lastPsnTxPackets_ = txTotal;
        lastPsnRxPackets_ = rxTotal;
    }
    statsPanel_->setPsnTxRate(txRate);
    statsPanel_->setPsnRxRate(rxRate, psnReceiver_->remotePositions().size());

    if (txRate > 0) {
        statusPsnOut_->setStyleSheet("color: #33aa44; padding: 0 4px;");
    } else {
        statusPsnOut_->setStyleSheet("color: #cc3333; padding: 0 4px;");
    }

    statsPanel_->setNdiInfo(project_.ndiSource,
                             video_->frameSize().width(),
                             video_->frameSize().height(),
                             currentFps_);
    statsPanel_->setSessionInfo(
        sessionMgr_->state() == SessionManager::State::Idle ? "Offline" :
        sessionMgr_->state() == SessionManager::State::Hosting ? "Hosting" : "Joined",
        sessionMgr_->peers().size());
}

void MainWindow::updateCalibStatus() {
    if (calibration_.isValid()) {
        statusCalib_->setText("Calibrated");
        statusCalib_->setStyleSheet("color: palette(mid);");
    } else {
        statusCalib_->setText("No calibration");
        statusCalib_->setStyleSheet("color: #cc9900;");
    }
}

void MainWindow::updateSessionStatus() {
    auto state = sessionMgr_->state();
    bool isJoinedUser = state == SessionManager::State::Joined
                        && sessionMgr_->localRole() == SessionRole::User;

    struct { const char* text; const char* color; } info;
    switch (state) {
        case SessionManager::State::Idle:
            info = {"● Offline",     "#888888"}; break;
        case SessionManager::State::Hosting:
            info = {"● Hosting",     "#33aa44"}; break;
        case SessionManager::State::Joining:
            info = {"● Connecting…", "#ccaa22"}; break;
        case SessionManager::State::Joined:
            info = sessionMgr_->localRole() == SessionRole::Admin
                   ? decltype(info){"● Joined (Admin)", "#33aa44"}
                   : decltype(info){"● Joined",         "#33aa44"};
            break;
    }
    statusSession_->setText(info.text);
    statusSession_->setStyleSheet(QString("color: %1; padding: 0 4px;").arg(info.color));
    leaveSessionBtn_->setVisible(isJoinedUser);
}

void MainWindow::onFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::Ndi)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("NDI: %1  %2×%3")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height()));
}

void MainWindow::onWebcamFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::Webcam)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("Webcam: %1  %2×%3")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height()));
}

void MainWindow::onDecklinkFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::DeckLink)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("DeckLink: %1  %2×%3")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height()));
}

void MainWindow::handleVideoFrame(const QImage& frame) {
    QSize sz(frame.width(), frame.height());
    if (sz != lastVideoFrameSize_) {
        log(QString("VIDEO_SIZE  %1x%2  (was %3x%4)")
            .arg(sz.width()).arg(sz.height())
            .arg(lastVideoFrameSize_.width()).arg(lastVideoFrameSize_.height()));
        lastVideoFrameSize_ = sz;
    }

    video_->setFrame(frame);

    auto remote = kEnableIncomingPsn ? psnReceiver_->remotePositions() : QMap<int, QVector3D>{};
    if (video_->mouseHeld()) {
        const int activeId = trackersPanel_->activeTrackerId();
        if (activeId >= 0) remote.remove(activeId);
    }
    video_->setRemotePositions(remote, project_.trackers);
}

void MainWindow::onNewProject() {
    project_     = Project::defaultProject();
    projectPath_ = QString();
    trackerPositions_.clear();
    applyProject();
}

void MainWindow::onOpenProject() {
    QFileDialog::Options opts;
#ifdef Q_OS_MAC
    opts |= QFileDialog::DontUseNativeDialog;
#endif
    QString path = QFileDialog::getOpenFileName(
        this, "Open Project", QDir::homePath(), "mouse-posi Projects (*.mposi)", nullptr, opts);
    if (path.isEmpty()) return;
    projectPath_ = path;
    loadProject(Project::load(path));
    saveRecent(path);
    updateWindowTitle();
}

void MainWindow::onSaveProject() {
    if (projectPath_.isEmpty()) { onSaveProjectAs(); return; }
    project_.save(projectPath_);
    updateWindowTitle();
}

void MainWindow::onSaveProjectAs() {
    QFileDialog::Options opts;
#ifdef Q_OS_MAC
    opts |= QFileDialog::DontUseNativeDialog;
#endif
    QString path = QFileDialog::getSaveFileName(
        this, "Save Project",
        QDir::homePath() + "/" + project_.name + ".mposi",
        "mouse-posi Projects (*.mposi)", nullptr, opts);
    if (path.isEmpty()) return;
    projectPath_ = path;
    project_.save(path);
    saveRecent(path);
    updateWindowTitle();
}

void MainWindow::saveRecent(const QString& path) {
    QSettings s("mouse-posi", "mouse-posi");
    QStringList recent = s.value("recentProjects").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    s.setValue("recentProjects", recent);
}

QStringList MainWindow::recentProjects() const {
    QSettings s("mouse-posi", "mouse-posi");
    return s.value("recentProjects").toStringList();
}
