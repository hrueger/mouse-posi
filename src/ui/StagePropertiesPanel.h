#pragma once
#include <QWidget>
#include <QList>
#include "Project.h"
#include "MvrImporter.h"

class QFormLayout;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;

class StagePropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit StagePropertiesPanel(QWidget* parent = nullptr);

    void setAllObjects(const QList<StageObject>& all);
    void setSelectedObject(int id);
    void setHas3DCalibration(bool has3D);
    void setMvrImport(int index, const MvrImport& import);
    void setTrackers(const QList<TrackerConfig>& trackers);
    void setMvrFixture(int importIdx, int layerIdx, int objIdx,
                       const MvrImportData& importData, const QList<TrackerConfig>& trackers);
    void setPsnOrigin(QVector3D offset, float rotDeg);
    void setMvrImports(const QList<MvrImport>& imports);

signals:
    void objectEdited(const StageObject& obj);
    void mvrImportEdited(int index, MvrImport import);
    void mvrFixtureTrackerLinkChanged(int importIdx, int layerIdx, int objIdx, int trackerLink);
    void psnOriginEdited(QVector3D offset, float rotDeg);

private slots:
    void onPropertiesChanged();
    void onMvrPropertiesChanged();
    void onPsnOriginChanged();

private:
    void updatePropertiesForm(int id);
    void applyPropertiesToSelected();

    QLabel*         noSelectionLabel_;

    // Stage object properties
    QWidget*        propsGroup_;
    QFormLayout*    propsForm_;
    QLineEdit*      nameLine_;
    QDoubleSpinBox* heightSpin_;
    QDoubleSpinBox* xSpin_;
    QDoubleSpinBox* zSpin_;
    QDoubleSpinBox* widthSpin_;
    QDoubleSpinBox* depthSpin_;
    QDoubleSpinBox* rotSpin_;
    QDoubleSpinBox* fovSpin_;
    QLabel*         fovNoteLabel_;
    QLabel*         camPosLabel_;

    int  heightFormRow_  = -1;
    int  xFormRow_       = -1;
    int  zFormRow_       = -1;
    int  widthFormRow_   = -1;
    int  depthFormRow_   = -1;
    int  rotFormRow_     = -1;
    int  fovFormRow_     = -1;
    int  fovNoteFormRow_ = -1;
    int  camPosFormRow_  = -1;

    // MVR group properties
    QWidget*        mvrPropsGroup_;
    QLineEdit*      mvrNameEdit_;
    QDoubleSpinBox* mvrOffsetXSpin_;
    QDoubleSpinBox* mvrOffsetYSpin_;
    QDoubleSpinBox* mvrOffsetZSpin_;
    QDoubleSpinBox* mvrRotSpin_;

    // MVR fixture properties (tracker link)
    QWidget*        fixtureGroup_;
    QLabel*         fixtureNameLabel_;
    QLabel*         fixtureDmxLabel_;
    QLabel*         fixtureGdtfLabel_;
    QComboBox*      trackerLinkCombo_;
    int             fixtureImportIdx_ = -1;
    int             fixtureLayerIdx_  = -1;
    int             fixtureObjIdx_    = -1;

    // PSN origin properties
    QWidget*        psnOriginGroup_;
    QDoubleSpinBox* psnXSpin_;
    QDoubleSpinBox* psnYSpin_;
    QDoubleSpinBox* psnZSpin_;
    QDoubleSpinBox* psnRotSpin_;
    QPushButton*    snapToMvrBtn_;

    // Stage origin (read-only info label)
    QWidget*        stageOriginGroup_;

    bool has3DCalib_ = false;

    QList<StageObject>  objects_;
    QList<TrackerConfig> trackers_;
    QList<MvrImport>    mvrImports_;
    MvrImport           mvrImport_;
    int  mvrImportIndex_ = -1;
    int  selectedId_   = -999;
    bool updatingForm_ = false;
};
