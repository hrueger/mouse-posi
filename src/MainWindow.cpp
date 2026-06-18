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
#include "DmxSender.h"
#include "DmxReceiver.h"
#include "PanTiltCalculator.h"
#include "Calibration.h"
#include "adapters/InputAdapterBase.h"
#include "adapters/SacnArtNetInputAdapter.h"
#include "adapters/MidiInputAdapter.h"
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
#include "ui/SettingsDialog.h"
#include "ui/InputAdaptersPanel.h"
#include "ui/FixturesPanel.h"
#include "ui/GdtfLibraryDialog.h"
#include "ui/DmxMonitorPanel.h"
#include "ui/NewProjectWizard.h"
#include "GdtfLibrary.h"
#include "ui/StreamSourcePanel.h"
#include <oclero/qlementine/style/ThemeManager.hpp>
#include <QToolBar>
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
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
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
    dmxSender_   = new DmxSender(this);
    dmxReceiver_ = new DmxReceiver(this);
    sessionMgr_  = new SessionManager(this);

    sessionPanel_          = new SessionPanel(sessionMgr_);
    stage3DPanel_          = new Stage3DPanel;
    stageItemsPanel_       = new StageItemsPanel;
    stagePropertiesPanel_  = new StagePropertiesPanel;
    streamPanel_     = new StreamSourcePanel(ndi_);
    calibrationPanel_= new CalibrationPanel(video_, ndi_, this);
    trackersPanel_   = new TrackersPanel;
    statsPanel_      = new StatsPanel;
    fixturesPanel_   = new FixturesPanel;
    settingsDialog_  = new SettingsDialog(this);
    // Stream source panel moves into settings dialog
    settingsDialog_->setStreamSourcePanel(streamPanel_);
    connect(settingsDialog_->adaptersPanel(), &InputAdaptersPanel::requestLearn,
            this, [this]() {
        for (auto* a : inputAdapters_)
            if (auto* midi = qobject_cast<MidiInputAdapter*>(a))
                midi->setLearning(true);
    });

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
    calibrationDock_ = makeDock("Calibration",   "dock_calibration", calibrationPanel_);
    trackersDock_    = makeDock("Trackers",       "dock_trackers",    trackersPanel_);
    statsDock_       = makeDock("Stats",          "dock_stats",       statsPanel_);
    stage3DDock_          = makeDock("Stage 3D",          "dock_stage3d",          stage3DPanel_);
    stageItemsDock_       = makeDock("Stage Objects",     "dock_stage_items",      stageItemsPanel_);
    stagePropertiesDock_  = makeDock("Object Properties", "dock_stage_properties", stagePropertiesPanel_);
    fixturesDock_         = makeDock("Fixtures",          "dock_fixtures",         fixturesPanel_);
    dmxMonitorPanel_      = new DmxMonitorPanel;
    dmxMonitorDock_       = makeDock("DMX Monitor",       "dock_dmx_monitor",      dmxMonitorPanel_);
    panelDocks_           = {sessionDock_, calibrationDock_,
                             trackersDock_, statsDock_};

    // Default layout: all 4 panels tabified on the right
    addDockWidget(Qt::RightDockWidgetArea, sessionDock_);
    for (auto* d : panelDocks_.sliced(1))
        tabifyDockWidget(panelDocks_[0], d);
    sessionDock_->raise();

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

    // Fixtures dock: tabified alongside object properties
    addDockWidget(Qt::RightDockWidgetArea, fixturesDock_);
    tabifyDockWidget(stagePropertiesDock_, fixturesDock_);

    // DMX Monitor dock: tabified alongside fixtures
    addDockWidget(Qt::RightDockWidgetArea, dmxMonitorDock_);
    tabifyDockWidget(fixturesDock_, dmxMonitorDock_);

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
        if (id == -21) {
            const QVector3D off(project_.network.psnOffsetX,
                                project_.network.psnOffsetY,
                                project_.network.psnOffsetZ);
            stagePropertiesPanel_->setPsnOrigin(off, project_.network.psnRotDeg);
        }
        stagePropertiesPanel_->setSelectedObject(id);
        stage3DPanel_->setSelectedObject(id);
    });

    // Items panel: selection changed
    connect(stageItemsPanel_, &StageItemsPanel::selectionChanged, this, [this](int id) {
        stage3DPanel_->setSelectedObject(id);
        if (id == -21) {
            const QVector3D off(project_.network.psnOffsetX,
                                project_.network.psnOffsetY,
                                project_.network.psnOffsetZ);
            stagePropertiesPanel_->setPsnOrigin(off, project_.network.psnRotDeg);
        }
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
        if (mvrImports_.isEmpty()) {
            commitMvrImport(import, -1, false);
            return;
        }

        // ── Ask: add or replace ───────────────────────────────────────────────
        int replaceIndex = -1;
        {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(QStringLiteral("Import MVR"));
            if (mvrImports_.size() == 1) {
                msgBox.setText(QString("An MVR import named \"%1\" already exists.")
                               .arg(mvrImports_[0].name));
            } else {
                msgBox.setText(QString("%1 MVR imports already exist.").arg(mvrImports_.size()));
            }
            msgBox.setInformativeText("What would you like to do?");
            auto* addBtn     = msgBox.addButton(QStringLiteral("Add as second import"), QMessageBox::AcceptRole);
            auto* replaceBtn = msgBox.addButton(QStringLiteral("Replace existing…"),    QMessageBox::DestructiveRole);
            msgBox.addButton(QMessageBox::Cancel);
            msgBox.exec();

            if (msgBox.clickedButton() == nullptr) return;
            if (msgBox.clickedButton() == addBtn) {
                commitMvrImport(import, -1, false);
                return;
            }
            if (msgBox.clickedButton() != replaceBtn) return;

            // Determine which import to replace
            if (mvrImports_.size() == 1) {
                replaceIndex = 0;
            } else {
                QDialog picker(this);
                picker.setWindowTitle(QStringLiteral("Replace MVR Import"));
                auto* lay   = new QVBoxLayout(&picker);
                auto* lbl   = new QLabel(QStringLiteral("Which import do you want to replace?"), &picker);
                auto* combo = new QComboBox(&picker);
                for (const auto& imp : mvrImports_)
                    combo->addItem(imp.name);
                auto* btns  = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
                lay->addWidget(lbl);
                lay->addWidget(combo);
                lay->addWidget(btns);
                connect(btns, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
                connect(btns, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
                if (picker.exec() != QDialog::Accepted) return;
                replaceIndex = combo->currentIndex();
            }
        }

        // ── Ask: copy config or clean ─────────────────────────────────────────
        bool copyConfig = false;
        {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(QStringLiteral("Replace MVR Import"));
            msgBox.setText(QString("How should the existing configuration of \"%1\" be handled?")
                           .arg(mvrImports_[replaceIndex].name));
            msgBox.setInformativeText(
                "Copy configuration preserves stage position, rotation, "
                "visibility settings, and tracker assignments.");
            auto* copyBtn = msgBox.addButton(QStringLiteral("Copy configuration"), QMessageBox::AcceptRole);
            msgBox.addButton(QStringLiteral("Import cleanly"), QMessageBox::DestructiveRole);
            msgBox.addButton(QMessageBox::Cancel);
            msgBox.exec();

            if (msgBox.clickedButton() == nullptr || msgBox.clickedButton() == msgBox.button(QMessageBox::Cancel))
                return;
            copyConfig = (msgBox.clickedButton() == copyBtn);
        }

        commitMvrImport(import, replaceIndex, copyConfig);
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

            // Remove fixtureUniverseConfigs for universes no longer present in any import
            QSet<quint16> usedUnis;
            for (const auto& imp : project_.mvr.imports)
                for (const auto& layer : imp.layers)
                    for (const auto& obj : layer.objects)
                        if (obj.type == MvrObjectData::Type::Fixture)
                            usedUnis.insert(quint16(obj.universe));
            project_.fixtureUniverseConfigs.removeIf(
                [&](const FixtureUniverseConfig& c){ return !usedUnis.contains(c.fixtureUniverse); });
            settingsDialog_->setFixtureUniverseConfigs(project_.fixtureUniverseConfigs);
            settingsDialog_->setMvrData(project_.mvr);
            reconfigureDmxOutput();

            stage3DPanel_->setMvrImports(mvrImports_);
            stageItemsPanel_->setMvrImports(mvrImports_);
    stagePropertiesPanel_->setMvrImports(mvrImports_);
            fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
            dmxMonitorPanel_->setMvrImports(project_.mvr.imports);
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
    stagePropertiesPanel_->setMvrImports(mvrImports_);
            markDirty();
        }
    });

    // Items panel: MVR layer/object selected (not the root) → clear properties panel
    connect(stageItemsPanel_, &StageItemsPanel::mvrChildItemSelected,
            this, [this]() {
        stagePropertiesPanel_->setSelectedObject(-999);
    });

    connect(stageItemsPanel_, &StageItemsPanel::mvrFixtureSelected,
            this, [this](int importIdx, int layerIdx, int objIdx) {
        if (importIdx < 0 || importIdx >= project_.mvr.imports.size()) return;
        stagePropertiesPanel_->setMvrFixture(importIdx, layerIdx, objIdx,
                                             project_.mvr.imports[importIdx],
                                             project_.trackers);
    });

    connect(stagePropertiesPanel_, &StagePropertiesPanel::mvrFixtureTrackerLinkChanged,
            this, [this](int importIdx, int layerIdx, int objIdx, int trackerLink) {
        if (importIdx < 0 || importIdx >= project_.mvr.imports.size()) return;
        auto& imp = project_.mvr.imports[importIdx];
        if (layerIdx < 0 || layerIdx >= imp.layers.size()) return;
        auto& layer = imp.layers[layerIdx];
        if (objIdx < 0 || objIdx >= layer.objects.size()) return;
        layer.objects[objIdx].trackerLink = trackerLink;
        fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
        markDirty();
    });

    connect(fixturesPanel_, &FixturesPanel::trackerLinkChanged,
        this, [this](int importIdx, int layerIdx, int objIdx, int trackerLink) {
            if (importIdx < 0 || importIdx >= project_.mvr.imports.size()) return;
            auto& imp = project_.mvr.imports[importIdx];
            if (layerIdx < 0 || layerIdx >= imp.layers.size()) return;
            auto& layer = imp.layers[layerIdx];
            if (objIdx < 0 || objIdx >= layer.objects.size()) return;
            layer.objects[objIdx].trackerLink = trackerLink;
            markDirty();
        });

    connect(fixturesPanel_, &FixturesPanel::gdtfAssignRequested,
            this, &MainWindow::onAssignGdtf);

    connect(fixturesPanel_, &FixturesPanel::dmxAddressChanged, this,
        [this](int importIdx, int layerIdx, int objIdx, int universe, int address) {
            if (importIdx < 0 || importIdx >= project_.mvr.imports.size()) return;
            auto& imp = project_.mvr.imports[importIdx];
            if (layerIdx < 0 || layerIdx >= imp.layers.size()) return;
            auto& layer = imp.layers[layerIdx];
            if (objIdx < 0 || objIdx >= layer.objects.size()) return;
            layer.objects[objIdx].universe   = universe;
            layer.objects[objIdx].dmxAddress = address;
            syncFixtureUniverseConfigs();
            markDirty();
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

    // Properties panel: PSN origin edited
    connect(stagePropertiesPanel_, &StagePropertiesPanel::psnOriginEdited,
            this, [this](QVector3D offset, float rotDeg) {
        project_.network.psnOffsetX = offset.x();
        project_.network.psnOffsetY = offset.y();
        project_.network.psnOffsetZ = offset.z();
        project_.network.psnRotDeg  = rotDeg;
        stage3DPanel_->setPsnOrigin(offset, rotDeg);
        video_->setPsnOrigin(offset, rotDeg);
        markDirty();
    });

    // Items panel: vid / 3D visibility toggled
    connect(stageItemsPanel_, &StageItemsPanel::visibilityChanged,
            this, [this](int id, bool inVideo, bool in3D) {
        if (id == -20) {
            project_.network.showStageOriginIn3D = in3D;
            stage3DPanel_->setShowStageOrigin(in3D);
            markDirty();
        } else if (id == -21) {
            project_.network.showPsnOriginIn3D = in3D;
            stage3DPanel_->setShowPsnOrigin(in3D);
            markDirty();
        } else if (id == -1) {
            project_.calibrationView.showCameraIn3D = in3D;
            syncAllStageObjects(); markDirty();
        } else if (id == -2) {
            project_.calibrationView.showCalibRectInVideo = inVideo;
            project_.calibrationView.showCalibRectIn3D    = in3D;
            syncAllStageObjects(); markDirty();
        } else {
            for (auto& o : project_.stageObjects) {
                if (o.id == id) { o.visibleInVideo = inVideo; o.visibleIn3D = in3D; break; }
            }
            syncAllStageObjects(); markDirty();
        }
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

    auto* settingsBtn = new QPushButton("⚙ Project Settings");
    settingsBtn->setFlat(true);
    connect(settingsBtn, &QPushButton::clicked, settingsDialog_, &SettingsDialog::showModeTab);
    statusBar()->addPermanentWidget(settingsBtn);

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
        else
            statusNdi_->setText("DeckLink: " + videoSourceName_);
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
        fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
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

    connect(settingsDialog_, &SettingsDialog::networkConfigChanged,
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
        const QVector3D psnOff(cfg.psnOffsetX, cfg.psnOffsetY, cfg.psnOffsetZ);
        stage3DPanel_->setPsnOrigin(psnOff, cfg.psnRotDeg);
        video_->setPsnOrigin(psnOff, cfg.psnRotDeg);
        markDirty();
    });
    connect(settingsDialog_, &SettingsDialog::inputAdaptersChanged,
            this, [this](const QList<InputAdapterConfig>& adapters) {
        project_.inputAdapters = adapters;
        reconfigureInputAdapters();
        markDirty();
    });
    connect(settingsDialog_, &SettingsDialog::fixtureUniverseConfigsChanged,
            this, [this](const QList<FixtureUniverseConfig>& configs) {
        project_.fixtureUniverseConfigs = configs;
        reconfigureDmxOutput();
        markDirty();
    });
    connect(settingsDialog_, &SettingsDialog::operatingModeChanged, this, [this](OperatingMode mode) {
        project_.operatingMode = mode;
        reconfigureDmxOutput();
        stage3DDock_->setVisible(mode != OperatingMode::Camera2D);
        calibrationPanel_->setOperatingMode(mode);
        const bool showOut = mode == OperatingMode::Stage3DPSN && !calibration_.has3D();
        stage3DPanel_->setShowOutputMarkers(showOut);
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
    auto* actProjectSettings = fileMenu->addAction("Project &Settings…");
    connect(actProjectSettings, &QAction::triggered, settingsDialog_, &SettingsDialog::showModeTab);

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

    auto* fixtureMenu = menuBar()->addMenu("Fi&xtures");
    auto* actGdtfLib  = fixtureMenu->addAction("GDTF &Library…");
    connect(actGdtfLib, &QAction::triggered, this, &MainWindow::openGdtfLibrary);

    auto* viewMenu = menuBar()->addMenu("&View");
    for (auto* d : panelDocks_)
        viewMenu->addAction(d->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(videoDock_->toggleViewAction());
    viewMenu->addAction(stage3DDock_->toggleViewAction());
    viewMenu->addAction(stageItemsDock_->toggleViewAction());
    viewMenu->addAction(stagePropertiesDock_->toggleViewAction());
    viewMenu->addAction(fixturesDock_->toggleViewAction());
    viewMenu->addAction(dmxMonitorDock_->toggleViewAction());
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
        fixturesDock_->setVisible(true);
        sessionDock_->raise();
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
        onNewProject();  // handles showWorkspace() internally
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
        project_     = Project::defaultProject();
        projectPath_ = QString();
        calibration_ = Calibration{};
        calibrationPanel_->reset();
        video_->setCalibration(&calibration_);
        project_.network.sessionInterface = iface;
        applyProject();
        markSaved();
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
    fixturesDock_->hide();
    dmxMonitorDock_->hide();
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
    fixturesDock_->show();
    dmxMonitorDock_->show();
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
    stage3DPanel_->setOutputMarkerHeight(h);
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
    video_->setFrame(QImage{});
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
    project_.webcamDevice    = device;

    // Stop NDI decoding to reduce CPU/network usage when using the webcam.
    ndi_->disconnectFromSource();
    decklink_->stop();
    video_->setFrame(QImage{});

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
    video_->setFrame(QImage{});
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
    settingsDialog_->setNetworkConfig(project_.network);
    settingsDialog_->setInputAdapters(project_.inputAdapters);
    settingsDialog_->setFixtureUniverseConfigs(project_.fixtureUniverseConfigs);
    settingsDialog_->setMvrData(project_.mvr);
    settingsDialog_->setOperatingMode(project_.operatingMode);
    calibrationPanel_->setOperatingMode(project_.operatingMode);
    sessionPanel_->setSessionInterface(project_.network.sessionInterface);
    psnSender_->configure(project_.network);
    reconfigureDmxOutput();
    reconfigureInputAdapters();
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
    } else if (project_.videoSourceType == "webcam") {
        setWebcamSource(project_.webcamDevice);
    } else {
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
    stage3DPanel_->setOutputMarkerHeight(cv.clickPlaneHeight);
    video_->setShowFloorGrid(cv.showFloorGrid);
    video_->setShowClickPlane(cv.showClickPlane);
    video_->setCalibBoundaryVisible(cv.showCalibRectInVideo);
    QMetaObject::invokeMethod(sacnReceiver_, [this]() {
        sacnReceiver_->stop();
    });

    // Restore MVR imports by parsing the embedded MVR data on the main thread.
    // (libmvrgdtf requires the main thread; progress bar updated between imports.)
    mvrImports_.clear();
    {
        const int n = project_.mvr.imports.size();
        for (int i = 0; i < n; ++i) {
            const auto& importData = project_.mvr.imports[i];
            if (importData.mvrData.isEmpty()) continue;

            // Start a long animation covering this import's slice of the bar
            // (15–95% spread across imports). The animation will actually tick
            // because processEvents is called inside the per-object tickCb below.
            if (saveLoadProgressBar_ && saveLoadProgressBar_->isVisible()) {
                const int from = 15 + 80 *  i      / std::max(n, 1);
                const int to   = 15 + 80 * (i + 1) / std::max(n, 1);
                if (progressAnim_) progressAnim_->stop();
                progressAnim_ = new QPropertyAnimation(saveLoadProgressBar_, "value", this);
                connect(progressAnim_, &QPropertyAnimation::destroyed,
                        this, [this] { progressAnim_ = nullptr; });
                progressAnim_->setDuration(10000); // generous upper bound; stopped early on finish
                progressAnim_->setStartValue(from);
                progressAnim_->setEndValue(to);
                progressAnim_->setEasingCurve(QEasingCurve::Linear);
                progressAnim_->start(QAbstractAnimation::DeleteWhenStopped);
            }

            // Throttled processEvents tick — called after each scene object so
            // animations run and the window stays repaintable during the parse.
            // ExcludeUserInputEvents keeps re-entrant user actions (open, close) blocked.
            QElapsedTimer tickTimer;
            tickTimer.start();
            qint64 lastTick = -16; // fire on first call
            auto tickCb = [&]() {
                const qint64 now = tickTimer.elapsed();
                if (now - lastTick >= 16) { // ~60 fps
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    lastTick = now;
                }
            };

            auto pr = MvrImporter::parseFromData(importData.mvrData, tickCb);
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
        // Stop any in-progress parse animation and snap to 100%.
        if (saveLoadProgressBar_ && saveLoadProgressBar_->isVisible()) {
            setProgressAnimated(100);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 200);
            saveLoadProgressBar_->setVisible(false);
        }
    }
    stage3DPanel_->setMvrImports(mvrImports_);
    stageItemsPanel_->setMvrImports(mvrImports_);
    stagePropertiesPanel_->setMvrImports(mvrImports_);
    stage3DPanel_->setShowMvrLabels(project_.mvr.showLabels);
    stage3DPanel_->setMvrRenderMode(MvrRenderMode(int(project_.mvr.renderMode)));
    fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
    if (dmxMonitorPanel_)
        dmxMonitorPanel_->setMvrImports(project_.mvr.imports);

    {
        const QVector3D psnOff(project_.network.psnOffsetX,
                               project_.network.psnOffsetY,
                               project_.network.psnOffsetZ);
        stage3DPanel_->setPsnOrigin(psnOff, project_.network.psnRotDeg);
        video_->setPsnOrigin(psnOff, project_.network.psnRotDeg);
        stage3DPanel_->setShowStageOrigin(project_.network.showStageOriginIn3D);
        stage3DPanel_->setShowPsnOrigin(project_.network.showPsnOriginIn3D);
        stageItemsPanel_->setOriginVisibility(project_.network.showStageOriginIn3D,
                                              project_.network.showPsnOriginIn3D);
    }

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
        switch (project_.operatingMode) {
        case OperatingMode::Stage3DPSN: {
            QMap<int, float> heights;
            for (auto it = trackerPositions_.constBegin(); it != trackerPositions_.constEnd(); ++it)
                heights[it.key()] = stageHeightAt(it.value().first, it.value().second) + clickPlaneHeight_;
            psnSender_->sendPositions(trackerPositions_, project_.trackers, heights);
            break;
        }
        case OperatingMode::Stage3DDMX:
        case OperatingMode::Camera2D:
            sendDmxForMode();
            break;
        }
    }
    stage3DPanel_->setTrackerPositions(trackerPositions_, project_.trackers);
    stage3DPanel_->setShowOutputMarkers(project_.operatingMode == OperatingMode::Stage3DPSN
                                        && !calibration_.has3D());
    frameCount_++;
}

void MainWindow::updateStatsTimer() {
    double elapsed = statsElapsed_.restart() / 1000.0;
    currentFps_  = frameCount_ / elapsed;
    frameCount_  = 0;
    if (elapsed > 0.0) {
        videoFps_ = videoFrameCount_ / elapsed;
        videoFrameCount_ = 0;
    }

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

    int dmxRxRate = 0;
    if (elapsed > 0.0) {
        quint64 dmxTotal = quint64(dmxReceiver_->totalFramesReceived());
        dmxRxRate = std::max(0, static_cast<int>(
            std::llround(double(dmxTotal - lastSacnRxPackets_) / elapsed)));
        lastSacnRxPackets_ = dmxTotal;
    }
    const bool hasDmxControl = std::any_of(project_.inputAdapters.begin(), project_.inputAdapters.end(),
        [](const InputAdapterConfig& c){ return c.enabled && c.type == InputAdapterType::SacnArtNet; });
    statsPanel_->setSacnRxInfo(hasDmxControl, dmxRxRate, clickPlaneHeight_);

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

static QString fpsStr(double fps) {
    if (fps < 1.0) return QString();
    return QStringLiteral("  %1 fps").arg(qRound(fps));
}

void MainWindow::onFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::Ndi)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("NDI: %1  %2×%3%4")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height())
        .arg(fpsStr(videoFps_)));
}

void MainWindow::onWebcamFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::Webcam)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("Webcam: %1  %2×%3%4")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height())
        .arg(fpsStr(videoFps_)));
}

void MainWindow::onDecklinkFrameReady(const QImage& frame) {
    if (videoSourceKind_ != VideoSourceKind::DeckLink)
        return;

    handleVideoFrame(frame);
    statusNdi_->setText(QString("DeckLink: %1  %2×%3%4")
        .arg(videoSourceName_).arg(frame.width()).arg(frame.height())
        .arg(fpsStr(videoFps_)));
}

void MainWindow::handleVideoFrame(const QImage& frame) {
    ++videoFrameCount_;

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
    // If called from the welcome screen, showWorkspace() already ran.
    // Run the wizard before showing the workspace if we're still on the welcome screen.
    const bool wasActive = workspaceActive_;
    if (!wasActive) showWorkspace();

    NewProjectWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted) {
        if (!wasActive) showWelcomeScreen();
        return;
    }

    project_     = Project::defaultProject();
    projectPath_ = QString();
    trackerPositions_.clear();
    trackerRawPositions_.clear();
    calibration_ = Calibration{};
    calibrationPanel_->reset();
    video_->setCalibration(&calibration_);

    project_.operatingMode = wizard.selectedMode();
    project_.trackers      = wizard.trackers();

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

// ── DMX output routing ────────────────────────────────────────────────────────

void MainWindow::reconfigureDmxOutput() {
    dmxSender_->stop();
    dmxReceiver_->stop();
    if (dmxMonitorPanel_)
        disconnect(dmxReceiver_, nullptr, dmxMonitorPanel_, nullptr);

    if (project_.operatingMode == OperatingMode::Stage3DPSN) return;

    // Build effective universe lists from FixtureUniverseConfig
    effectiveDmxUniverses_.clear();
    QList<DmxUniverseConfig> outConfigs, inConfigs;

    for (const auto& cfg : project_.fixtureUniverseConfigs) {
        const quint16 outNum = (cfg.outputUniverse >= 0)
            ? quint16(cfg.outputUniverse) : cfg.fixtureUniverse;

        // InFixtures entry — always subscribe so the monitor can show all input universes
        if (cfg.inputEnabled) {
            DmxUniverseEntry in;
            in.number    = cfg.fixtureUniverse;
            in.role      = DmxUniverseRole::InFixtures;
            in.protocol  = cfg.inputProtocol;
            in.netMode   = cfg.inputNetMode;
            in.iface     = cfg.inputIface;
            in.unicastIp = cfg.inputUnicastIp;
            effectiveDmxUniverses_.append(in);
            DmxUniverseConfig uc;
            uc.universe  = in.number;
            uc.protocol  = in.protocol;
            uc.netMode   = in.netMode;
            uc.iface     = in.iface;
            uc.unicastIp = in.unicastIp;
            inConfigs.append(uc);
        }

        // OutFixtures entry — only for follow-spot universes (OnPoint sends pan/tilt)
        if (cfg.hasFollowSpots && cfg.outputEnabled) {
            DmxUniverseEntry out;
            out.number            = outNum;
            out.role              = DmxUniverseRole::OutFixtures;
            out.protocol          = cfg.outputProtocol;
            out.netMode           = cfg.outputNetMode;
            out.iface             = cfg.outputIface;
            out.unicastIp         = cfg.outputUnicastIp;
            out.mergeFromUniverse = (cfg.outputUniverse >= 0) ? int(cfg.fixtureUniverse) : -1;
            effectiveDmxUniverses_.append(out);
            DmxUniverseConfig uc;
            uc.universe  = out.number;
            uc.protocol  = out.protocol;
            uc.netMode   = out.netMode;
            uc.iface     = out.iface;
            uc.unicastIp = out.unicastIp;
            outConfigs.append(uc);
        }
    }

    if (!outConfigs.isEmpty())
        dmxSender_->configure(outConfigs);
    if (!inConfigs.isEmpty()) {
        dmxReceiver_->configure(inConfigs);
        if (dmxMonitorPanel_) {
            connect(dmxReceiver_, &DmxReceiver::dmxFrameReceived,
                    dmxMonitorPanel_, &DmxMonitorPanel::updateInFrame);
        }
    }

    if (dmxMonitorPanel_)
        dmxMonitorPanel_->setUniverses(effectiveDmxUniverses_);
}

void MainWindow::reconfigureInputAdapters() {
    for (auto* a : inputAdapters_) { a->stop(); a->deleteLater(); }
    inputAdapters_.clear();

    for (const auto& cfg : project_.inputAdapters) {
        if (!cfg.enabled) continue;
        InputAdapterBase* adapter = nullptr;
        if (cfg.type == InputAdapterType::SacnArtNet)
            adapter = new SacnArtNetInputAdapter(cfg, this);
        else if (cfg.type == InputAdapterType::Midi)
            adapter = new MidiInputAdapter(cfg, this);
        if (!adapter) continue;
        connect(adapter, &InputAdapterBase::clickPlaneHeightChanged,
                this, [this](float h) { applyPlaneHeight(h); });
        if (auto* midi = qobject_cast<MidiInputAdapter*>(adapter)) {
            connect(midi, &MidiInputAdapter::midiEventReceived,
                    settingsDialog_->adaptersPanel(), &InputAdaptersPanel::logMidiEvent);
            connect(midi, &MidiInputAdapter::learnedCC,
                    settingsDialog_->adaptersPanel(), &InputAdaptersPanel::applyLearnedCC);
        }
        inputAdapters_.append(adapter);
        adapter->start();
    }
}

void MainWindow::sendDmxForMode() {
    QMap<quint16, QByteArray> frames;
    QMap<QString, FixtureStatus> statusMap;
    QList<FixtureRay> fixtureRays;

    auto getMergeUniverse = [&](quint16 outUni) -> int {
        for (const auto& e : effectiveDmxUniverses_)
            if (e.role == DmxUniverseRole::OutFixtures && e.number == outUni)
                return e.mergeFromUniverse;
        return -1;
    };

    // Returns the effective output universe for a fixture, or 0 if not configured for follow-spots
    auto resolveOutputUni = [&](quint16 fixtureUni) -> quint16 {
        for (const auto& cfg : project_.fixtureUniverseConfigs)
            if (cfg.fixtureUniverse == fixtureUni && cfg.hasFollowSpots)
                return (cfg.outputUniverse >= 0) ? quint16(cfg.outputUniverse) : fixtureUni;
        return 0; // 0 = not a follow-spot universe, skip
    };
    auto getFrame = [&](quint16 uni) -> QByteArray& {
        auto it = frames.find(uni);
        if (it == frames.end()) {
            int mergeUni = getMergeUniverse(uni);
            if (mergeUni >= 0)
                frames[uni] = dmxReceiver_->latestFrame(quint16(mergeUni));
            else
                frames[uni] = QByteArray(512, '\0');
        }
        return frames[uni];
    };

    auto writeChannel = [](QByteArray& frame, int baseAddr, const GdtfChannelInfo& ch, quint16 val16) {
        if (ch.address < 0) return;
        const int coarse = baseAddr + ch.address - 1;
        if (coarse >= 0 && coarse < 512)
            frame[coarse] = char(val16 >> 8);
        if (ch.is16bit) {
            const int fine = baseAddr + ch.address2 - 1;
            if (fine >= 0 && fine < 512)
                frame[fine] = char(val16 & 0xFF);
        }
    };

    if (project_.operatingMode == OperatingMode::Stage3DDMX) {
        for (int ii = 0; ii < project_.mvr.imports.size(); ++ii) {
            const auto& imp = project_.mvr.imports[ii];
            if (!imp.enabled) continue;
            for (int li = 0; li < imp.layers.size(); ++li) {
                const auto& layer = imp.layers[li];
                if (!layer.enabled) continue;
                for (int oi = 0; oi < layer.objects.size(); ++oi) {
                    const auto& obj = layer.objects[oi];
                    if (!obj.enabled) continue;
                    if (obj.type != MvrObjectData::Type::Fixture) continue;
                    if (obj.trackerLink < 0) continue;
                    if (!trackerPositions_.contains(obj.trackerLink)) continue;

                    // Apply import offset+rotation to get world-space fixture position
                    QMatrix4x4 importModel;
                    importModel.translate(imp.offsetX, imp.offsetY, imp.offsetZ);
                    if (imp.rotDeg != 0.f) importModel.rotate(imp.rotDeg, 0, 1, 0);
                    const QVector3D fixturePos = importModel.map(obj.positionM);

                    const auto& pos = trackerPositions_[obj.trackerLink];
                    const float targetY = stageHeightAt(pos.first, pos.second) + clickPlaneHeight_;
                    const QVector3D target(pos.first, targetY, pos.second);

                    // Combined world-from-fixture rotation: import rotation applied on top of fixture's MVR rotation.
                    QMatrix4x4 importRot;
                    importRot.rotate(imp.rotDeg, 0, 1, 0);
                    const QMatrix4x4 fixtureRot = importRot * obj.xformRot;

                    // Transform target direction into fixture-local space to compute pan/tilt.
                    // Moving-head convention: beam at rest = local -Y (straight down); pan=0/tilt=0.
                    const QVector3D v_world = target - fixturePos;
                    const QVector3D v_local = fixtureRot.inverted().mapVector(v_world);
                    const float horizDist = std::sqrt(v_local.x() * v_local.x() + v_local.z() * v_local.z());
                    const float tiltDeg = qRadiansToDegrees(std::atan2(horizDist, -v_local.y()));
                    const float panDeg  = qRadiansToDegrees(std::atan2(-v_local.x(), -v_local.z()));

                    const QString key = QString("%1-%2-%3").arg(ii).arg(li).arg(oi);
                    FixtureStatus& st = statusMap[key];
                    st.active  = true;
                    st.panDeg  = panDeg;
                    st.tiltDeg = tiltDeg;

                    // Reconstruct beam direction from pan/tilt so the ray exactly matches what is sent
                    // to the fixture — any angle error will be visible in the 3D view.
                    {
                        const float tiltRad = qDegreesToRadians(tiltDeg);
                        const float panRad  = qDegreesToRadians(panDeg);
                        // Beam in fixture-local space (pan then tilt on a downward-pointing head).
                        const QVector3D d_local(-std::sin(panRad) * std::sin(tiltRad),
                                                 -std::cos(tiltRad),
                                                 -std::cos(panRad) * std::sin(tiltRad));
                        FixtureRay fr;
                        fr.origin    = fixturePos;
                        fr.direction = fixtureRot.mapVector(d_local).normalized();
                        for (const auto& tc : project_.trackers)
                            if (tc.id == obj.trackerLink) { fr.color = tc.color; break; }
                        if (!fr.color.isValid()) fr.color = Qt::white;
                        fixtureRays.append(fr);
                    }

                    // Compute DMX values for display and output (GDTF profile required)
                    if (!obj.gdtfProfile.valid) continue;
                    const PanTiltDmx pt = PanTiltCalculator::calculate(
                        fixturePos, target, obj.gdtfProfile, fixtureRot);
                    if (!pt.valid) continue;
                    st.panDmx  = pt.pan;
                    st.tiltDmx = pt.tilt;

                    const quint16 outUni = resolveOutputUni(quint16(obj.universe));
                    if (outUni == 0) continue; // not a follow-spot universe
                    QByteArray& frame = getFrame(outUni);
                    if (frame.size() < 512) frame.resize(512, '\0');
                    const int baseAddr = obj.dmxAddress - 1;
                    writeChannel(frame, baseAddr, obj.gdtfProfile.pan,  pt.pan);
                    writeChannel(frame, baseAddr, obj.gdtfProfile.tilt, pt.tilt);
                }
            }
        }
    } else if (project_.operatingMode == OperatingMode::Camera2D) {
        if (project_.camera2DCalib.valid) {
            for (int ii = 0; ii < project_.mvr.imports.size(); ++ii) {
                const auto& imp = project_.mvr.imports[ii];
                if (!imp.enabled) continue;
                for (int li = 0; li < imp.layers.size(); ++li) {
                    const auto& layer = imp.layers[li];
                    if (!layer.enabled) continue;
                    for (int oi = 0; oi < layer.objects.size(); ++oi) {
                        const auto& obj = layer.objects[oi];
                        if (!obj.enabled) continue;
                        if (obj.type != MvrObjectData::Type::Fixture) continue;
                        if (obj.trackerLink < 0) continue;
                        if (!trackerPositions_.contains(obj.trackerLink)) continue;

                        const auto& rawPos = trackerRawPositions_[obj.trackerLink];
                        const QPointF panTilt = Calibration::pixelToPanTilt(
                            project_.camera2DCalib,
                            QPointF(rawPos.first, rawPos.second));
                        if (panTilt.x() < 0) continue;

                        const quint16 panVal  = quint16(qBound(0.0, panTilt.x(), 65535.0));
                        const quint16 tiltVal = quint16(qBound(0.0, panTilt.y(), 65535.0));

                        const QString key = QString("%1-%2-%3").arg(ii).arg(li).arg(oi);
                        FixtureStatus& st = statusMap[key];
                        st.active  = true;
                        st.panDmx  = panVal;
                        st.tiltDmx = tiltVal;

                        const quint16 outUni = resolveOutputUni(quint16(obj.universe));
                        if (outUni == 0) continue; // not a follow-spot universe
                        QByteArray& frame = getFrame(outUni);
                        if (frame.size() < 512) frame.resize(512, '\0');
                        const int baseAddr = obj.dmxAddress - 1;
                        writeChannel(frame, baseAddr, obj.gdtfProfile.pan,  panVal);
                        writeChannel(frame, baseAddr, obj.gdtfProfile.tilt, tiltVal);
                    }
                }
            }
        }
    }

    fixturesPanel_->updateStatus(statusMap);
    stage3DPanel_->setFixtureRays(fixtureRays);

    const bool hasOutputs = std::any_of(
        project_.fixtureUniverseConfigs.begin(), project_.fixtureUniverseConfigs.end(),
        [](const FixtureUniverseConfig& c){ return c.hasFollowSpots && c.outputEnabled; });
    if (hasOutputs) {
        for (auto it = frames.constBegin(); it != frames.constEnd(); ++it) {
            dmxSender_->sendFrame(it.key(), it.value());
            if (dmxMonitorPanel_)
                dmxMonitorPanel_->updateOutFrame(it.key(), it.value());
        }
    }
}

void MainWindow::openGdtfLibrary()
{
    if (!gdtfLibraryDialog_) {
        gdtfLibraryDialog_ = new GdtfLibraryDialog(false, {}, this);
        gdtfLibraryDialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(gdtfLibraryDialog_, &QDialog::destroyed, this,
                [this]() { gdtfLibraryDialog_ = nullptr; });
    }
    gdtfLibraryDialog_->show();
    gdtfLibraryDialog_->raise();
    gdtfLibraryDialog_->activateWindow();
}

void MainWindow::onAssignGdtf(int importIdx, int layerIdx, int objIdx)
{
    if (importIdx < 0 || importIdx >= project_.mvr.imports.size()) return;
    auto& imp = project_.mvr.imports[importIdx];
    if (layerIdx < 0 || layerIdx >= imp.layers.size()) return;
    auto& layer = imp.layers[layerIdx];
    if (objIdx < 0 || objIdx >= layer.objects.size()) return;
    const auto& obj = layer.objects[objIdx];

    GdtfLibraryDialog dlg(true, project_.mvr.imports, this);
    dlg.preselectEntry(obj.gdtfSpec, obj.gdtfProfile.modeName);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString gdtfPath = dlg.selectedGdtfPath();
    if (gdtfPath.isEmpty()) return;

    const GdtfDmxProfile profile = GdtfLibrary::loadProfile(gdtfPath, dlg.selectedModeName());
    if (!profile.valid) {
        QMessageBox::warning(this, "Invalid GDTF",
            "Could not read the GDTF file.");
        return;
    }

    // Ask whether to assign to all fixtures of the same type
    const QString gdtfSpec = obj.gdtfSpec;
    int sameTypeCount = 0;
    if (!gdtfSpec.isEmpty()) {
        for (const auto& i2 : project_.mvr.imports)
            for (const auto& l2 : i2.layers)
                for (const auto& o2 : l2.objects)
                    if (o2.type == MvrObjectData::Type::Fixture
                            && o2.gdtfSpec == gdtfSpec)
                        ++sameTypeCount;
    }

    bool assignAll = false;
    if (sameTypeCount > 1) {
        auto* msgBox = new QMessageBox(this);
        msgBox->setWindowTitle("Assign GDTF");
        msgBox->setText(QString("Assign the GDTF profile to:"));
        msgBox->setInformativeText(
            QString("%1 fixture \"%2\" only, or all %3 unassigned \"%2\" fixtures?")
                .arg(1).arg(gdtfSpec).arg(sameTypeCount));
        auto* btnThis = msgBox->addButton("Just This Fixture", QMessageBox::AcceptRole);
        auto* btnAll  = msgBox->addButton(
            QString("All %1 Fixtures").arg(sameTypeCount), QMessageBox::AcceptRole);
        msgBox->addButton(QMessageBox::Cancel);
        msgBox->exec();
        if (msgBox->clickedButton() == btnAll)  assignAll = true;
        else if (msgBox->clickedButton() != btnThis) return;
    }

    // Apply profile — use explicit indices to avoid pointer-comparison issues
    const QString newGdtfSpec = QFileInfo(gdtfPath).fileName();
    for (int ii = 0; ii < project_.mvr.imports.size(); ++ii)
        for (int li = 0; li < project_.mvr.imports[ii].layers.size(); ++li)
            for (int oi = 0; oi < project_.mvr.imports[ii].layers[li].objects.size(); ++oi) {
                auto& o2 = project_.mvr.imports[ii].layers[li].objects[oi];
                if (o2.type != MvrObjectData::Type::Fixture) continue;
                const bool isThis = (ii == importIdx && li == layerIdx && oi == objIdx);
                const bool matchType = !gdtfSpec.isEmpty() && o2.gdtfSpec == gdtfSpec;
                if (isThis || (assignAll && matchType)) {
                    o2.gdtfProfile = profile;
                    o2.gdtfSpec    = newGdtfSpec;
                }
            }

    fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
    markDirty();
}

// ── MVR import helpers ────────────────────────────────────────────────────────

static MvrImportData buildMvrImportData(const MvrImport& import)
{
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
            objData.name        = obj.name;
            objData.type        = MvrObjectData::Type(int(obj.type));
            objData.positionM   = obj.positionM;
            objData.xformRot    = obj.xformRot;
            objData.gdtfSpec    = obj.gdtfSpec;
            objData.unitNumber  = obj.unitNumber;
            objData.fixtureId   = obj.fixtureId;
            objData.dmxAddress  = obj.dmxAddress;
            objData.universe    = obj.universe;
            objData.enabled     = obj.enabled;
            objData.gdtfProfile = obj.gdtfProfile;
            layerData.objects.append(objData);
        }
        data.layers.append(layerData);
    }
    return data;
}

// Match objects by fixtureId (preferred) or name, and copy user-configured state.
static void copyMvrConfig(MvrImport& newImport, MvrImportData& newData,
                          const MvrImport& oldImport, const MvrImportData& oldData)
{
    newImport.offsetX = oldImport.offsetX;
    newImport.offsetY = oldImport.offsetY;
    newImport.offsetZ = oldImport.offsetZ;
    newImport.rotDeg  = oldImport.rotDeg;
    newImport.enabled = oldImport.enabled;
    newData.offsetX   = oldData.offsetX;
    newData.offsetY   = oldData.offsetY;
    newData.offsetZ   = oldData.offsetZ;
    newData.rotDeg    = oldData.rotDeg;
    newData.enabled   = oldData.enabled;

    for (auto& newLayer : newImport.layers) {
        for (const auto& oldLayer : oldImport.layers) {
            if (newLayer.name != oldLayer.name) continue;
            newLayer.enabled = oldLayer.enabled;
            for (auto& newObj : newLayer.objects) {
                for (const auto& oldObj : oldLayer.objects) {
                    const bool match = (!newObj.fixtureId.isEmpty() && newObj.fixtureId == oldObj.fixtureId)
                                       || newObj.name == oldObj.name;
                    if (!match) continue;
                    newObj.enabled = oldObj.enabled;
                    break;
                }
            }
            break;
        }
    }

    for (auto& newLayer : newData.layers) {
        for (const auto& oldLayer : oldData.layers) {
            if (newLayer.name != oldLayer.name) continue;
            newLayer.enabled = oldLayer.enabled;
            for (auto& newObj : newLayer.objects) {
                for (const auto& oldObj : oldLayer.objects) {
                    const bool match = (!newObj.fixtureId.isEmpty() && newObj.fixtureId == oldObj.fixtureId)
                                       || newObj.name == oldObj.name;
                    if (!match) continue;
                    newObj.enabled     = oldObj.enabled;
                    newObj.trackerLink = oldObj.trackerLink;
                    break;
                }
            }
            break;
        }
    }
}

void MainWindow::syncFixtureUniverseConfigs()
{
    // Collect every universe number currently referenced by any fixture
    QSet<quint16> usedUnis;
    for (const auto& imp : project_.mvr.imports)
        for (const auto& layer : imp.layers)
            for (const auto& obj : layer.objects)
                if (obj.type == MvrObjectData::Type::Fixture)
                    usedUnis.insert(quint16(obj.universe));

    // Add entries for any new universes
    for (quint16 uni : usedUnis) {
        const bool exists = std::any_of(project_.fixtureUniverseConfigs.begin(),
            project_.fixtureUniverseConfigs.end(),
            [uni](const FixtureUniverseConfig& c){ return c.fixtureUniverse == uni; });
        if (!exists) {
            FixtureUniverseConfig cfg;
            cfg.fixtureUniverse = uni;
            project_.fixtureUniverseConfigs.append(cfg);
        }
    }

    // Remove entries for universes no longer in use
    project_.fixtureUniverseConfigs.removeIf(
        [&](const FixtureUniverseConfig& c){ return !usedUnis.contains(c.fixtureUniverse); });

    settingsDialog_->setFixtureUniverseConfigs(project_.fixtureUniverseConfigs);
    reconfigureDmxOutput();
}

void MainWindow::commitMvrImport(MvrImport import, int replaceIndex, bool copyConfig)
{
    MvrImportData data = buildMvrImportData(import);

    if (replaceIndex >= 0 && replaceIndex < mvrImports_.size()) {
        if (copyConfig)
            copyMvrConfig(import, data, mvrImports_[replaceIndex], project_.mvr.imports[replaceIndex]);
        mvrImports_[replaceIndex]            = import;
        project_.mvr.imports[replaceIndex]   = data;
    } else {
        mvrImports_.append(import);
        project_.mvr.imports.append(data);
    }

    stage3DPanel_->setMvrImports(mvrImports_);
    stageItemsPanel_->setMvrImports(mvrImports_);
    stagePropertiesPanel_->setMvrImports(mvrImports_);
    fixturesPanel_->setData(project_.mvr.imports, project_.trackers);
    dmxMonitorPanel_->setMvrImports(project_.mvr.imports);

    syncFixtureUniverseConfigs();
    settingsDialog_->setMvrData(project_.mvr);

    markDirty();
}
