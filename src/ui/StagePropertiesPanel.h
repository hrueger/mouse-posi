#pragma once
#include <QWidget>
#include <QList>
#include "Project.h"
#include "MvrImporter.h"

class QFormLayout;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;

class StagePropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit StagePropertiesPanel(QWidget* parent = nullptr);

    void setAllObjects(const QList<StageObject>& all);
    void setSelectedObject(int id);
    void setHas3DCalibration(bool has3D);
    void setMvrImport(int index, const MvrImport& import);

signals:
    void objectEdited(const StageObject& obj);
    void mvrImportEdited(int index, MvrImport import);

private slots:
    void onPropertiesChanged();
    void onMvrPropertiesChanged();

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

    bool has3DCalib_ = false;

    QList<StageObject> objects_;
    MvrImport          mvrImport_;
    int  mvrImportIndex_ = -1;
    int  selectedId_   = -999;
    bool updatingForm_ = false;
};
