#pragma once
#include <QWidget>
#include <QList>
#include <QPointF>
#include "Project.h"

class VideoWidget;
class NdiReceiver;
class Calibration;
class QFrame;
class QPushButton;
class QStackedWidget;
class QComboBox;
class QListWidget;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;

class CalibrationPanel : public QWidget {
    Q_OBJECT
public:
    CalibrationPanel(VideoWidget* video, NdiReceiver* ndi,
                     QWidget* mainWindow, QWidget* parent = nullptr);
    ~CalibrationPanel() override;

    void setCalibration(const CalibrationData& cal);
    void setViewSettings(bool showFloorGrid, float clickPlaneHeight, bool showClickPlane);
    void setPlaneHeight(float h);
    void reset();
    CalibrationData calibration() const { return result_; }

signals:
    void calibrationChanged(CalibrationData cal);
    void calibrationActiveChanged(bool active);
    void showFloorGridChanged(bool on);
    void clickPlaneHeightChanged(float h);
    void showClickPlaneChanged(bool on);

private slots:
    void onCalibrateToggled(bool on);
    void onSchemeChanged(int idx);
    void onCompute();
    void onTest(bool checked);
    void onMousePosInFrame(QPointF imagePos);
    // Rectangle
    void onRectAction();
    void onRectReset();
    void onRectDimChanged();
    // Manual
    void onManualSetOrigin();
    void onManualAddPoint();
    void onManualRemove();
    // Overlay
    void onOverlayXChanged(double val);
    void onOverlayZChanged(double val);

private:
    enum class Scheme  { Rectangle, Manual, Rect3D };
    enum class Placing { None, RectCorner, Rect3DCorner, ManualOrigin, ManualPoint };

    QWidget* buildRectPage();
    QWidget* buildManualPage();
    QWidget* buildRect3DPage();
    void     buildPointOverlay();

    void rectOnCornerPlaced(QPointF imagePos);
    void updateRectOverlay();
    void updateRectStepRows();
    void updateRectActionBtn();

    void rect3DOnCornerPlaced(QPointF imagePos);
    void updateRect3DStepRows();
    void updateRect3DActionBtn();
    void updateRect3DOverlay();

    void manualOnOriginPlaced(QPointF imagePos);
    void manualOnPointPlaced(QPointF imagePos);
    void updateManualOverlay();
    void updateManualList();
    void updateManualStatus();

    void showPointOverlay(int index, QPoint videoWidgetPos);
    void hidePointOverlay();

    void updateComputeButton();
    void loadExisting(const CalibrationData& data);
    void update3DControls();

    void disconnectVideoSignals();
    void connectVideoSignals();

    VideoWidget*    video_;
    NdiReceiver*    ndi_;
    QWidget*        mainWindow_;

    QPushButton*    calibrateBtn_;
    QComboBox*      schemeCombo_;
    QStackedWidget* schemeStack_;
    QPushButton*    computeBtn_;
    QLabel*         errorLabel_;
    QCheckBox*      testCheck_;
    bool            active_ = false;

    // Point edit overlay (manual mode) — parented to mainWindow_
    QFrame*         pointOverlay_;
    QLabel*         overlayTitle_;
    QWidget*        overlaySXRow_;
    QWidget*        overlaySZRow_;
    QDoubleSpinBox* overlaySX_;
    QDoubleSpinBox* overlaySZ_;
    int             editingIndex_ = -2;

    // Rectangle mode (2D)
    QDoubleSpinBox* rectWSpin_;
    QDoubleSpinBox* rectHSpin_;
    QLabel*         rectStepRows_[4];
    QPushButton*    rectActionBtn_;
    QPushButton*    rectResetBtn_;
    QList<QPointF>  rectCorners_;
    int             rectStep_ = 0;

    // 3D Rectangle mode
    QDoubleSpinBox* rect3DWSpin_;
    QDoubleSpinBox* rect3DHSpin_;
    QDoubleSpinBox* rect3DMarkerSpin_;
    QLabel*         rect3DStepRows_[8];   // 0-3 = floor, 4-7 = elevated
    QPushButton*    rect3DActionBtn_;
    QPushButton*    rect3DResetBtn_;
    QList<QPointF>  rect3DCorners_;       // 0-3 = floor, 4-7 = elevated
    int             rect3DStep_ = 0;

    // Manual mode
    QListWidget*    manualList_;
    QLabel*         manualStatusLabel_;
    QPushButton*    manualOriginBtn_;
    QPushButton*    manualAddBtn_;
    QPushButton*    manualRemoveBtn_;
    QPointF         originImagePoint_;
    bool            hasOrigin_ = false;
    QList<QPointF>  imagePoints_;
    QList<QPointF>  stagePoints_;

    Scheme          scheme_   = Scheme::Rect3D;
    Placing         placing_  = Placing::None;
    bool            testMode_ = false;

    Calibration*    previewCal_;
    CalibrationData result_;

    // Height controls
    QWidget*        has3DControls_;   // disabled until 3D calibration exists
    QCheckBox*      floorGridCheck_;
    QSlider*        clickPlaneSlider_;
    QDoubleSpinBox* clickPlaneSpin_;
    QCheckBox*      showClickPlaneCheck_;

    // Track signal connections so we can disconnect on toggle-off
    QList<QMetaObject::Connection> videoConnections_;
};
