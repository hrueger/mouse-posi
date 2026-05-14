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

class CalibrationPanel : public QWidget {
    Q_OBJECT
public:
    CalibrationPanel(VideoWidget* video, NdiReceiver* ndi,
                     QWidget* mainWindow, QWidget* parent = nullptr);
    ~CalibrationPanel() override;

    void setCalibration(const CalibrationData& cal);
    CalibrationData calibration() const { return result_; }

signals:
    void calibrationChanged(CalibrationData cal);

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
    enum class Scheme  { Rectangle, Manual };
    enum class Placing { None, RectCorner, ManualOrigin, ManualPoint };

    QWidget* buildRectPage();
    QWidget* buildManualPage();
    void     buildPointOverlay();

    void rectOnCornerPlaced(QPointF imagePos);
    void updateRectOverlay();
    void updateRectStepRows();
    void updateRectActionBtn();

    void manualOnOriginPlaced(QPointF imagePos);
    void manualOnPointPlaced(QPointF imagePos);
    void updateManualOverlay();
    void updateManualList();
    void updateManualStatus();

    void showPointOverlay(int index, QPoint videoWidgetPos);
    void hidePointOverlay();

    void updateComputeButton();
    void loadExisting(const CalibrationData& data);

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

    // Rectangle mode
    QDoubleSpinBox* rectWSpin_;
    QDoubleSpinBox* rectHSpin_;
    QLabel*         rectStepRows_[4];
    QPushButton*    rectActionBtn_;
    QPushButton*    rectResetBtn_;
    QList<QPointF>  rectCorners_;
    int             rectStep_ = 0;

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

    Scheme          scheme_   = Scheme::Rectangle;
    Placing         placing_  = Placing::None;
    bool            testMode_ = false;

    Calibration*    previewCal_;
    CalibrationData result_;

    // Track signal connections so we can disconnect on toggle-off
    QList<QMetaObject::Connection> videoConnections_;
};
