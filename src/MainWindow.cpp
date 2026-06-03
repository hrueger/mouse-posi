#define NOMINMAX
#include "MainWindow.h"
#include "MvrImporter.h"
#include "VideoWidget.h"
#include "NdiReceiver.h"
#include "WebcamCapture.h"
#include "DeckLinkCapture.h"
#include "PsnSender.h"
#include "PsnReceiver.h"
#include "SacnReceiver.h"
#include "SessionManager.h"
#include "ui/TrackersPanel.h"
#include "ui/TrackerBar.h"
#include "ui/StreamSourcePanel.h"
#include "ui/NetworkSettingsPanel.h"
#include "ui/StatsPanel.h"
#include "ui/CalibrationPanel.h"
#include "ui/SessionPanel.h"
#include "ui/Stage3DPanel.h"
#include "ui/StageItemsPanel.h"
#include "ui/StagePropertiesPanel.h"
#include "ui/WelcomeScreen.h"
#include <oclero/qlementine/style/ThemeManager.hpp>
#include <QApplication>
#include <QPropertyAnimation>
#include <QStyleHints>
#include <QDockWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QFileDialog>
#include <QSettings>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QShortcut>
#include <oclero/qlementine/widgets/AboutDialog.hpp>
#include <QDir>
#include <QFileInfo>
#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QSet>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
static constexpr bool kEnableIncomingPsn = true;
}

MainWindow::MainWindow(NdiReceiver* ndi,
                       oclero::qlementine::ThemeManager* themeManager,
                       QWidget* parent)
    : QMainWindow(parent), themeManager_(themeManager)
{
    setWindowTitle("OnPoint");
    resize(1440, 810);

    // ── Create widgets ────────────────────────────────────────────────────
    video_ = new VideoWidget;
    video_->setCalibration(&calibration_);
    trackerBar_ = new TrackerBar;

    ndi_         = ndi;
    ndi_->setParent(this);
    webcam_      = new WebcamCapture(this);
    decklink_    = new DeckLinkCapture(this);
    psnSender_    = new PsnSender(this);
    psnReceiver_  = new PsnReceiver(this);
    sacnReceiver_ = new SacnReceiver();   // no parent — lives on sacnThread_
    sacnThread_   = new QThread(this);
    sacnReceiver_->moveToThread(sacnThread_);
    connect(sacnThread_, &QThread::finished, sacnReceiver_, &QObject::deleteLater);
    sacnThread_->start();
    sessionMgr_   = new SessionManager(this);

    sessionPanel_          = new SessionPanel(sessionMgr_);
    stage3DPanel_          = new Stage3DPanel;
    stageItemsPanel_       = new StageItemsPanel;
    stagePropertiesPanel_  = new StagePropertiesPanel;
    streamPanel_     = new StreamSourcePanel(ndi_);
    calibrationPanel_= new CalibrationPanel(video_, ndi_, this);
    trackersPanel_   = new TrackersPanel;
    networkPanel_    = new NetworkSettingsPanel;
    statsPanel_      = new StatsPanel;

    // ── Dock layout ───────────────────────────────────────────────────────
    // Central widget: contains the welcome screen; shrunk to 0x0 in workspace mode
    // so dock widgets fill all available space.
    centralContainer_ = new QWidget;
    auto* centralLayout = new QVBoxLayout(centralContainer_);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    welcomeScreen_ = new WelcomeScreen(sessionMgr_);
    centralLayout->addWidget(welcomeScreen_);
    centralContainer_->setMaximumSize(0, 0);
    setCentralWidget(centralContainer_);
    setDockNestingEnabled(true);

    // Video dock: tracker bar on top, video below
    auto* videoContainer = new QWidget;
    auto* videoLayout    = new QVBoxLayout(videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);
    videoLayout->addWidget(trackerBar_);
    videoLayout->addWidget(video_);
    videoDock_ = new QDockWidget("Video", this);
    videoDock_->setObjectName("dock_video");
    videoDock_->setWidget(videoContainer);
    videoDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, videoDock_);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    auto makeDock = [this](const QString& title, const QString& objName, QWidget* w) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(objName);
        d->setWidget(w);
        connect(d, &QDockWidget::topLevelChanged, d, [d](bool floating) {
            if (floating) {
                d->setWindowFlags(Qt::Window);
                d->show();
            }
        });
        return d;
    };

    // Same native-window behaviour for the video dock
    connect(videoDock_, &QDockWidget::topLevelChanged, videoDock_, [this](bool floating) {
        if (floating) {
            videoDock_->setWindowFlags(Qt::Window);
            videoDock_->show();
        }
    });

    sessionDock_     = makeDock("Session",       "dock_session",     sessionPanel_);
    streamDock_      = makeDock("Stream Source", "dock_stream",      streamPanel_);
    calibrationDock_ = makeDock("Calibration",   "dock_calibration", calibrationPanel_);
    trackersDock_    = makeDock("Trackers",       "dock_trackers",    trackersPanel_);
    networkDock_     = makeDock("Network",        "dock_network",     networkPanel_);
    statsDock_       = makeDock("Stats",          "dock_stats",       statsPanel_);
    stage3DDock_          = makeDock("Stage 3D",              "dock_stage3d",          stage3DPanel_);
    stageItemsDock_       = makeDock("Stage Objects",         "dock_stage_items",      stageItemsPanel_);
    stagePropertiesDock_  = makeDock("Object Properties",     "dock_stage_properties", stagePropertiesPanel_);
    panelDocks_           = {sessionDock_, streamDock_, calibrationDock_,
                             trackersDock_, networkDock_, statsDock_};

    // Default layout: all 6 panels tabified on the right
    addDockWidget(Qt::RightDockWidgetArea, sessionDock_);
    for (auto* d : panelDocks_.sliced(1))
        tabifyDockWidget(panelDocks_[0], d);
    streamDock_->raise();   // show "Stream Source" tab by default

    // Stage 3D dock tabs alongside video on the left
    addDockWidget(Qt::LeftDockWidgetArea, stage3DDock_);
    tabifyDockWidget(videoDock_, stage3DDock_);
    videoDock_->raise();  // keep video visible by default

    // Stage objects dock: tabified with right panels
    addDockWidget(Qt::RightDockWidgetArea, stageItemsDock_);
    tabifyDockWidget(panelDocks_[0], stageItemsDock_);

    // Object properties dock: tabified alongside stage objects
    addDockWidget(Qt::RightDockWidgetArea, stagePropertiesDock_);
    tabifyDockWidget(stageItemsDock_, stagePropertiesDock_);

    // Default sizing — overridden by saved state on subsequent launches
    resizeDocks({videoDock_},   {1160}, Qt::Horizontal);
    resizeDocks({sessionDock_}, {280},  Qt::Horizontal);

    // ── Stage geometry signal wiring ──────────────────────────────────────
    // 3D view: new rect drawn -> add platform
    connect(stage3DPanel_, &Stage3DPanel::rectDrawn,
            this, [this](QPointF center, float w, float d) {
        StageObject obj;
        obj.id = 1;
        for (const auto& o : project_.stageObjects) obj.id = std::max(obj.id, o.id + 1);
        obj.name = QString("Platform %1").arg(obj.id);
        obj.isRect = true; obj.center = center; obj.width = w; obj.depth = d;
        obj.rotation = 0; obj.height = 1.0f;
        const float hw = w/2, hd = d/2;
        obj.polygon << QPointF(center.x()-hw, center.y()-hd)
                    << QPointF(center.x()+hw, center.y()-hd)
                    << QPointF(center.x()+hw, center.y()+hd)
                    << QPointF(center.x()-hw, center.y()+hd);
        project_.stageObjects << obj;
        syncAllStageObjects(); markDirty();
        stage3DPanel_->setActiveTool(Stage3DTool::Select);
        stage3DPanel_->setSelectedObject(obj.id);
        stageItemsPanel_->setSelectedObject(obj.id);
        stagePropertiesPanel_->setSelectedObject(obj.id);
    });
    connect(stage3DPanel_, &Stage3DPanel::polygonDrawn,
            this, [this](QPolygonF poly) {
        StageObject obj;
        obj.id = 1;
        for (const auto& o : project_.stageObjects) obj.id = std::max(obj.id, o.id + 1);
        obj.name = QString("Platform %1").arg(obj.id);
        obj.isRect = false; obj.height = 1.0f;
        QRectF bb = poly.boundingRect();
        obj.center = bb.center(); obj.width = float(bb.width()); obj.depth = float(bb.height());
        obj.polygon = poly;
        project_.stageObjects << obj;
        syncAllStageObjects(); markDirty();
        stage3DPanel_->setActiveTool(Stage3DTool::Select);
        stage3DPanel_->setSelectedObject(obj.id);
        stageItemsPanel_->setSelectedObject(obj.id);
        stagePropertiesPanel_->setSelectedObject(obj.id);
    });

    // 3D view: object clicked
    connect(stage3DPanel_, &Stage3DPanel::objectSelected, this, [this](int id) {
        stageItemsPanel_->setSelectedObject(id);
        stagePropertiesPanel_->setSelectedObject(id);
        stage3DPanel_->setSelectedObject(id);
    });

    // Items panel: selection changed
    connect(stageItemsPanel_, &StageItemsPanel::selectionChanged, this, [this](int id) {
        stage3DPanel_->setSelectedObject(id);
        stagePropertiesPanel_->setSelectedObject(id);
    });

    // Items panel: request draw / add tools
    connect(stageItemsPanel_, &StageItemsPanel::addRectRequested, this, [this]() {
        stage3DDock_->raise(); stage3DPanel_->setActiveTool(Stage3DTool::DrawRect);
    });
    connect(stageItemsPanel_, &StageItemsPanel::addPolygonRequested, this, [this]() {
        stage3DDock_->raise(); stage3DPanel_->setActiveTool(Stage3DTool::DrawPolygon);
    });
    connect(stageItemsPanel_, &StageItemsPanel::addStageOutlineRequested, this, [this]() {
        StageObject obj;
        obj.id = 1;
        for (const auto& o : project_.stageObjects) obj.id = std::max(obj.id, o.id + 1);
        obj.name = "Stage Outline";
        obj.isRect = true;
        obj.isStageOutline = true;
        obj.center = {0.0, 0.0};
        obj.width = 10.0f; obj.depth = 6.0f; obj.rotation = 0.0f; obj.height = 0.0f;
        obj.polygon << QPointF(-5, -3) << QPointF(5, -3)
                    << QPointF(5, 3)   << QPointF(-5, 3);
        project_.stageObjects << obj;
        syncAllStageObjects(); markDirty();
        stage3DPanel_->setSelectedObject(obj.id);
        stageItemsPanel_->setSelectedObject(obj.id);
        stagePropertiesPanel_->setSelectedObject(obj.id);
    });

    // Items panel: delete
    connect(stageItemsPanel_, &StageItemsPanel::deleteRequested,
            this, [this](int id) {
        project_.stageObjects.removeIf([id](const StageObject& o){ return o.id == id; });
        syncAllStageObjects(); markDirty();
        stage3DPanel_->setSelectedObject(-1);
        stageItemsPanel_->setSelectedObject(-999);
        stagePropertiesPanel_->setSelectedObject(-999);
    });

    // Items panel: stage object renamed inline in tree
    connect(stageItemsPanel_, &StageItemsPanel::objectRenamed,
            this, [this](int id, const QString& name) {
        for (auto& o : project_.stageObjects)
            if (o.id == id) { o.name = name; break; }
        stagePropertiesPanel_->setAllObjects(systemStageItems_ + project_.stageObjects);
        markDirty();
    });

    // Items panel: duplicate
    connect(stageItemsPanel_, &StageItemsPanel::duplicateRequested,
            this, [this](int id) {
        const StageObject* src = nullptr;
        for (const auto& o : project_.stageObjects)
            if (o.id == id) { src = &o; break; }
        if (!src) return;

        StageObject copy = *src;
        copy.id = 1;
        for (const auto& o : project_.stageObjects) copy.id = std::max(copy.id, o.id + 1);
        copy.name = src->name + " Copy";
        const QPointF offset(0.5, 0.5);
        copy.center += offset;
        QPolygonF moved;
        for (const QPointF& v : copy.polygon) moved << (v + offset);
        copy.polygon = moved;

        project_.stageObjects << copy;
        syncAllStageObjects(); markDirty();
        stage3DPanel_->setSelectedObject(copy.id);
        stageItemsPanel_->setSelectedObject(copy.id);
        stagePropertiesPanel_->setSelectedObject(copy.id);
    });

    // Properties panel: object edited via properties form
    connect(stagePropertiesPanel_, &StagePropertiesPanel::objectEdited,
            this, [this](const StageObject& edited) {
        if (edited.id == -1) {
            // Camera: only the FOV is user-editable
            project_.calibrationView.cameraFovDeg = edited.fovDeg;
            syncAllStageObjects(); markDirty();
            return;
        }
        for (auto& o : project_.stageObjects) {
            if (o.id == edited.id) { o = edited; break; }
        }
        syncAllStageObjects(); markDirty();
    });

    // Stage3DPanel: MVR file imported — each import is a separate root node
    connect(stage3DPanel_, &Stage3DPanel::mvrImportChanged,
            this, [this](const MvrImport& import) {
        mvrImports_.append(import);
        stage3DPanel_->setMvrImports(mvrImports_);
        stageItemsPanel_->setMvrImports(mvrImports_);
        // Convert MvrImport to MvrImportData for storage (metadata + raw MVR bytes)
        MvrImportData data;
        data.name    = import.name;
        data.offsetX = import.offsetX;
        data.offsetY = import.offsetY;
        data.offsetZ = import.offsetZ;
        data.rotDeg  = import.rotDeg;
        data.enabled = import.enabled;
        data.mvrData = import.mvrData;
        for (const auto& layer : import.layers) {
            MvrLayerData layerData;
            layerData.name    = layer.name;
            layerData.enabled = layer.enabled;
            for (const auto& obj : layer.objects) {
                MvrObjectData objData;
                objData.name       = obj.name;
                objData.type       = MvrObjectData::Type(int(obj.type));
                objData.positionM  = obj.positionM;
                objData.gdtfSpec   = obj.gdtfSpec;
                objData.unitNumber = obj.unitNumber;
                objData.dmxAddress = obj.dmxAddress;
                objData.enabled    = obj.enabled;
                layerData.objects.append(objData);
            }
            data.layers.append(layerData);
        }
        project_.mvr.imports.append(data);
        markDirty();
    });

    // Items panel: layer/object visibility toggled
    connect(stageItemsPanel_, &StageItemsPanel::mvrVisibilityChanged,
            this, [this](int ii, int li, int oi, bool visible) {
        if (ii < 0 || ii >= mvrImports_.size()) return;
        if (li < 0) {
            // Root level: li == -1
            mvrImports_[ii].enabled = visible;
            if (ii < project_.mvr.imports.size())
                project_.mvr.imports[ii].enabled = visible;
        } else if (li >= 0 && li < mvrImports_[ii].layers.size()) {
            if (oi < 0) {
                mvrImports_[ii].layers[li].enabled = visible;
                if (ii < project_.mvr.imports.size() && li < project_.mvr.imports[ii].layers.size())
                    project_.mvr.imports[ii].layers[li].enabled = visible;
            } else if (oi < mvrImports_[ii].layers[li].objects.size()) {
                mvrImports_[ii].layers[li].objects[oi].enabled = visible;
                if (ii < project_.mvr.imports.size() && li < project_.mvr.imports[ii].layers.size()
                    && oi < project_.mvr.imports[ii].layers[li].objects.size())
                    project_.mvr.imports[ii].layers[li].objects[oi].enabled = visible;
            }
        }
        stage3DPanel_->setMvrImports(mvrImports_);
        markDirty();
    });

    // Items panel: MVR root renamed inline
    connect(stageItemsPanel_, &StageItemsPanel::mvrImportRenamed,
            this, [this](int ii, const QString& name) {
        if (ii >= 0 && ii < mvrImports_.size()) {
            mvrImports_[ii].name = name;
            if (ii < project_.mvr.imports.size())
                project_.mvr.imports[ii].name = name;
            markDirty();
        }
    });

    // Items panel: MVR import deleted
    connect(stageItemsPanel_, &StageItemsPanel::mvrImportDeleteRequested,
            this, [this](int ii) {
        if (ii >= 0 && ii < mvrImports_.size()) {
            mvrImports_.removeAt(ii);
            if (ii < project_.mvr.imports.size())
                project_.mvr.imports.removeAt(ii);
            stage3DPanel_->setMvrImports(mvrImports_);
            stageItemsPanel_->setMvrImports(mvrImports_);
            stagePropertiesPanel_->setSelectedObject(-999);
            markDirty();
        }
    });

    // Items panel: MVR root selected → show offset/rotation properties
    connect(stageItemsPanel_, &StageItemsPanel::mvrImportSelected,
            this, [this](int ii) {
        if (ii >= 0 && ii < mvrImports_.size())
            stagePropertiesPanel_->setMvrImport(ii, mvrImports_[ii]);
    });

    // Properties panel: MVR offset/rotation/name edited — sync fields that the
    // properties panel owns; leave layer/object visibility state untouched
    connect(stagePropertiesPanel_, &StagePropertiesPanel::mvrImportEdited,
            this, [this](int ii, const MvrImport& import) {
        if (ii >= 0 && ii < mvrImports_.size()) {
            mvrImports_[ii].name    = import.name;
            mvrImports_[ii].offsetX = import.offsetX;
            mvrImports_[ii].offsetY = import.offsetY;
            mvrImports_[ii].offsetZ = import.offsetZ;
            mvrImports_[ii].rotDeg  = import.rotDeg;
            if (ii < project_.mvr.imports.size()) {
                project_.mvr.imports[ii].name    = import.name;
                project_.mvr.imports[ii].offsetX = import.offsetX;
                project_.mvr.imports[ii].offsetY = import.offsetY;
                project_.mvr.imports[ii].offsetZ = import.offsetZ;
                project_.mvr.imports[ii].rotDeg  = import.rotDeg;
            }
            stage3DPanel_->setMvrImports(mvrImports_);
            stageItemsPanel_->setMvrImports(mvrImports_);
            markDirty();
        }
    });

    // Items panel: MVR layer/object selected (not the root) → clear properties panel
    connect(stageItemsPanel_, &StageItemsPanel::mvrChildItemSelected,
            this, [this]() {
        stagePropertiesPanel_->setSelectedObject(-999);
    });

    // Stage3D panel: show MVR labels setting changed
    connect(stage3DPanel_, &Stage3DPanel::showMvrLabelsChanged,
            this, [this](bool show) {
        project_.mvr.showLabels = show;
        markDirty();
    });

    // Stage3D panel: MVR render mode changed
    connect(stage3DPanel_, &Stage3DPanel::mvrRenderModeChanged,
            this, [this](MvrRenderMode mode) {
        project_.mvr.renderMode = MvrRenderModeEnum(int(mode));
        markDirty();
    });

    // Items panel: vid / 3D visibility toggled
    connect(stageItemsPanel_, &StageItemsPanel::visibilityChanged,
            this, [this](int id, bool inVideo, bool in3D) {
        if (id == -1) {
            project_.calibrationView.showCameraIn3D = in3D;
        } else if (id == -2) {
            project_.calibrationView.showCalibRectInVideo = inVideo;
            project_.calibrationView.showCalibRectIn3D    = in3D;
        } else {
            for (auto& o : project_.stageObjects) {
                if (o.id == id) { o.visibleInVideo = inVideo; o.visibleIn3D = in3D; break; }
            }
        }
        syncAllStageObjects(); markDirty();
    });

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
    statusBar()->addWidget(statusTracker_);
    statusBar()->addWidget(makeSep());
    statusBar()->addWidget(statusNdi_);
    statusBar()->addWidget(makeSep());
    statusBar()->addWidget(statusPos_);

    statusBar()->addPermanentWidget(statusCalib_);

    statusPsnOut_ = new QLabel("● PSN Out");
    statusPsnOut_->setStyleSheet("color: #cc3333; padding: 0 8px;");
    statusBar()->addPermanentWidget(statusPsnOut_);

    statusSacnIn_ = new QLabel("● sACN In");
    statusSacnIn_->setStyleSheet("color: #888888; padding: 0 8px;");
    statusSacnIn_->setVisible(false);
    statusBar()->addPermanentWidget(statusSacnIn_);

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

    // ── Save/Load progress bar ────────────────────────────────────────────
    saveLoadProgressBar_ = new QProgressBar;
    saveLoadProgressBar_->setMinimumWidth(150);
    saveLoadProgressBar_->setMaximumWidth(250);
    saveLoadProgressBar_->setMaximumHeight(16);
    saveLoadProgressBar_->setRange(0, 100);
    saveLoadProgressBar_->setVisible(false);
    saveLoadProgressBar_->setStyleSheet("QProgressBar { border: 1px solid palette(mid); border-radius: 2px; text-align: center; }"
                                        "QProgressBar::chunk { background-color: #3399ff; }");
    statusBar()->addPermanentWidget(saveLoadProgressBar_);

    // ── Signal wiring ─────────────────────────────────────────────────────
    connect(ndi_, &NdiReceiver::frameReady, this, &MainWindow::onFrameReady);
    connect(webcam_, &WebcamCapture::frameReady, this, &MainWindow::onWebcamFrameReady);
    connect(webcam_, &WebcamCapture::errorChanged, this, [this](const QString& err) {
        if (videoSourceKind_ == VideoSourceKind::Webcam && !err.isEmpty())
            statusNdi_->setText("Webcam error: " + err);
    });
    connect(decklink_, &DeckLinkCapture::frameReady, this, &MainWindow::onDecklinkFrameReady);
    connect(decklink_, &DeckLinkCapture::errorChanged, this, [this](const QString& err) {
        if (videoSourceKind_ != VideoSourceKind::DeckLink)
            return;
        if (!err.isEmpty())
            statusNdi_->setText("DeckLink: " + err);
    });

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
        markDirty();
    });

    connect(trackersPanel_, &TrackersPanel::trackerAccessChanged,
            this, [this](const QString& peerName, const QList<int>& ids) {
        sessionMgr_->setTrackerAccess(peerName, ids);
        project_.stationTrackers[peerName] = ids;
        if (!projectPath_.isEmpty())
            project_.save(projectPath_);
    });

    connect(streamPanel_, &StreamSourcePanel::ndiSourceSelected, this, &MainWindow::setNdiSource);
    connect(streamPanel_, &StreamSourcePanel::webcamSourceSelected, this, &MainWindow::setWebcamSource);
    connect(streamPanel_, &StreamSourcePanel::decklinkSourceSelected,
            this, [this](const QString& id, const QString& conn, uint32_t mode, bool b10) {
        setDecklinkSource(id, conn, mode, b10);
    });

    connect(networkPanel_, &NetworkSettingsPanel::configChanged,
            this, [this](const NetworkConfig& cfg) {
        project_.network = cfg;
        sessionPanel_->setSessionInterface(cfg.sessionInterface);
        psnSender_->configure(cfg);
        if (kEnableIncomingPsn) {
            psnReceiver_->stop();
            psnReceiver_->wait();
            psnReceiver_->startListening(cfg.multicastIp, cfg.port,
                                         cfg.psnMode == PsnMode::Multicast,
                                         cfg.psnInterface);
        } else {
            psnReceiver_->stop();
            psnReceiver_->wait();
        }
        sacnLastReceivedMs_ = -1;
        statusSacnIn_->setText("● sACN In");
        statusSacnIn_->setStyleSheet("color: #888888; padding: 0 8px;");
        statusSacnIn_->setVisible(cfg.sacnInput.enabled);
        const SacnInputConfig sacnCfg = cfg.sacnInput;
        QMetaObject::invokeMethod(sacnReceiver_, [this, sacnCfg]() {
            sacnReceiver_->stop();
            if (sacnCfg.enabled)
                sacnReceiver_->startListening(sacnCfg);
        });
        markDirty();
    });

    connect(calibrationPanel_, &CalibrationPanel::calibrationActiveChanged,
            this, &MainWindow::setCalibrationActive);

    connect(calibrationPanel_, &CalibrationPanel::calibrationChanged,
            this, [this](const CalibrationData& cal) {
        project_.calibration = cal;
        if (cal.isValid()) {
            calibration_.fromList(cal.homography);
            if (cal.is3DValid())
                calibration_.projectionFromList(cal.projectionMatrix);
            video_->setCalibration(&calibration_);
            stage3DPanel_->setCalibration(&calibration_, cal.stagePoints);
            video_->setCalibStagePoints(cal.stagePoints);
        }
        syncAllStageObjects();   // recomputes camera position from new calibration
        updateCalibStatus();
        if (sessionMgr_->state() == SessionManager::State::Hosting ||
            (sessionMgr_->state() == SessionManager::State::Joined &&
             sessionMgr_->localRole() == SessionRole::Admin))
            sessionMgr_->broadcastProjectState(project_);
        markDirty();
    });

    connect(calibrationPanel_, &CalibrationPanel::showFloorGridChanged,
            this, [this](bool on) {
        project_.calibrationView.showFloorGrid = on;
        video_->setShowFloorGrid(on);
        markDirty();
    });
    connect(calibrationPanel_, &CalibrationPanel::showClickPlaneChanged,
            this, [this](bool on) {
        project_.calibrationView.showClickPlane = on;
        video_->setShowClickPlane(on);
        markDirty();
    });
    connect(calibrationPanel_, &CalibrationPanel::clickPlaneHeightChanged,
            this, [this](float h) {
        if (applyingProject_) return;
        applyPlaneHeight(h);
    });
    connect(sacnReceiver_, &SacnReceiver::heightReceived,
            this, [this](float h, const QString& src) {
        sacnLastReceivedMs_ = QDateTime::currentMSecsSinceEpoch();
        sacnSourceName_     = src;
        applyPlaneHeight(h);
        statusSacnIn_->setText("● sACN In");
        statusSacnIn_->setStyleSheet("color: #33cc55; padding: 0 8px;");
    });
    connect(video_, &VideoWidget::planeHeightScrolled,
            this, [this](float delta) {
        applyPlaneHeight(qBound(0.0f, clickPlaneHeight_ + delta, 20.0f));
    });

    connect(video_, &VideoWidget::mousePosInFrame, this, [this](QPointF framePt) {
        if (video_->mouseHeld() && calibration_.isValid()) {
            int id = trackersPanel_->activeTrackerId();
            if (id >= 0) {
                QPointF raw = calibration_.pixelToStage(framePt);
                QPointF stg;
                if (calibration_.has3D())
                    stg = calibration_.pixelToStageAtHeight(framePt, clickPlaneHeight_);
                else {
                    stg = raw;
                }
                trackerRawPositions_[id] = {float(raw.x()), float(raw.y())};
                trackerPositions_[id]    = {float(stg.x()), float(stg.y())};
                statusPos_->setText(QString("X: %1m  Z: %2m")
                    .arg(stg.x(), 0, 'f', 2).arg(stg.y(), 0, 'f', 2));
                log(QString("DRAG  tracker=%1  frame=(%2,%3)  floor=(%4,%5)  plane=(%6,%7)")
                    .arg(id)
                    .arg(framePt.x(), 0, 'f', 1).arg(framePt.y(), 0, 'f', 1)
                    .arg(raw.x(), 0, 'f', 4).arg(raw.y(), 0, 'f', 4)
                    .arg(stg.x(), 0, 'f', 4).arg(stg.y(), 0, 'f', 4));
            }
        }
    });
    connect(video_, &VideoWidget::mouseLeftPressed, this, [this](QPointF framePt) {
        if (!calibration_.isValid()) return;
        int id = trackersPanel_->activeTrackerId();
        if (id < 0) return;
        QPointF raw = calibration_.pixelToStage(framePt);
        QPointF stg;
        if (calibration_.has3D())
            stg = calibration_.pixelToStageAtHeight(framePt, clickPlaneHeight_);
        else {
            stg = raw;
        }
        trackerRawPositions_[id] = {float(raw.x()), float(raw.y())};
        trackerPositions_[id]    = {float(stg.x()), float(stg.y())};
        statusPos_->setText(QString("X: %1m  Z: %2m")
            .arg(stg.x(), 0, 'f', 2).arg(stg.y(), 0, 'f', 2));
        log(QString("CLICK tracker=%1  frame=(%2,%3)  floor=(%4,%5)  plane=(%6,%7)")
            .arg(id)
            .arg(framePt.x(), 0, 'f', 1).arg(framePt.y(), 0, 'f', 1)
            .arg(raw.x(), 0, 'f', 4).arg(raw.y(), 0, 'f', 4)
            .arg(stg.x(), 0, 'f', 4).arg(stg.y(), 0, 'f', 4));
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
        for (auto* dock : panelDocks_) dock->setVisible(!isUserStation);
        menuBar()->setVisible(true);
        if (!isUserStation && state == SessionManager::State::Idle)
            video_->setAssignedTrackers({}, 255);  // restore full opacity
        if (state == SessionManager::State::Hosting)
            sessionMgr_->broadcastProjectState(project_);  // seed sharedProject_ before any peer joins
        updateSessionStatus();
        updateTrackerBarRestriction();
        updateTrackersPanelPeers();
    });
    connect(sessionMgr_, &SessionManager::peerJoined, this, [this](SessionPeer peer) {
        // Auto-restore previously saved tracker assignment for this station
        if (project_.stationTrackers.contains(peer.displayName)) {
            const QList<int>& ids = project_.stationTrackers[peer.displayName];
            sessionMgr_->setTrackerAccess(peer.displayName, ids);
        }
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

    // Debug log file — ~/onpoint-debug.log
    logFile_.setFileName(QDir::homePath() + "/onpoint-debug.log");
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
    actSaveProject_   = fileMenu->addAction("&Save Project");
    actSaveProjectAs_ = fileMenu->addAction("Save Project &As...");
    auto* actSave   = actSaveProject_;
    auto* actSaveAs = actSaveProjectAs_;
    actNew->setShortcut(QKeySequence::New);
    actOpen->setShortcut(QKeySequence::Open);
    actSave->setShortcut(QKeySequence::Save);
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(actNew,    &QAction::triggered, this, &MainWindow::onNewProject);
    connect(actOpen,   &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(actSave,   &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(actSaveAs, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();
    actCloseProject_ = fileMenu->addAction("&Close Project");
    auto* actClose = actCloseProject_;
    connect(actClose, &QAction::triggered, this, [this]() {
        if (projectDirty_) {
            QString name = projectPath_.isEmpty()
                ? QStringLiteral("Untitled")
                : QFileInfo(projectPath_).fileName();
            auto btn = QMessageBox::question(this, "Unsaved Changes",
                QString("Save changes to \"%1\" before closing?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (btn == QMessageBox::Cancel) return;
            if (btn == QMessageBox::Save)   onSaveProject();
        }
        workspaceActive_ = false;
        project_     = Project{};
        projectPath_ = QString();
        projectDirty_ = false;
        showWelcomeScreen();
    });

    // Ensure shortcuts work even if the menu bar is hidden by the OS/window mode.
    addAction(actNew);
    addAction(actOpen);
    addAction(actSave);
    addAction(actSaveAs);

    auto* recentMenu = fileMenu->addMenu("Recent Projects");
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        recentMenu->clear();
        const auto recent = recentProjects();
        if (recent.isEmpty()) {
            recentMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);
            return;
        }
        for (const auto& path : recent) {
            auto* a = recentMenu->addAction(QFileInfo(path).fileName());
            a->setToolTip(path);
            connect(a, &QAction::triggered, this, [this, path]() {
                openRecentProject(path);
            });
        }
    });
    fileMenu->addSeparator();
    auto* actExit = fileMenu->addAction("E&xit");
    connect(actExit, &QAction::triggered, qApp, &QApplication::quit);

    auto* viewMenu = menuBar()->addMenu("&View");
    for (auto* d : panelDocks_)
        viewMenu->addAction(d->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(videoDock_->toggleViewAction());
    viewMenu->addAction(stage3DDock_->toggleViewAction());
    viewMenu->addAction(stageItemsDock_->toggleViewAction());
    viewMenu->addAction(stagePropertiesDock_->toggleViewAction());
    viewMenu->addSeparator();
    auto* actResetLayout = viewMenu->addAction("Reset Layout");

    viewMenu->addSeparator();
    auto* appearanceMenu = viewMenu->addMenu("Appearance");
    auto* themeGroup     = new QActionGroup(this);
    themeGroup->setExclusive(true);
    auto* actSystem = appearanceMenu->addAction("System (follow OS)");
    auto* actDark   = appearanceMenu->addAction("Dark");
    auto* actLight  = appearanceMenu->addAction("Light");
    for (auto* a : {actSystem, actDark, actLight}) {
        a->setCheckable(true);
        themeGroup->addAction(a);
    }

    {
        QSettings s("onpoint", "onpoint");
        const QString saved = s.value("theme", "system").toString();
        if      (saved == "dark")  actDark->setChecked(true);
        else if (saved == "light") actLight->setChecked(true);
        else                       actSystem->setChecked(true);
        applyTheme(saved);
    }

    connect(actSystem, &QAction::triggered, this, [this]() { applyTheme("system"); });
    connect(actDark,   &QAction::triggered, this, [this]() { applyTheme("dark"); });
    connect(actLight,  &QAction::triggered, this, [this]() { applyTheme("light"); });

    connect(actResetLayout, &QAction::triggered, this, [this]() {
        QSettings s("onpoint", "onpoint");
        s.remove("windowGeometry");
        s.remove("windowState");
        resize(1440, 810);
        resizeDocks({videoDock_},   {1160}, Qt::Horizontal);
        resizeDocks({sessionDock_}, {280},  Qt::Horizontal);
        for (auto* d : panelDocks_) d->setVisible(true);
        streamDock_->raise();
    });

    // Restore window geometry now; dock state is deferred until workspace is shown.
    {
        QSettings s("onpoint", "onpoint");
        if (s.contains("windowGeometry")) restoreGeometry(s.value("windowGeometry").toByteArray());
        if (s.contains("windowState"))    savedWindowState_ = s.value("windowState").toByteArray();
    }

    auto* helpMenu  = menuBar()->addMenu("&Help");
    auto* actAbout  = helpMenu->addAction("About OnPoint...");
    actAbout->setMenuRole(QAction::AboutRole);
    connect(actAbout, &QAction::triggered, this, [this]() {
        oclero::qlementine::AboutDialog dlg(this);
        dlg.setApplicationName("OnPoint");
        dlg.setApplicationVersion("1.0");
        dlg.setDescription("Camera-based mouse follow spot position tracker and PSN sender.");
        dlg.setCopyright("© 2026 Hannes Rüger");
        dlg.exec();
    });

    // Reapply system theme when OS dark/light mode changes (only in "system" mode).
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
        if (currentTheme_ == "system") applyTheme("system");
    });

    // Connect welcome screen actions
    connect(welcomeScreen_, &WelcomeScreen::newRequested, this, [this]() {
        showWorkspace();
        onNewProject();
    });
    connect(welcomeScreen_, &WelcomeScreen::openRequested, this, [this](const QString& path) {
        if (path.isEmpty()) {
            onOpenProject(); // showWorkspace() is called inside, after the dialog confirms a file
        } else {
            showWorkspace();
            openRecentProject(path);
        }
    });
    connect(welcomeScreen_, &WelcomeScreen::joinRequested,
            this, [this](const QString& peerName, const QString& iface, DiscoveredSession session) {
        showWorkspace();
        onNewProject();
        project_.network.sessionInterface = iface;
        networkPanel_->setConfig(project_.network);
        sessionMgr_->joinSession(session.host, session.port, peerName);
    });

    // ── Async project save/load worker ────────────────────────────────────
    workerThread_ = new QThread(this);
    projectWorker_ = new ProjectWorker;
    projectWorker_->moveToThread(workerThread_);
    connect(workerThread_, &QThread::finished, projectWorker_, &QObject::deleteLater);
    connect(projectWorker_, &ProjectWorker::saveFinished, this, &MainWindow::onSaveFinished);
    connect(projectWorker_, &ProjectWorker::saveFailed, this, &MainWindow::onSaveFailed);
    connect(projectWorker_, &ProjectWorker::loadFinished, this, &MainWindow::onLoadFinished);
    connect(projectWorker_, &ProjectWorker::loadFailed, this, &MainWindow::onLoadFailed);
    connect(projectWorker_, &ProjectWorker::progress, this, &MainWindow::onWorkerProgress);

    workerThread_->start();

    showWelcomeScreen();
    updateSessionStatus();
}

void MainWindow::showWelcomeScreen()
{
    workspaceActive_ = false;
    welcomeScreen_->refresh(recentProjects());
    centralContainer_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    statusBar()->hide();
    actSaveProject_->setEnabled(false);
    actSaveProjectAs_->setEnabled(false);
    actCloseProject_->setEnabled(false);

    // Hide all docks so the welcome screen fills the window
    videoDock_->hide();
    stage3DDock_->hide();
    stageItemsDock_->hide();
    stagePropertiesDock_->hide();
    for (auto* d : panelDocks_) d->hide();
}

void MainWindow::showWorkspace()
{
    if (workspaceActive_) return;
    workspaceActive_ = true;

    // Shrink the central container so docks fill the window
    centralContainer_->setMaximumSize(0, 0);
    statusBar()->show();
    actSaveProject_->setEnabled(true);
    actSaveProjectAs_->setEnabled(true);
    actCloseProject_->setEnabled(true);

    // Restore dock positions/sizes from the previous workspace session.
    // Always force-show all docks afterwards: restoreState may have recorded
    // a hidden state (e.g. saved while the welcome screen was showing).
    if (!savedWindowState_.isEmpty())
        restoreState(savedWindowState_);

    videoDock_->show();
    stage3DDock_->show();
    stageItemsDock_->show();
    stagePropertiesDock_->show();
    for (auto* d : panelDocks_) d->show();
}

void MainWindow::openRecentProject(const QString& path)
{
    if (isSavingOrLoading_) return;

    isSavingOrLoading_ = true;
    setCursor(Qt::WaitCursor);
    projectPath_ = path;
    showWorkspace();
    projectWorker_->loadAsync(path);
}

MainWindow::~MainWindow() {
    timer_.stop();
    statsTimer_.stop();
    // Stop the sACN socket in its own thread before quitting it.
    // BlockingQueuedConnection ensures the socket is closed before we exit.
    QMetaObject::invokeMethod(sacnReceiver_, [this]() {
        sacnReceiver_->stop();
    }, Qt::BlockingQueuedConnection);
    sacnThread_->quit();
    sacnThread_->wait();
    // Stop the project worker thread
    if (workerThread_) {
        workerThread_->quit();
        workerThread_->wait();
    }
    psnReceiver_->stop();
    ndi_->stop();
    psnReceiver_->wait();
    ndi_->wait();
    log("=== session ended ===");
    delete logStream_;
}

void MainWindow::applyPlaneHeight(float h) {
    h = qBound(0.0f, h, 20.0f);
    clickPlaneHeight_ = h;
    project_.calibrationView.clickPlaneHeight = h;
    video_->setClickPlaneHeight(h);
    calibrationPanel_->setPlaneHeight(h);
    markDirty();
}

float MainWindow::stageHeightAt(float x, float z) const {
    for (const auto& obj : project_.stageObjects)
        if (obj.polygon.containsPoint({x, z}, Qt::OddEvenFill))
            return obj.height;
    return 0.0f;
}


void MainWindow::log(const QString& msg) {
    if (!logStream_) return;
    *logStream_ << QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ") << msg << "\n";
    logStream_->flush();
}

void MainWindow::setCalibrationActive(bool on) {
    calibActive_ = on;
    if (on) selectTracker(-1);
    trackerBar_->setCalibrationActive(on);
    trackersPanel_->setCalibrationActive(on);
}

void MainWindow::selectTracker(int id) {
    if (calibActive_ && id >= 0) return;
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

void MainWindow::loadProject(const Project& p, const QList<MvrImport>& parsedImports) {
    project_ = p;
    pendingParsedImports_ = parsedImports;
    applyProject();
    pendingParsedImports_.clear();
}

void MainWindow::setNdiSource(const QString& source) {
    bool changed = (project_.ndiSource != source || project_.videoSourceType != "ndi");
    log(QString("setNdiSource('%1') - changed=%2").arg(source).arg(changed));

    project_.ndiSource     = source;
    project_.videoSourceType = "ndi";

    videoSourceKind_ = VideoSourceKind::Ndi;
    videoSourceName_ = source;

    webcam_->stop();
    decklink_->stop();
    ndi_->connectToSource(source);
    statusNdi_->setText(source.isEmpty() ? "No NDI source" : "NDI: " + source + " (connecting…)");
    streamPanel_->setCurrentNdiSource(source);
    video_->setNdiSourceConfigured(!source.isEmpty());

    if (changed) {
        markDirty();
    }
}

void MainWindow::setWebcamSource(const QString& device) {
#if WEBCAM_AVAILABLE
    bool changed = (project_.videoSourceType != "webcam" || videoSourceName_ != device);
    log(QString("setWebcamSource('%1') - changed=%2").arg(device).arg(changed));

    videoSourceKind_ = VideoSourceKind::Webcam;
    videoSourceName_ = device;
    project_.videoSourceType = "webcam";

    // Stop NDI decoding to reduce CPU/network usage when using the webcam.
    ndi_->disconnectFromSource();
    decklink_->stop();

    webcam_->setDeviceDescription(device);
    webcam_->start();

    statusNdi_->setText(device.isEmpty() ? "No webcam" : "Webcam: " + device + " (starting…)");
    video_->setNdiSourceConfigured(!device.isEmpty());

    if (changed) {
        markDirty();
    }
#else
    (void)device;
    statusNdi_->setText("Webcam support unavailable (install Qt Multimedia)");
    video_->setNdiSourceConfigured(false);
#endif
}

void MainWindow::setDecklinkSource(const QString& deviceId, const QString& connection,
                                   uint32_t displayMode, bool allow10Bit) {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    bool changed = (project_.videoSourceType != "decklink" || project_.decklinkDevice != deviceId ||
                    project_.decklinkConnection != connection || project_.decklinkDisplayMode != displayMode ||
                    project_.decklinkAllow10Bit != allow10Bit);
    log(QString("setDecklinkSource('%1') - changed=%2").arg(deviceId).arg(changed));

    videoSourceKind_              = VideoSourceKind::DeckLink;
    videoSourceName_              = deviceId;
    project_.videoSourceType      = "decklink";
    project_.decklinkDevice       = deviceId;
    project_.decklinkConnection   = connection;
    project_.decklinkAllow10Bit   = allow10Bit;
    project_.decklinkDisplayMode  = displayMode;

    ndi_->disconnectFromSource();
    webcam_->stop();

    // Map connection name back to enum.
    DeckLinkCapture::Connection conn = DeckLinkCapture::Connection::Unspecified;
    for (auto c : {DeckLinkCapture::Connection::SDI, DeckLinkCapture::Connection::HDMI,
                   DeckLinkCapture::Connection::OpticalSDI, DeckLinkCapture::Connection::Component,
                   DeckLinkCapture::Connection::Composite, DeckLinkCapture::Connection::SVideo}) {
        if (DeckLinkCapture::connectionName(c) == connection) { conn = c; break; }
    }

    decklink_->stop();
    decklink_->setDeviceId(deviceId);
    decklink_->setConnection(conn);
    decklink_->setDisplayMode(displayMode);
    decklink_->setAllow10Bit(allow10Bit);
    decklink_->start();

    statusNdi_->setText(deviceId.isEmpty() ? "No DeckLink device" : "DeckLink: " + deviceId + " (starting…)");
    video_->setNdiSourceConfigured(!deviceId.isEmpty());

    if (changed) {
        markDirty();
    }
#else
    (void)deviceId; (void)connection; (void)displayMode; (void)allow10Bit;
    statusNdi_->setText("DeckLink support unavailable");
    video_->setNdiSourceConfigured(false);
#endif
}

void MainWindow::applyProject() {
    applyingProject_ = true;
    updateWindowTitle();
    trackersPanel_->setTrackers(project_.trackers);
    trackerBar_->setTrackers(project_.trackers);
    networkPanel_->setConfig(project_.network);
    sessionPanel_->setSessionInterface(project_.network.sessionInterface);
    psnSender_->configure(project_.network);
    if (kEnableIncomingPsn) {
        psnReceiver_->stop();
        psnReceiver_->wait();
        psnReceiver_->startListening(project_.network.multicastIp,
                                     project_.network.port,
                                     project_.network.psnMode == PsnMode::Multicast,
                                     project_.network.psnInterface);
    } else {
        psnReceiver_->stop();
        psnReceiver_->wait();
    }
    if (project_.videoSourceType == "decklink" && !project_.decklinkDevice.isEmpty()) {
        setDecklinkSource(project_.decklinkDevice, project_.decklinkConnection,
                          project_.decklinkDisplayMode, project_.decklinkAllow10Bit);
        streamPanel_->setCurrentDecklinkSource(project_.decklinkDevice,
                                               project_.decklinkConnection,
                                               project_.decklinkDisplayMode,
                                               project_.decklinkAllow10Bit);
    } else if (project_.videoSourceType != "webcam") {
        video_->setNdiSourceConfigured(!project_.ndiSource.isEmpty());
        if (!project_.ndiSource.isEmpty())
            setNdiSource(project_.ndiSource);
    }
    if (project_.calibration.isValid()) {
        calibration_.fromList(project_.calibration.homography);
        if (project_.calibration.is3DValid())
            calibration_.projectionFromList(project_.calibration.projectionMatrix);
        video_->setCalibration(&calibration_);
        const auto& h = project_.calibration.homography;
        if (h.size() == 9)
            log(QString("CALIBRATION  H=[%1 %2 %3 | %4 %5 %6 | %7 %8 %9]")
                .arg(h[0],0,'g',6).arg(h[1],0,'g',6).arg(h[2],0,'g',6)
                .arg(h[3],0,'g',6).arg(h[4],0,'g',6).arg(h[5],0,'g',6)
                .arg(h[6],0,'g',6).arg(h[7],0,'g',6).arg(h[8],0,'g',6));
        calibrationPanel_->setCalibration(project_.calibration);
        stage3DPanel_->setCalibration(&calibration_, project_.calibration.stagePoints);
        video_->setCalibStagePoints(project_.calibration.stagePoints);
    }
    syncAllStageObjects();
    stage3DPanel_->setCameraState(project_.stage3dCamera);
    const auto& cv = project_.calibrationView;
    calibrationPanel_->setViewSettings(cv.showFloorGrid, cv.clickPlaneHeight, cv.showClickPlane);
    clickPlaneHeight_ = cv.clickPlaneHeight;
    video_->setClickPlaneHeight(cv.clickPlaneHeight);
    video_->setShowFloorGrid(cv.showFloorGrid);
    video_->setShowClickPlane(cv.showClickPlane);
    video_->setCalibBoundaryVisible(cv.showCalibRectInVideo);
    sacnLastReceivedMs_ = -1;
    statusSacnIn_->setVisible(project_.network.sacnInput.enabled);
    statusSacnIn_->setText("● sACN In");
    statusSacnIn_->setStyleSheet("color: #888888; padding: 0 8px;");
    const SacnInputConfig sacnCfg = project_.network.sacnInput;
    QMetaObject::invokeMethod(sacnReceiver_, [this, sacnCfg]() {
        sacnReceiver_->stop();
        if (sacnCfg.enabled)
            sacnReceiver_->startListening(sacnCfg);
    });

    // Restore MVR imports by parsing the embedded MVR data on the main thread.
    // (libmvrgdtf requires the main thread; progress bar updated between imports.)
    mvrImports_.clear();
    {
        const int n = project_.mvr.imports.size();
        for (int i = 0; i < n; ++i) {
            // Animate progress bar: 15% (after ZIP load) → 95% spread across imports.
            if (saveLoadProgressBar_ && saveLoadProgressBar_->isVisible()) {
                setProgressAnimated(15 + 80 * i / std::max(n, 1));
                // Let the animation run visibly before the next blocking parse.
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 350);
            }

            const auto& importData = project_.mvr.imports[i];
            if (importData.mvrData.isEmpty()) continue;
            auto pr = MvrImporter::parseFromData(importData.mvrData);
            if (!pr.error.isEmpty()) continue;

            MvrImport import;
            import.name    = importData.name;
            import.offsetX = importData.offsetX;
            import.offsetY = importData.offsetY;
            import.offsetZ = importData.offsetZ;
            import.rotDeg  = importData.rotDeg;
            import.enabled = importData.enabled;
            import.mvrData = importData.mvrData;
            for (const auto& layerData : importData.layers) {
                for (auto& parsedLayer : pr.layers) {
                    if (parsedLayer.name != layerData.name) continue;
                    parsedLayer.enabled = layerData.enabled;
                    for (auto& parsedObj : parsedLayer.objects) {
                        for (const auto& objData : layerData.objects) {
                            if (parsedObj.name == objData.name) {
                                parsedObj.enabled = objData.enabled;
                                break;
                            }
                        }
                    }
                    import.layers.append(parsedLayer);
                    break;
                }
            }
            mvrImports_.append(import);
        }
        // Animate to 100% then hide.
        if (saveLoadProgressBar_ && saveLoadProgressBar_->isVisible()) {
            setProgressAnimated(100);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 450);
            saveLoadProgressBar_->setVisible(false);
        }
    }
    stage3DPanel_->setMvrImports(mvrImports_);
    stageItemsPanel_->setMvrImports(mvrImports_);
    stage3DPanel_->setShowMvrLabels(project_.mvr.showLabels);
    stage3DPanel_->setMvrRenderMode(MvrRenderMode(int(project_.mvr.renderMode)));

    updateCalibStatus();
    applyingProject_ = false;
}

void MainWindow::updateWindowTitle() {
    const QString base = QStringLiteral("OnPoint");
    const QString dirty = projectDirty_ ? QStringLiteral("*") : QString();
    if (projectPath_.isEmpty()) {
        setWindowTitle(QString("%1 — Untitled%2").arg(base, dirty));
        return;
    }
    setWindowTitle(QString("%1 — %2%3").arg(base, QFileInfo(projectPath_).fileName(), dirty));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (projectDirty_ && workspaceActive_) {
        QString name = projectPath_.isEmpty()
            ? QStringLiteral("Untitled")
            : QFileInfo(projectPath_).fileName();
        auto btn = QMessageBox::question(this, "Unsaved Changes",
            QString("Save changes to \"%1\" before closing?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) { e->ignore(); return; }
        if (btn == QMessageBox::Save)   onSaveProject();
    }
    QSettings s("onpoint", "onpoint");
    s.setValue("windowGeometry", saveGeometry());
    if (workspaceActive_) s.setValue("windowState", saveState());
    e->accept();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        const bool fs = isFullScreen();
        for (auto* dock : panelDocks_) dock->setVisible(!fs);
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
    video_->setOwnRawPositions(trackerRawPositions_);
    if (!trackerPositions_.isEmpty()) {
        QMap<int, float> heights;
        for (auto it = trackerPositions_.constBegin(); it != trackerPositions_.constEnd(); ++it)
            heights[it.key()] = stageHeightAt(it.value().first, it.value().second) + clickPlaneHeight_;
        psnSender_->sendPositions(trackerPositions_, project_.trackers, heights);
    }
    stage3DPanel_->setTrackerPositions(trackerPositions_, project_.trackers);
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

    int sacnRate = 0;
    if (elapsed > 0.0) {
        quint64 sacnTotal = sacnReceiver_->totalSacnReceived();
        sacnRate = std::max(0, static_cast<int>(
            std::llround((sacnTotal - lastSacnRxPackets_) / elapsed)));
        lastSacnRxPackets_ = sacnTotal;
    }
    const bool sacnActive = project_.network.sacnInput.enabled
        && sacnLastReceivedMs_ >= 0
        && (QDateTime::currentMSecsSinceEpoch() - sacnLastReceivedMs_) < 2000;
    if (project_.network.sacnInput.enabled) {
        statusSacnIn_->setVisible(true);
        if (!sacnActive) {
            statusSacnIn_->setText("● sACN In");
            statusSacnIn_->setStyleSheet("color: #888888; padding: 0 8px;");
        }
    }
    statsPanel_->setSacnRxInfo(project_.network.sacnInput.enabled, sacnRate, clickPlaneHeight_);

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
        statusCalib_->setText(calibration_.has3D() ? "● Calibrated · 3D" : "● Calibrated · 2D");
        statusCalib_->setStyleSheet("color: #33aa44; padding: 0 4px;");
    } else {
        statusCalib_->setText("● No calibration");
        statusCalib_->setStyleSheet("color: #cc9900; padding: 0 4px;");
    }
}

void MainWindow::updateSaveStatus() {
    updateWindowTitle();
}

void MainWindow::markDirty() {
    if (applyingProject_) {
        return;
    }
    if (!projectDirty_) {
        projectDirty_ = true;
        updateSaveStatus();
    }
}

void MainWindow::markSaved() {
    projectDirty_ = false;
    updateSaveStatus();
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
    showWorkspace();
    project_     = Project::defaultProject();
    projectPath_ = QString();
    trackerPositions_.clear();
    trackerRawPositions_.clear();
    calibration_ = Calibration{};
    calibrationPanel_->reset();
    video_->setCalibration(&calibration_);
    applyProject();
    markSaved();
}

void MainWindow::onOpenProject() {
    if (isSavingOrLoading_) return;

    QFileDialog::Options opts;
#ifdef Q_OS_MAC
    opts |= QFileDialog::DontUseNativeDialog;
#endif
    QString path = QFileDialog::getOpenFileName(
        this, "Open Project", QDir::homePath(), "OnPoint Projects (*.onpoint)", nullptr, opts);
    if (path.isEmpty()) return;

    showWorkspace();

    isSavingOrLoading_ = true;
    setCursor(Qt::WaitCursor);
    projectPath_ = path;
    projectWorker_->loadAsync(path);
}

void MainWindow::onSaveProject() {
    if (projectPath_.isEmpty()) { onSaveProjectAs(); return; }
    if (isSavingOrLoading_) return;

    isSavingOrLoading_ = true;
    setCursor(Qt::WaitCursor);
    actSaveProject_->setEnabled(false);

    project_.stage3dCamera = stage3DPanel_->getCameraState();
    projectWorker_->saveAsync(project_, projectPath_);
}

void MainWindow::onSaveProjectAs() {
    if (isSavingOrLoading_) return;

    QFileDialog::Options opts;
#ifdef Q_OS_MAC
    opts |= QFileDialog::DontUseNativeDialog;
#endif
    QString defaultName = projectPath_.isEmpty()
        ? QDir::homePath() + "/Untitled.onpoint"
        : projectPath_;
    QString path = QFileDialog::getSaveFileName(
        this, "Save Project", defaultName,
        "OnPoint Projects (*.onpoint)", nullptr, opts);
    if (path.isEmpty()) return;

    if (!path.endsWith(QStringLiteral(".onpoint"), Qt::CaseInsensitive))
        path += QStringLiteral(".onpoint");

    isSavingOrLoading_ = true;
    setCursor(Qt::WaitCursor);
    actSaveProject_->setEnabled(false);

    projectPath_ = path;
    project_.stage3dCamera = stage3DPanel_->getCameraState();
    projectWorker_->saveAsync(project_, path);
    saveRecent(path);
}

void MainWindow::onSaveFinished() {
    isSavingOrLoading_ = false;
    setCursor(Qt::ArrowCursor);
    actSaveProject_->setEnabled(true);
    if (saveLoadProgressBar_) saveLoadProgressBar_->setVisible(false);
    updateWindowTitle();
    markSaved();
}

void MainWindow::onSaveFailed(const QString& error) {
    isSavingOrLoading_ = false;
    setCursor(Qt::ArrowCursor);
    actSaveProject_->setEnabled(true);
    if (saveLoadProgressBar_) saveLoadProgressBar_->setVisible(false);
    QMessageBox::critical(this, "Save Error",
        QString("Could not save project:\n\n%1").arg(error));
}

void MainWindow::onLoadFinished(const Project& project, const QList<MvrImport>& parsedImports) {
    isSavingOrLoading_ = false;
    setCursor(Qt::ArrowCursor);
    // Do NOT hide progress bar yet — applyProject() will parse MVRs and hide it when done.

    // Set applyingProject temporarily to prevent UI updates from marking as dirty during load
    bool wasApplying = applyingProject_;
    applyingProject_ = true;
    loadProject(project, parsedImports);

    saveRecent(projectPath_);
    updateWindowTitle();
    markSaved();

    // Restore applyingProject_ after current event has been processed to catch any deferred signals
    QMetaObject::invokeMethod(this, [this, wasApplying]() {
        applyingProject_ = wasApplying;
    }, Qt::QueuedConnection);
}

void MainWindow::onLoadFailed(const QString& error) {
    isSavingOrLoading_ = false;
    setCursor(Qt::ArrowCursor);
    if (saveLoadProgressBar_) saveLoadProgressBar_->setVisible(false);
    QMessageBox::critical(this, "Invalid Showfile",
        QString("Could not open \"%1\":\n\n%2")
            .arg(QFileInfo(projectPath_).fileName(), error));
}

void MainWindow::setProgressAnimated(int target) {
    if (!saveLoadProgressBar_) return;
    if (!saveLoadProgressBar_->isVisible()) {
        saveLoadProgressBar_->setValue(0);
        saveLoadProgressBar_->setVisible(true);
    }
    if (progressAnim_) progressAnim_->stop();
    progressAnim_ = new QPropertyAnimation(saveLoadProgressBar_, "value", this);
    connect(progressAnim_, &QPropertyAnimation::destroyed,
            this, [this] { progressAnim_ = nullptr; });
    progressAnim_->setDuration(400);
    progressAnim_->setStartValue(saveLoadProgressBar_->value());
    progressAnim_->setEndValue(target);
    progressAnim_->setEasingCurve(QEasingCurve::OutCubic);
    progressAnim_->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onWorkerProgress(int percent) {
    if (!saveLoadProgressBar_) return;
    if (percent <= 0) {
        if (progressAnim_) { progressAnim_->stop(); progressAnim_ = nullptr; }
        saveLoadProgressBar_->setVisible(false);
        return;
    }
    setProgressAnimated(percent);
}

void MainWindow::saveRecent(const QString& path) {
    QSettings s("onpoint", "onpoint");
    QStringList recent = s.value("recentProjects").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    s.setValue("recentProjects", recent);
}

QStringList MainWindow::recentProjects() const {
    QSettings s("onpoint", "onpoint");
    QStringList all = s.value("recentProjects").toStringList();
    QStringList existing;
    for (const auto& p : all)
        if (QFileInfo::exists(p)) existing << p;
    if (existing.size() != all.size())
        s.setValue("recentProjects", existing);
    return existing;
}

void MainWindow::updateCameraPosition()
{
    if (calibration_.has3D()) {
        // 3D calibration: camera centre comes from the projection matrix, FOV is irrelevant
        cameraPos3D_   = calibration_.cameraCenter3D();
        camera3DValid_ = true;
    } else if (calibration_.isValid() && project_.calibration.imagePoints.size() >= 4) {
        // 2D-only calibration: estimate position from FOV + PnP
        float fov = project_.calibrationView.cameraFovDeg;
        QSize sz  = video_->frameSize();
        if (sz.isEmpty()) sz = QSize(1920, 1080);
        cameraPos3D_   = Calibration::computeCameraFromFov(
            project_.calibration.imagePoints,
            project_.calibration.stagePoints,
            fov, sz);
        camera3DValid_ = !cameraPos3D_.isNull();
    } else {
        cameraPos3D_   = {};
        camera3DValid_ = false;
    }
}

void MainWindow::updateSystemObjects()
{
    updateCameraPosition();
    systemStageItems_.clear();

    // Camera (id = -1): can show in 3D, never in video view
    StageObject cam;
    cam.id             = -1;
    cam.name           = "Camera";
    cam.color          = QColor(255, 200, 50);
    cam.fovDeg         = project_.calibrationView.cameraFovDeg;
    cam.visibleIn3D    = project_.calibrationView.showCameraIn3D;
    cam.visibleInVideo = false;
    if (camera3DValid_) {
        cam.center = QPointF(cameraPos3D_.x(), cameraPos3D_.z());
        cam.height = cameraPos3D_.y();
    }
    systemStageItems_ << cam;

    // Calibration rect (id = -2): can show in both views
    StageObject cr;
    cr.id             = -2;
    cr.name           = "Calib Rect";
    cr.color          = QColor(255, 180, 0);
    cr.visibleInVideo = project_.calibrationView.showCalibRectInVideo;
    cr.visibleIn3D    = project_.calibrationView.showCalibRectIn3D;
    if (!project_.calibration.stagePoints.isEmpty())
        cr.polygon = QPolygonF(project_.calibration.stagePoints);
    systemStageItems_ << cr;
}

void MainWindow::applyTheme(const QString& theme)
{
    currentTheme_ = theme;
    QSettings s("onpoint", "onpoint");
    s.setValue("theme", theme);
    if (!themeManager_) return;
    if (theme == "dark") {
        themeManager_->setCurrentTheme("Dark");
    } else if (theme == "light") {
        themeManager_->setCurrentTheme("Light");
    } else {
        const bool isDark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        themeManager_->setCurrentTheme(isDark ? "Dark" : "Light");
    }
}

void MainWindow::syncAllStageObjects()
{
    updateSystemObjects();

    // Panels get the combined list: system items first so they appear at the top
    stageItemsPanel_->setAllObjects(systemStageItems_ + project_.stageObjects);
    stagePropertiesPanel_->setHas3DCalibration(calibration_.has3D());
    stagePropertiesPanel_->setAllObjects(systemStageItems_ + project_.stageObjects);

    // 3D view: user objects + explicit system-item controls
    stage3DPanel_->setStageObjects(project_.stageObjects);
    stage3DPanel_->setCalibRectVisible(project_.calibrationView.showCalibRectIn3D);
    stage3DPanel_->setCameraMarker(
        cameraPos3D_,
        project_.calibrationView.cameraFovDeg,
        camera3DValid_ && project_.calibrationView.showCameraIn3D);

    // Video: user objects + calib rect boundary flag
    video_->setStageObjects(project_.stageObjects);
    video_->setCalibBoundaryVisible(project_.calibrationView.showCalibRectInVideo);
}
