#pragma once
#include <QWidget>
#include <QList>
#include <QMap>
#include <QPair>
#include <QVector3D>
#include "Project.h"
#include "Stage3DView.h"
#include "MvrImporter.h"

class Calibration;
class QToolButton;
class QAction;

class Stage3DPanel : public QWidget {
    Q_OBJECT
public:
    explicit Stage3DPanel(QWidget* parent = nullptr);

    void setCalibration(const Calibration* c, const QList<QPointF>& stagePoints);
    void setStageObjects(const QList<StageObject>& objs);
    void setTrackerPositions(const QMap<int, QPair<float,float>>& pos,
                             const QList<TrackerConfig>& trackers);
    void setSelectedObject(int id);
    void setActiveTool(Stage3DTool tool);
    void setMvrImports(const QList<MvrImport>& imports);
    void setShowMvrLabels(bool show);
    void setMvrRenderMode(MvrRenderMode mode);

    Stage3DCameraState getCameraState() const;
    void               setCameraState(const Stage3DCameraState& s);

    void setCalibRectVisible(bool visible);
    void setCameraMarker(QVector3D pos, float fovDeg, bool visible);

signals:
    void polygonDrawn(QPolygonF polygon);
    void rectDrawn(QPointF center, float width, float depth);
    void objectSelected(int id);
    void mvrImportChanged(MvrImport import);
    void mvrImportCleared();
    void showMvrLabelsChanged(bool show);
    void mvrRenderModeChanged(MvrRenderMode mode);

private:
    Stage3DView* view_;
};
