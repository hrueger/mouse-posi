#include "Stage3DPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QActionGroup>
#include <QAction>

Stage3DPanel::Stage3DPanel(QWidget* parent) : QWidget(parent)
{
    view_ = new Stage3DView;

    // Camera preset toolbar
    auto* presetBar = new QToolBar;
    presetBar->setIconSize({16, 16});
    presetBar->setStyleSheet("QToolBar { spacing: 2px; }");
    auto addPreset = [&](const QString& label, CameraPreset p) {
        auto* a = presetBar->addAction(label);
        connect(a, &QAction::triggered, view_, [this, p]{ view_->applyCameraPreset(p); });
    };
    addPreset("Top",   CameraPreset::Top);
    addPreset("Front", CameraPreset::Front);
    addPreset("F-Top", CameraPreset::FrontTop);
    addPreset("Left",  CameraPreset::Left);
    addPreset("Right", CameraPreset::Right);

    // Vertical tool toolbar on the left
    auto* toolBar = new QToolBar;
    toolBar->setOrientation(Qt::Vertical);
    toolBar->setIconSize({20, 20});
    toolBar->setStyleSheet("QToolBar { spacing: 2px; }");
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
    addTool("↺", "Orbit camera (drag)", Stage3DTool::OrbitCamera)->setChecked(true);
    addTool("→", "Select object",       Stage3DTool::Select);
    addTool("□", "Draw rectangle",      Stage3DTool::DrawRect);
    addTool("⬡", "Draw polygon",        Stage3DTool::DrawPolygon);

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
