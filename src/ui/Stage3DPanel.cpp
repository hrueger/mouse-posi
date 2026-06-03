#include "Stage3DPanel.h"
#include "MvrImportDialog.h"
#include "MvrImporter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QActionGroup>
#include <QAction>
#include <QMenu>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

Stage3DPanel::Stage3DPanel(QWidget* parent) : QWidget(parent)
{
    view_ = new Stage3DView;

    // Camera preset toolbar
    auto* presetBar = new QToolBar;
    presetBar->setIconSize({16, 16});
    presetBar->setStyleSheet(QStringLiteral("QToolBar { spacing: 2px; }"));
    auto addPreset = [&](const QString& label, CameraPreset p) {
        auto* a = presetBar->addAction(label);
        connect(a, &QAction::triggered, view_, [this, p]{ view_->applyCameraPreset(p); });
    };
    addPreset(QStringLiteral("Top"),   CameraPreset::Top);
    addPreset(QStringLiteral("Front"), CameraPreset::Front);
    addPreset(QStringLiteral("F-Top"), CameraPreset::FrontTop);
    addPreset(QStringLiteral("Left"),  CameraPreset::Left);
    addPreset(QStringLiteral("Right"), CameraPreset::Right);

    presetBar->addSeparator();

    // ── View settings dropdown ─────────────────────────────────────────────
    auto* viewSettingsMenu = new QMenu(this);

    auto* showLabelsAct = viewSettingsMenu->addAction(QStringLiteral("Show Labels"));
    showLabelsAct->setCheckable(true);
    showLabelsAct->setChecked(false);  // default off
    connect(showLabelsAct, &QAction::toggled, this, [this](bool show) {
        view_->setShowMvrLabels(show);
        emit showMvrLabelsChanged(show);
    });

    viewSettingsMenu->addSeparator();
    viewSettingsMenu->addAction(QStringLiteral("Render Mode"))->setEnabled(false);

    auto* renderGroup = new QActionGroup(this);
    renderGroup->setExclusive(true);

    auto addRenderMode = [&](const QString& label, MvrRenderMode mode, bool checked) {
        auto* a = viewSettingsMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(checked);
        renderGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] {
            view_->setMvrRenderMode(mode);
            emit mvrRenderModeChanged(mode);
        });
    };
    addRenderMode(QStringLiteral("  Flat"),      MvrRenderMode::Flat,      false);
    addRenderMode(QStringLiteral("  Shaded"),    MvrRenderMode::Shaded,    true);
    addRenderMode(QStringLiteral("  Wireframe"), MvrRenderMode::Wireframe, false);

    auto* viewBtn = new QToolButton;
    viewBtn->setText(QStringLiteral("View ▾"));
    viewBtn->setMenu(viewSettingsMenu);
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    presetBar->addWidget(viewBtn);

    presetBar->addSeparator();
    auto* importMvr = presetBar->addAction(QStringLiteral("Import MVR…"));
    connect(importMvr, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import MVR"),
            QString(), QStringLiteral("MVR Files (*.mvr)"));
        if (path.isEmpty()) return;

        const auto pr = MvrImporter::parse(path);
        if (!pr.error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("MVR Import Error"), pr.error);
            return;
        }
        if (pr.layers.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("MVR Import"),
                                     QStringLiteral("No layers found in this MVR file."));
            return;
        }

        MvrImportDialog dlg(pr.layers, this);
        if (dlg.exec() != QDialog::Accepted) return;

        MvrImport import;
        import.name   = QFileInfo(path).completeBaseName();
        import.layers = dlg.selectedLayers();
        QFile mvrFile(path);
        if (mvrFile.open(QIODevice::ReadOnly))
            import.mvrData = mvrFile.readAll();
        // MainWindow owns the accumulated import: it merges this file's layers
        // with any previously imported MVRs and pushes the combined result back
        // to the view and items panel. Don't set the view directly here, or the
        // earlier imports would be dropped.
        emit mvrImportChanged(import);
    });

    // Vertical tool toolbar on the left
    auto* toolBar = new QToolBar;
    toolBar->setOrientation(Qt::Vertical);
    toolBar->setIconSize({20, 20});
    toolBar->setStyleSheet(QStringLiteral("QToolBar { spacing: 2px; }"));
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    auto addTool = [&](const QString& label, const QString& tip, Stage3DTool t) {
        auto* a = toolBar->addAction(label);
        a->setToolTip(tip);
        a->setCheckable(true);
        toolGroup->addAction(a);
        connect(a, &QAction::triggered, view_, [this, t]{ view_->setActiveTool(t); });
        return a;
    };
    addTool(QStringLiteral("↺"), QStringLiteral("Orbit camera (drag)"), Stage3DTool::OrbitCamera)->setChecked(true);
    addTool(QStringLiteral("→"), QStringLiteral("Select object"),       Stage3DTool::Select);
    addTool(QStringLiteral("□"), QStringLiteral("Draw rectangle"),      Stage3DTool::DrawRect);
    addTool(QStringLiteral("⬡"), QStringLiteral("Draw polygon"),        Stage3DTool::DrawPolygon);

    auto* midRow = new QWidget;
    auto* mrH = new QHBoxLayout(midRow);
    mrH->setContentsMargins(0, 0, 0, 0);
    mrH->setSpacing(0);
    mrH->addWidget(toolBar);
    mrH->addWidget(view_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(presetBar);
    layout->addWidget(midRow, 1);

    // Forward Stage3DView signals
    connect(view_, &Stage3DView::polygonDrawn,  this, &Stage3DPanel::polygonDrawn);
    connect(view_, &Stage3DView::rectDrawn,     this, &Stage3DPanel::rectDrawn);
    connect(view_, &Stage3DView::objectSelected,this, &Stage3DPanel::objectSelected);
}

void Stage3DPanel::setCalibration(const Calibration* c, const QList<QPointF>& pts)
{
    view_->setCalibration(c, pts);
}

void Stage3DPanel::setStageObjects(const QList<StageObject>& objs)
{
    view_->setStageObjects(objs);
}

void Stage3DPanel::setTrackerPositions(const QMap<int, QPair<float,float>>& pos,
                                        const QList<TrackerConfig>& trackers)
{
    view_->setTrackerPositions(pos, trackers);
}

void Stage3DPanel::setSelectedObject(int id)
{
    view_->setSelectedObject(id);
}

void Stage3DPanel::setActiveTool(Stage3DTool tool)
{
    view_->setActiveTool(tool);
}

void Stage3DPanel::setMvrImports(const QList<MvrImport>& imports)
{
    view_->setMvrImports(imports);
}

void Stage3DPanel::setShowMvrLabels(bool show)
{
    view_->setShowMvrLabels(show);
}

void Stage3DPanel::setMvrRenderMode(MvrRenderMode mode)
{
    view_->setMvrRenderMode(mode);
}

Stage3DCameraState Stage3DPanel::getCameraState() const
{
    return view_->getCameraState();
}

void Stage3DPanel::setCameraState(const Stage3DCameraState& s)
{
    view_->setCameraState(s);
}

void Stage3DPanel::setCalibRectVisible(bool visible)
{
    view_->setCalibRectVisible(visible);
}

void Stage3DPanel::setCameraMarker(QVector3D pos, float fovDeg, bool visible)
{
    view_->setCameraMarker(pos, fovDeg, visible);
}
