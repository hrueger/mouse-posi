#include "CalibrationPanel.h"
#include "../VideoWidget.h"
#include "../NdiReceiver.h"
#include "../Calibration.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QStackedWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QFrame>
#include <limits>
#include <cmath>

CalibrationPanel::CalibrationPanel(VideoWidget* video, NdiReceiver* ndi,
                                    QWidget* mainWindow, QWidget* parent)
    : QWidget(parent), video_(video), ndi_(ndi), mainWindow_(mainWindow),
      previewCal_(new Calibration)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    calibrateBtn_ = new QPushButton("Start Calibration");
    calibrateBtn_->setCheckable(true);
    calibrateBtn_->setToolTip("Toggle calibration placement mode on the video");
    layout->addWidget(calibrateBtn_);

    // Mode selector — 3D Rectangle is the default (index 0)
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel("Mode:"));
    schemeCombo_ = new QComboBox;
    schemeCombo_->addItem("3D Rectangle");
    schemeCombo_->addItem("Rectangle");
    schemeCombo_->addItem("Manual");
    modeRow->addWidget(schemeCombo_, 1);
    layout->addLayout(modeRow);

    schemeStack_ = new QStackedWidget;
    schemeStack_->addWidget(buildRect3DPage());
    schemeStack_->addWidget(buildRectPage());
    schemeStack_->addWidget(buildManualPage());
    layout->addWidget(schemeStack_);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    computeBtn_ = new QPushButton("Compute");
    computeBtn_->setEnabled(false);
    layout->addWidget(computeBtn_);

    testCheck_ = new QCheckBox("Test mode (hover shows coords)");
    layout->addWidget(testCheck_);

    errorLabel_ = new QLabel("No calibration.");
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet("color: palette(brightText); font-size: 11px;");
    layout->addWidget(errorLabel_);

    auto* resetBtn = new QPushButton("Reset Calibration");
    layout->addWidget(resetBtn);

    // ── Height section ────────────────────────────────────────────────────
    auto* heightSep = new QFrame;
    heightSep->setFrameShape(QFrame::HLine);
    heightSep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(heightSep);

    // Helper: add a label + slider/spinbox row to any layout.
    auto mkHeightRow = [&](QBoxLayout* tl, const QString& labelText,
                           QSlider*& slider, QDoubleSpinBox*& spin) {
        tl->addWidget(new QLabel(labelText));
        auto* row = new QHBoxLayout;
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 2000);
        slider->setValue(0);
        spin = new QDoubleSpinBox;
        spin->setRange(0.0, 20.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        spin->setSuffix(" m");
        spin->setFixedWidth(72);
        row->addWidget(slider, 1);
        row->addWidget(spin);
        tl->addLayout(row);
    };

    // Controls that require a 3D calibration — disabled until one exists.
    has3DControls_ = new QWidget;
    auto* h3l = new QVBoxLayout(has3DControls_);
    h3l->setContentsMargins(0, 0, 0, 0);
    h3l->setSpacing(6);

    floorGridCheck_ = new QCheckBox("Show floor grid (1×1 m)");
    h3l->addWidget(floorGridCheck_);

    mkHeightRow(h3l, "Height (click / PSN output):", clickPlaneSlider_, clickPlaneSpin_);

    showClickPlaneCheck_ = new QCheckBox("Show click plane");
    h3l->addWidget(showClickPlaneCheck_);

    has3DControls_->setEnabled(false);
    layout->addWidget(has3DControls_);

    // Slider ↔ spinbox bidirectional sync; emit the signal from whichever side changed.
    connect(clickPlaneSlider_, &QSlider::valueChanged, this, [this](int v) {
        QSignalBlocker b(clickPlaneSpin_);
        clickPlaneSpin_->setValue(v / 100.0);
        emit clickPlaneHeightChanged(float(v / 100.0));
    });
    connect(clickPlaneSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        QSignalBlocker b(clickPlaneSlider_);
        clickPlaneSlider_->setValue(qRound(v * 100.0));
        emit clickPlaneHeightChanged(float(v));
    });

    connect(floorGridCheck_, &QCheckBox::toggled,
            this, &CalibrationPanel::showFloorGridChanged);
    connect(showClickPlaneCheck_, &QCheckBox::toggled,
            this, &CalibrationPanel::showClickPlaneChanged);

    buildPointOverlay();

    connect(calibrateBtn_, &QPushButton::toggled, this, &CalibrationPanel::onCalibrateToggled);
    connect(schemeCombo_,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CalibrationPanel::onSchemeChanged);
    connect(computeBtn_,   &QPushButton::clicked, this, &CalibrationPanel::onCompute);
    connect(testCheck_,    &QCheckBox::toggled,   this, &CalibrationPanel::onTest);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        disconnectVideoSignals();
        if (video_) {
            video_->setCalibrationMode(false);
            video_->clearCalibOriginPoint();
            video_->setCalibrationOverlay({});
            video_->setCalibExplicitLines({});
            video_->setCalibDistanceLabels({});
        }
        calibrateBtn_->setChecked(false);
        result_ = {};
        rectCorners_.clear();
        rectStep_ = 0;
        imagePoints_.clear();
        stagePoints_.clear();
        hasOrigin_ = false;
        placing_   = Placing::None;
        updateRectStepRows();
        updateRectActionBtn();
        updateRectOverlay();
        updateManualList();
        updateManualStatus();
        updateComputeButton();
        errorLabel_->setText("Calibration reset.");
        emit calibrationChanged(result_);
    });
}

CalibrationPanel::~CalibrationPanel() {
    disconnectVideoSignals();
    delete previewCal_;
}

// ── Activate / deactivate ─────────────────────────────────────────────────────

void CalibrationPanel::connectVideoSignals() {
    if (!video_) return;
    videoConnections_ << connect(video_, &VideoWidget::calibPointClicked,
                                  this, [this](QPointF pt) {
        switch (placing_) {
        case Placing::RectCorner:    rectOnCornerPlaced(pt);    break;
        case Placing::Rect3DCorner:  rect3DOnCornerPlaced(pt);  break;
        case Placing::ManualOrigin:  manualOnOriginPlaced(pt);  break;
        case Placing::ManualPoint:   manualOnPointPlaced(pt);   break;
        default: break;
        }
    });
    videoConnections_ << connect(video_, &VideoWidget::existingCalibPointMoved,
                                  this, [this](int index, QPointF imagePos) {
        if (scheme_ == Scheme::Rectangle) {
            int ci = (index == -1) ? 0 : (index + 1);
            if (ci < rectCorners_.size()) {
                rectCorners_[ci] = imagePos;
                updateRectOverlay();
            }
        } else if (scheme_ == Scheme::Rect3D) {
            int ci = (index == -1) ? 0 : (index + 1);
            if (ci < rect3DCorners_.size()) {
                rect3DCorners_[ci] = imagePos;
                updateRect3DOverlay();
            }
        } else {
            if (index == -1 && hasOrigin_) {
                originImagePoint_ = imagePos;
                video_->setCalibOriginPoint(originImagePoint_);
            } else if (index >= 0 && index < imagePoints_.size()) {
                imagePoints_[index] = imagePos;
            }
            updateManualOverlay();
        }
    });
    videoConnections_ << connect(video_, &VideoWidget::existingCalibPointClicked,
                                  this, [this](int index, QPoint widgetPos) {
        if (scheme_ == Scheme::Rectangle) {
            updateRectOverlay();
            Q_UNUSED(index);
        } else if (scheme_ == Scheme::Rect3D) {
            updateRect3DOverlay();
            Q_UNUSED(index); Q_UNUSED(widgetPos);
        } else {
            updateManualList();
            showPointOverlay(index, widgetPos);
        }
    });
    videoConnections_ << connect(video_, &VideoWidget::mousePosInFrame,
                                  this, &CalibrationPanel::onMousePosInFrame);
}

void CalibrationPanel::disconnectVideoSignals() {
    for (auto& c : videoConnections_) disconnect(c);
    videoConnections_.clear();
}

void CalibrationPanel::onCalibrateToggled(bool on) {
    active_ = on;
    emit calibrationActiveChanged(on);
    calibrateBtn_->setText(on ? "Stop Calibration" : "Start Calibration");
    if (!video_) return;
    video_->setShowCalibrationOverlay(on);
    if (on) {
        connectVideoSignals();
        if (scheme_ == Scheme::Rectangle)
            updateRectOverlay();
        else if (scheme_ == Scheme::Rect3D)
            updateRect3DOverlay();
        else
            updateManualOverlay();
    } else {
        disconnectVideoSignals();
        video_->setCalibrationMode(false);
        hidePointOverlay();
        placing_ = Placing::None;
        updateRectActionBtn();
        updateManualStatus();
    }
    computeBtn_->setEnabled(on && false); // recomputed by updateComputeButton
    updateComputeButton();
}

void CalibrationPanel::setCalibration(const CalibrationData& cal) {
    loadExisting(cal);
}

void CalibrationPanel::setViewSettings(bool showFloorGrid, float clickPlaneHeight,
                                        bool showClickPlane) {
    floorGridCheck_->setChecked(showFloorGrid);
    clickPlaneSpin_->setValue(clickPlaneHeight);
    showClickPlaneCheck_->setChecked(showClickPlane);
}

void CalibrationPanel::setPlaneHeight(float h) {
    QSignalBlocker b1(clickPlaneSlider_);
    QSignalBlocker b2(clickPlaneSpin_);
    clickPlaneSpin_->setValue(double(h));
    clickPlaneSlider_->setValue(qRound(h * 100.0f));
}

void CalibrationPanel::reset() {
    disconnectVideoSignals();
    if (video_) {
        video_->setCalibrationMode(false);
        video_->clearCalibOriginPoint();
        video_->setCalibrationOverlay({});
        video_->setCalibExplicitLines({});
        video_->setCalibDistanceLabels({});
    }
    calibrateBtn_->setChecked(false);
    result_ = {};
    rectCorners_.clear();
    rectStep_ = 0;
    rect3DCorners_.clear();
    rect3DStep_ = 0;
    imagePoints_.clear();
    stagePoints_.clear();
    hasOrigin_ = false;
    placing_   = Placing::None;
    updateRectStepRows();
    updateRectActionBtn();
    updateRectOverlay();
    updateRect3DStepRows();
    updateRect3DActionBtn();
    updateRect3DOverlay();
    updateManualList();
    updateManualStatus();
    updateComputeButton();
    update3DControls();
    errorLabel_->setText({});
    emit calibrationChanged(result_);
}

// ── Mode pages ────────────────────────────────────────────────────────────────

QWidget* CalibrationPanel::buildRectPage() {
    auto* page = new QWidget;
    auto* l    = new QVBoxLayout(page);
    l->setSpacing(6);
    l->setContentsMargins(0, 0, 0, 0);

    l->addWidget(new QLabel(
        "<small>Tape a rectangle on stage.<br>Click its 4 corners in order.</small>"));

    auto* dimFrame = new QFrame;
    dimFrame->setFrameShape(QFrame::StyledPanel);
    auto* dl = new QGridLayout(dimFrame);
    dl->setSpacing(4);
    dl->addWidget(new QLabel("Width X (m):"),  0, 0);
    rectWSpin_ = new QDoubleSpinBox;
    rectWSpin_->setRange(0.1, 9999); rectWSpin_->setValue(10.0);
    rectWSpin_->setDecimals(2); rectWSpin_->setSingleStep(0.5);
    dl->addWidget(rectWSpin_, 0, 1);
    dl->addWidget(new QLabel("Depth Y (m):"), 1, 0);
    rectHSpin_ = new QDoubleSpinBox;
    rectHSpin_->setRange(0.1, 9999); rectHSpin_->setValue(8.0);
    rectHSpin_->setDecimals(2); rectHSpin_->setSingleStep(0.5);
    dl->addWidget(rectHSpin_, 1, 1);
    l->addWidget(dimFrame);

    auto* stepsFrame = new QFrame;
    stepsFrame->setFrameShape(QFrame::StyledPanel);
    auto* sl = new QVBoxLayout(stepsFrame);
    sl->setSpacing(2);
    for (int i = 0; i < 4; i++) {
        rectStepRows_[i] = new QLabel;
        rectStepRows_[i]->setTextFormat(Qt::RichText);
        sl->addWidget(rectStepRows_[i]);
    }
    l->addWidget(stepsFrame);

    auto* btnRow = new QHBoxLayout;
    rectActionBtn_ = new QPushButton("Place Corners");
    rectResetBtn_  = new QPushButton("Reset");
    rectResetBtn_->setEnabled(false);
    btnRow->addWidget(rectActionBtn_);
    btnRow->addWidget(rectResetBtn_);
    l->addLayout(btnRow);

    connect(rectActionBtn_, &QPushButton::clicked, this, &CalibrationPanel::onRectAction);
    connect(rectResetBtn_,  &QPushButton::clicked, this, &CalibrationPanel::onRectReset);
    connect(rectWSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CalibrationPanel::onRectDimChanged);
    connect(rectHSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CalibrationPanel::onRectDimChanged);

    updateRectStepRows();
    return page;
}

QWidget* CalibrationPanel::buildManualPage() {
    auto* page = new QWidget;
    auto* l    = new QVBoxLayout(page);
    l->setSpacing(6);
    l->setContentsMargins(0, 0, 0, 0);

    l->addWidget(new QLabel(
        "<small>Click 4+ points, enter their stage (X, Y) coords.</small>"));

    manualStatusLabel_ = new QLabel;
    manualStatusLabel_->setWordWrap(true);
    manualStatusLabel_->setStyleSheet("color: palette(highlight);");
    l->addWidget(manualStatusLabel_);

    manualOriginBtn_ = new QPushButton("Set Origin");
    l->addWidget(manualOriginBtn_);

    manualList_ = new QListWidget;
    manualList_->setMaximumHeight(120);
    l->addWidget(manualList_);

    auto* ptRow = new QHBoxLayout;
    manualAddBtn_    = new QPushButton("+ Add");
    manualAddBtn_->setEnabled(false);
    manualRemoveBtn_ = new QPushButton("Remove");
    manualRemoveBtn_->setEnabled(false);
    ptRow->addWidget(manualAddBtn_);
    ptRow->addWidget(manualRemoveBtn_);
    l->addLayout(ptRow);

    connect(manualOriginBtn_, &QPushButton::clicked, this, &CalibrationPanel::onManualSetOrigin);
    connect(manualAddBtn_,    &QPushButton::clicked, this, &CalibrationPanel::onManualAddPoint);
    connect(manualRemoveBtn_, &QPushButton::clicked, this, &CalibrationPanel::onManualRemove);
    connect(manualList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        manualRemoveBtn_->setEnabled(true);
        int ptIdx = hasOrigin_ ? (row == 0 ? -1 : row - 1) : row;
        if (ptIdx >= 0 && ptIdx >= imagePoints_.size()) return;
        QPointF fp = (ptIdx == -1) ? originImagePoint_ : imagePoints_[ptIdx];
        if (video_)
            showPointOverlay(ptIdx, video_->mapFrameToWidget(fp).toPoint());
    });

    return page;
}

// ── 3D Rectangle mode ─────────────────────────────────────────────────────────

QWidget* CalibrationPanel::buildRect3DPage() {
    auto* page = new QWidget;
    auto* l    = new QVBoxLayout(page);
    l->setSpacing(6);
    l->setContentsMargins(0, 0, 0, 0);

    auto* desc3D = new QLabel(
        "<small>Tape a rectangle on the floor <b>and</b> place markers "
        "at the same corners at a known height. "
        "Click 4 floor corners, then the same 4 at marker height.</small>");
    desc3D->setWordWrap(true);
    l->addWidget(desc3D);

    auto* dimFrame = new QFrame;
    dimFrame->setFrameShape(QFrame::StyledPanel);
    auto* dl = new QGridLayout(dimFrame);
    dl->setSpacing(4);
    dl->addWidget(new QLabel("Width X (m):"),    0, 0);
    rect3DWSpin_ = new QDoubleSpinBox;
    rect3DWSpin_->setRange(0.1, 9999); rect3DWSpin_->setValue(10.0);
    rect3DWSpin_->setDecimals(2); rect3DWSpin_->setSingleStep(0.5);
    dl->addWidget(rect3DWSpin_, 0, 1);
    dl->addWidget(new QLabel("Depth Y (m):"),    1, 0);
    rect3DHSpin_ = new QDoubleSpinBox;
    rect3DHSpin_->setRange(0.1, 9999); rect3DHSpin_->setValue(8.0);
    rect3DHSpin_->setDecimals(2); rect3DHSpin_->setSingleStep(0.5);
    dl->addWidget(rect3DHSpin_, 1, 1);
    dl->addWidget(new QLabel("Marker height (m):"), 2, 0);
    rect3DMarkerSpin_ = new QDoubleSpinBox;
    rect3DMarkerSpin_->setRange(0.1, 20.0); rect3DMarkerSpin_->setValue(2.0);
    rect3DMarkerSpin_->setDecimals(2); rect3DMarkerSpin_->setSingleStep(0.1);
    rect3DMarkerSpin_->setToolTip("Real-world height of the elevated markers above the floor.");
    dl->addWidget(rect3DMarkerSpin_, 2, 1);
    l->addWidget(dimFrame);

    auto* stepsFrame = new QFrame;
    stepsFrame->setFrameShape(QFrame::StyledPanel);
    auto* sl = new QVBoxLayout(stepsFrame);
    sl->setSpacing(2);
    for (int i = 0; i < 8; i++) {
        rect3DStepRows_[i] = new QLabel;
        rect3DStepRows_[i]->setTextFormat(Qt::RichText);
        rect3DStepRows_[i]->setMinimumWidth(0);
        rect3DStepRows_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        sl->addWidget(rect3DStepRows_[i]);
    }
    l->addWidget(stepsFrame);

    auto* btnRow = new QHBoxLayout;
    rect3DActionBtn_ = new QPushButton("Place Floor Corners");
    rect3DResetBtn_  = new QPushButton("Reset");
    rect3DResetBtn_->setEnabled(false);
    btnRow->addWidget(rect3DActionBtn_);
    btnRow->addWidget(rect3DResetBtn_);
    l->addLayout(btnRow);

    connect(rect3DActionBtn_, &QPushButton::clicked, this, [this]() {
        if (!active_) return;
        if (placing_ == Placing::Rect3DCorner) {
            placing_ = Placing::None;
            if (video_) video_->setCalibrationMode(false);
        } else {
            if (rect3DStep_ >= 8) rect3DStep_ = 0;
            placing_ = Placing::Rect3DCorner;
            if (video_) video_->setCalibrationMode(true);
        }
        updateRect3DStepRows(); updateRect3DActionBtn(); updateComputeButton();
    });
    connect(rect3DResetBtn_, &QPushButton::clicked, this, [this]() {
        placing_ = Placing::None;
        if (video_) video_->setCalibrationMode(false);
        rect3DCorners_.clear(); rect3DStep_ = 0;
        updateRect3DOverlay(); updateRect3DStepRows();
        updateRect3DActionBtn(); rect3DResetBtn_->setEnabled(false);
        updateComputeButton();
    });
    connect(rect3DWSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() { updateRect3DStepRows(); updateRect3DOverlay(); });
    connect(rect3DHSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() { updateRect3DStepRows(); updateRect3DOverlay(); });

    updateRect3DStepRows();
    return page;
}

void CalibrationPanel::rect3DOnCornerPlaced(QPointF imagePos) {
    if (rect3DStep_ >= 8) return;
    if (rect3DCorners_.size() <= rect3DStep_) rect3DCorners_.resize(rect3DStep_ + 1);
    rect3DCorners_[rect3DStep_] = imagePos;
    rect3DStep_++;
    if (rect3DStep_ < 8) {
        placing_ = Placing::Rect3DCorner;
        if (video_) video_->setCalibrationMode(true);
    } else {
        placing_ = Placing::None;
        if (video_) video_->setCalibrationMode(false);
    }
    updateRect3DOverlay();
    updateRect3DStepRows();
    updateRect3DActionBtn();
    updateComputeButton();
    rect3DResetBtn_->setEnabled(true);
}

void CalibrationPanel::updateRect3DStepRows() {
    double w = rect3DWSpin_->value(), h = rect3DHSpin_->value();
    double hm = rect3DMarkerSpin_->value();
    const char* names[8] = {
        "Floor origin",  "Floor X-corner",  "Floor Z-corner",  "Floor diagonal",
        "Elev. origin",  "Elev. X-corner",  "Elev. Z-corner",  "Elev. diagonal"
    };
    QStringList coords = {
        "(0, 0, 0)",
        QString("(%1, 0, 0)").arg(w,0,'f',2),
        QString("(0, 0, %1)").arg(h,0,'f',2),
        QString("(%1, 0, %2)").arg(w,0,'f',2).arg(h,0,'f',2),
        QString("(0, %1, 0)").arg(hm,0,'f',2),
        QString("(%1, %2, 0)").arg(w,0,'f',2).arg(hm,0,'f',2),
        QString("(0, %1, %2)").arg(hm,0,'f',2).arg(h,0,'f',2),
        QString("(%1, %2, %3)").arg(w,0,'f',2).arg(hm,0,'f',2).arg(h,0,'f',2),
    };
    const char* syms[8] = {
        "\xe2\x91\xa0","\xe2\x91\xa1","\xe2\x91\xa2","\xe2\x91\xa3",
        "\xe2\x91\xa4","\xe2\x91\xa5","\xe2\x91\xa6","\xe2\x91\xa7"
    };
    for (int i = 0; i < 8; i++) {
        bool placed = (i < rect3DCorners_.size());
        bool active = (placing_ == Placing::Rect3DCorner && i == rect3DStep_);
        // Green for floor (0-3), blue for elevated (4-7)
        QString baseCol = (i < 4) ? "#33cc55" : "#5599ff";
        QString col  = placed ? baseCol : (active ? "#ffee66" : "#666666");
        QString mark = placed ? "\xe2\x9c\x93" : (active ? "\xe2\x86\x92" : "\xe2\x97\x8b");
        rect3DStepRows_[i]->setText(
            QString("<span style='color:%1'>%2 %3 %4 <i>%5</i></span>")
                .arg(col, syms[i], mark, names[i], coords[i]));
    }
}

void CalibrationPanel::updateRect3DActionBtn() {
    if (!active_) { rect3DActionBtn_->setText("Place Floor Corners"); return; }
    if (placing_ == Placing::Rect3DCorner)
        rect3DActionBtn_->setText("Cancel");
    else if (rect3DStep_ >= 8)
        rect3DActionBtn_->setText("Replace");
    else if (rect3DStep_ >= 4)
        rect3DActionBtn_->setText("Place Elevated Corners");
    else if (rect3DStep_ > 0)
        rect3DActionBtn_->setText("Continue");
    else
        rect3DActionBtn_->setText("Place Floor Corners");
}

void CalibrationPanel::updateRect3DOverlay() {
    if (!video_) return;
    video_->clearCalibOriginPoint();
    video_->setCalibrationOverlay({}, -1);
    video_->setCalibDistanceLabels({});
    video_->setCalibExplicitLines({});
    if (rect3DCorners_.isEmpty()) return;

    // Show floor corners the same as regular rect
    video_->setCalibOriginPoint(rect3DCorners_[0]);
    QList<QPointF> pts;
    for (int i = 1; i < qMin(rect3DCorners_.size(), 4); i++) pts << rect3DCorners_[i];
    // Also add elevated corners with different styling — just add them as extra overlay points
    for (int i = 4; i < rect3DCorners_.size(); i++) pts << rect3DCorners_[i];
    video_->setCalibrationOverlay(pts, -1);

    // Rectangle closing lines for floor
    QList<QPair<QPointF,QPointF>> lines;
    if (rect3DCorners_.size() >= 4) {
        lines << qMakePair(rect3DCorners_[1], rect3DCorners_[3])
              << qMakePair(rect3DCorners_[2], rect3DCorners_[3]);
    }
    video_->setCalibExplicitLines(lines);
}

// ── Point edit overlay (floats over main window, above the video) ─────────────

void CalibrationPanel::buildPointOverlay() {
    pointOverlay_ = new QFrame(mainWindow_);
    pointOverlay_->setFrameShape(QFrame::Box);
    pointOverlay_->setObjectName("pov");
    pointOverlay_->setStyleSheet(
        "QFrame#pov { background:rgba(20,22,32,230); border:1px solid #556; border-radius:6px; }"
        "QLabel { color:#dde; background:transparent; }"
        "QDoubleSpinBox { background:#2a2d3a; color:#eee; border:1px solid #667;"
        "  border-radius:3px; padding:2px 4px; }");

    auto* ol = new QGridLayout(pointOverlay_);
    ol->setSpacing(6);
    ol->setContentsMargins(10, 8, 10, 10);

    overlayTitle_ = new QLabel;
    overlayTitle_->setStyleSheet("color:#fff; font-weight:bold; background:transparent;");
    auto* closeBtn = new QPushButton("\xc3\x97");
    closeBtn->setFixedSize(22, 22); closeBtn->setFlat(true);
    closeBtn->setStyleSheet("color:#aaa; font-size:14px; background:transparent;");
    closeBtn->setCursor(Qt::ArrowCursor);
    ol->addWidget(overlayTitle_, 0, 0);
    ol->addWidget(closeBtn,      0, 1, Qt::AlignRight);

    auto mkRow = [&](const QString& lbl, QDoubleSpinBox*& spin, int row) {
        auto* w = new QWidget; w->setStyleSheet("background:transparent;");
        auto* h = new QHBoxLayout(w); h->setContentsMargins(0,0,0,0); h->setSpacing(6);
        h->addWidget(new QLabel(lbl));
        spin = new QDoubleSpinBox;
        spin->setRange(-9999, 9999); spin->setDecimals(3);
        spin->setSingleStep(0.5); spin->setFixedWidth(90);
        h->addWidget(spin);
        ol->addWidget(w, row, 0, 1, 2);
        return w;
    };

    overlaySXRow_ = mkRow("Stage X (m):", overlaySX_, 1);
    overlaySZRow_ = mkRow("Stage Y (m):", overlaySZ_, 2);

    pointOverlay_->adjustSize();
    pointOverlay_->hide();

    connect(closeBtn,   &QPushButton::clicked, this, &CalibrationPanel::hidePointOverlay);
    connect(overlaySX_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CalibrationPanel::onOverlayXChanged);
    connect(overlaySZ_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CalibrationPanel::onOverlayZChanged);
}

void CalibrationPanel::showPointOverlay(int index, QPoint videoWidgetPos) {
    if (!video_ || !mainWindow_) return;
    editingIndex_ = index;
    if (index == -1) {
        overlayTitle_->setText("Origin — stage (0.000, 0.000)");
        overlaySXRow_->hide(); overlaySZRow_->hide();
    } else {
        overlayTitle_->setText(QString("Point %1 — stage pos").arg(index + 1));
        overlaySXRow_->show(); overlaySZRow_->show();
        bool bx = overlaySX_->blockSignals(true);
        bool bz = overlaySZ_->blockSignals(true);
        overlaySX_->setValue(index < stagePoints_.size() ? stagePoints_[index].x() : 0.0);
        overlaySZ_->setValue(index < stagePoints_.size() ? stagePoints_[index].y() : 0.0);
        overlaySX_->blockSignals(bx);
        overlaySZ_->blockSignals(bz);
    }
    pointOverlay_->adjustSize();
    QPoint dp = video_->mapTo(mainWindow_, videoWidgetPos) + QPoint(20, -pointOverlay_->height() / 2);
    QRect  dr = mainWindow_->rect();
    QSize  os = pointOverlay_->size();
    pointOverlay_->move(
        qBound(dr.left() + 4, dp.x(), dr.right()  - os.width()  - 4),
        qBound(dr.top()  + 4, dp.y(), dr.bottom() - os.height() - 4));
    pointOverlay_->show();
    pointOverlay_->raise();
    {
        QSignalBlocker sb(manualList_);
        int row = (index == -1) ? 0 : (hasOrigin_ ? index + 1 : index);
        manualList_->setCurrentRow(row);
    }
    updateManualOverlay();
}

void CalibrationPanel::hidePointOverlay() {
    editingIndex_ = -2;
    if (pointOverlay_) pointOverlay_->hide();
    if (scheme_ == Scheme::Manual) updateManualOverlay();
}

// ── Rectangle mode ────────────────────────────────────────────────────────────

void CalibrationPanel::rectOnCornerPlaced(QPointF imagePos) {
    if (rectStep_ >= 4) return;
    if (rectCorners_.size() <= rectStep_) rectCorners_.resize(rectStep_ + 1);
    rectCorners_[rectStep_] = imagePos;
    rectStep_++;
    if (rectStep_ < 4) {
        placing_ = Placing::RectCorner;
        if (video_) video_->setCalibrationMode(true);
    } else {
        placing_ = Placing::None;
        if (video_) video_->setCalibrationMode(false);
    }
    updateRectOverlay();
    updateRectStepRows();
    updateRectActionBtn();
    updateComputeButton();
    rectResetBtn_->setEnabled(true);
}

void CalibrationPanel::updateRectOverlay() {
    if (!video_) return;
    int n = rectCorners_.size();
    video_->clearCalibOriginPoint();
    video_->setCalibrationOverlay({}, -1);
    video_->setCalibDistanceLabels({});
    video_->setCalibExplicitLines({});
    if (n == 0) return;
    double w = rectWSpin_->value();
    double h = rectHSpin_->value();
    video_->setCalibOriginPoint(rectCorners_[0]);
    QList<QPointF> pts;
    for (int i = 1; i < n; i++) pts << rectCorners_[i];
    video_->setCalibrationOverlay(pts, -1);
    QList<QString> labels;
    if (n >= 2) labels << QString("W=%1m").arg(w, 0, 'f', 1);
    if (n >= 3) labels << QString("H=%1m").arg(h, 0, 'f', 1);
    if (n >= 4) labels << QString();
    video_->setCalibDistanceLabels(labels);
    QList<QPair<QPointF,QPointF>> lines;
    if (n >= 4) {
        lines << qMakePair(rectCorners_[1], rectCorners_[3])
              << qMakePair(rectCorners_[2], rectCorners_[3]);
    }
    video_->setCalibExplicitLines(lines);
}

void CalibrationPanel::updateRectStepRows() {
    double w = rectWSpin_->value();
    double h = rectHSpin_->value();
    const char* names[4] = {"Origin", "X-corner", "Y-corner", "Diagonal"};
    const char* syms[4]  = {"\xe2\x91\xa0","\xe2\x91\xa1","\xe2\x91\xa2","\xe2\x91\xa3"};
    QStringList coords = {
        "(0, 0)",
        QString("(%1, 0)").arg(w, 0, 'f', 2),
        QString("(0, %1)").arg(h, 0, 'f', 2),
        QString("(%1, %2)").arg(w, 0, 'f', 2).arg(h, 0, 'f', 2)
    };
    for (int i = 0; i < 4; i++) {
        bool placed = (i < rectCorners_.size());
        bool active = (placing_ == Placing::RectCorner && i == rectStep_);
        QString col  = placed ? "#88ff88" : (active ? "#ffee66" : "#777777");
        QString mark = placed ? "\xe2\x9c\x93" : (active ? "\xe2\x86\x92" : "\xe2\x97\x8b");
        rectStepRows_[i]->setText(
            QString("<span style='color:%1'>%2 %3 %4 <i>%5</i></span>")
                .arg(col, syms[i], mark, names[i], coords[i]));
    }
}

void CalibrationPanel::updateRectActionBtn() {
    if (!active_) { rectActionBtn_->setText("Place Corners"); return; }
    if (placing_ == Placing::RectCorner)    rectActionBtn_->setText("Cancel");
    else if (rectStep_ >= 4)                rectActionBtn_->setText("Replace");
    else if (rectStep_ > 0)                 rectActionBtn_->setText("Continue");
    else                                    rectActionBtn_->setText("Place Corners");
}

// ── Manual mode ───────────────────────────────────────────────────────────────

void CalibrationPanel::manualOnOriginPlaced(QPointF imagePos) {
    hasOrigin_ = true; originImagePoint_ = imagePos;
    placing_   = Placing::None;
    if (video_) video_->setCalibOriginPoint(originImagePoint_);
    updateManualList(); updateManualOverlay();
    updateManualStatus(); updateComputeButton();
    manualOriginBtn_->setText("Move Origin");
    manualAddBtn_->setEnabled(true);
    if (video_)
        showPointOverlay(-1, video_->mapFrameToWidget(imagePos).toPoint());
}

void CalibrationPanel::manualOnPointPlaced(QPointF imagePos) {
    int newIdx = imagePoints_.size();
    imagePoints_ << imagePos;
    QPointF suggested(0.0, 0.0);
    if (video_) {
        QSize fsize = video_->frameSize();
        if (!fsize.isEmpty() && hasOrigin_) {
            double dx = (imagePos.x() - originImagePoint_.x()) / fsize.width()  * 12.0;
            double dz = (imagePos.y() - originImagePoint_.y()) / fsize.height() *  8.0;
            suggested = QPointF(qRound(dx * 2.0) / 2.0, qRound(dz * 2.0) / 2.0);
        }
    }
    stagePoints_ << suggested;
    placing_ = Placing::None;
    updateManualList(); updateManualOverlay();
    updateManualStatus(); updateComputeButton();
    if (video_)
        showPointOverlay(newIdx, video_->mapFrameToWidget(imagePos).toPoint());
    if (overlaySX_) { overlaySX_->setFocus(); overlaySX_->selectAll(); }
}

void CalibrationPanel::updateManualList() {
    QSignalBlocker sb(manualList_);
    manualList_->clear();
    if (hasOrigin_) {
        auto* item = new QListWidgetItem("\xe2\x8a\x95 Origin (0, 0)");
        item->setForeground(QColor(100, 220, 100));
        manualList_->addItem(item);
    }
    for (int i = 0; i < imagePoints_.size(); ++i)
        manualList_->addItem(QString("\xe2\x97\x8f P%1  X:%2  Y:%3")
            .arg(i + 1)
            .arg(stagePoints_[i].x(), 0, 'f', 2)
            .arg(stagePoints_[i].y(), 0, 'f', 2));
}

void CalibrationPanel::updateManualOverlay() {
    if (!video_) return;
    video_->setCalibExplicitLines({});
    if (hasOrigin_) video_->setCalibOriginPoint(originImagePoint_);
    else            video_->clearCalibOriginPoint();
    int highlight = -1;
    if (editingIndex_ == -1)     highlight = -2;
    else if (editingIndex_ >= 0) highlight = editingIndex_;
    video_->setCalibrationOverlay(imagePoints_, highlight);
    QList<QString> labels;
    for (auto& s : stagePoints_)
        labels << ((s.x() == 0.0 && s.y() == 0.0)
                   ? QString() : QString("X:%1 Y:%2").arg(s.x(),0,'f',2).arg(s.y(),0,'f',2));
    video_->setCalibDistanceLabels(labels);
}

void CalibrationPanel::updateManualStatus() {
    if (placing_ == Placing::ManualOrigin)
        manualStatusLabel_->setText("Click video to place origin…");
    else if (placing_ == Placing::ManualPoint)
        manualStatusLabel_->setText("Click video to place a point…");
    else if (!hasOrigin_)
        manualStatusLabel_->setText("Step 1: Set the origin (0, 0).");
    else if (imagePoints_.size() < 3)
        manualStatusLabel_->setText(
            QString("Add %1 more point(s).").arg(3 - imagePoints_.size()));
    else
        manualStatusLabel_->setText("Ready — click Compute.");
}

// ── Shared ────────────────────────────────────────────────────────────────────

void CalibrationPanel::updateComputeButton() {
    bool ok = false;
    if (scheme_ == Scheme::Rectangle)   ok = (rectCorners_.size() == 4);
    else if (scheme_ == Scheme::Rect3D) ok = (rect3DCorners_.size() == 8);
    else                                ok = (hasOrigin_ && imagePoints_.size() >= 3);
    computeBtn_->setEnabled(ok);
}

void CalibrationPanel::loadExisting(const CalibrationData& data) {
    if (data.imagePoints.size() < 4
        || data.stagePoints.size() < data.imagePoints.size()) {
        updateManualStatus();
        if (data.isValid())
            errorLabel_->setText("Calibration valid.");
        return;
    }
    auto& sp = data.stagePoints;
    double xs[4] = {sp[0].x(), sp[1].x(), sp[2].x(), sp[3].x()};
    double zs[4] = {sp[0].y(), sp[1].y(), sp[2].y(), sp[3].y()};
    std::sort(xs, xs + 4); std::sort(zs, zs + 4);
    bool isRect = (data.imagePoints.size() == 4)
        && std::abs(xs[0]) < 0.01 && std::abs(xs[1]) < 0.01
        && std::abs(xs[2] - xs[3]) < 0.01
        && std::abs(zs[0]) < 0.01 && std::abs(zs[1]) < 0.01
        && std::abs(zs[2] - zs[3]) < 0.01
        && xs[2] > 0.01 && zs[2] > 0.01;

    auto mapCorner = [&](int i, double W, double H) -> int {
        bool ox = std::abs(sp[i].x()) < 0.01, oz = std::abs(sp[i].y()) < 0.01;
        bool wx = std::abs(sp[i].x() - W) < 0.01;
        bool hz = std::abs(sp[i].y() - H) < 0.01;
        return (ox&&oz)?0 : (wx&&oz)?1 : (ox&&hz)?2 : 3;
    };

    if (data.is3DValid() && isRect && data.elevatedImagePoints.size() == 4) {
        double W = xs[2], H = zs[2];
        schemeCombo_->setCurrentIndex(0);
        rect3DWSpin_->setValue(W);
        rect3DHSpin_->setValue(H);
        rect3DMarkerSpin_->setValue(double(data.markerHeight));
        rect3DCorners_.resize(8);
        rect3DStep_ = 8;
        for (int i = 0; i < 4; i++) {
            int ci = mapCorner(i, W, H);
            rect3DCorners_[ci]     = data.imagePoints[i];
            rect3DCorners_[ci + 4] = data.elevatedImagePoints[i];
        }
        updateRect3DOverlay(); updateRect3DStepRows();
        updateRect3DActionBtn(); rect3DResetBtn_->setEnabled(true);
    } else if (isRect) {
        double W = xs[2], H = zs[2];
        schemeCombo_->setCurrentIndex(1);
        rectWSpin_->setValue(W); rectHSpin_->setValue(H);
        rectCorners_.resize(4); rectStep_ = 4;
        for (int i = 0; i < 4; i++)
            rectCorners_[mapCorner(i, W, H)] = data.imagePoints[i];
        updateRectOverlay(); updateRectStepRows();
        updateRectActionBtn(); rectResetBtn_->setEnabled(true);
    } else {
        schemeCombo_->setCurrentIndex(2);
        int originIdx = 0; double minD = std::numeric_limits<double>::max();
        for (int i = 0; i < sp.size(); ++i) {
            double d = std::sqrt(sp[i].x()*sp[i].x() + sp[i].y()*sp[i].y());
            if (d < minD) { minD = d; originIdx = i; }
        }
        hasOrigin_ = true; originImagePoint_ = data.imagePoints[originIdx];
        for (int i = 0; i < data.imagePoints.size(); ++i) {
            if (i == originIdx) continue;
            imagePoints_ << data.imagePoints[i];
            stagePoints_ << data.stagePoints[i];
        }
        manualOriginBtn_->setText("Move Origin");
        manualAddBtn_->setEnabled(true);
        updateManualList(); updateManualOverlay(); updateManualStatus();
    }
    if (data.isValid()) {
        previewCal_->fromList(data.homography);
        if (data.is3DValid())
            previewCal_->projectionFromList(data.projectionMatrix);
        result_ = data;
        errorLabel_->setText("Calibration valid.");
    }
    updateComputeButton();
    update3DControls();
}

void CalibrationPanel::update3DControls() {
    has3DControls_->setEnabled(result_.is3DValid());
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CalibrationPanel::onSchemeChanged(int idx) {
    placing_ = Placing::None;
    if (video_) video_->setCalibrationMode(false);
    hidePointOverlay();
    scheme_ = (idx == 0) ? Scheme::Rect3D : (idx == 1) ? Scheme::Rectangle : Scheme::Manual;
    schemeStack_->setCurrentIndex(idx);
    if (scheme_ == Scheme::Rectangle) {
        updateRectOverlay(); updateRectStepRows(); updateRectActionBtn();
    } else if (scheme_ == Scheme::Rect3D) {
        updateRect3DOverlay(); updateRect3DStepRows(); updateRect3DActionBtn();
    } else {
        updateManualOverlay(); updateManualStatus();
    }
    updateComputeButton();
}

void CalibrationPanel::onRectAction() {
    if (!active_) return;
    if (placing_ == Placing::RectCorner) {
        placing_ = Placing::None;
        if (video_) video_->setCalibrationMode(false);
    } else {
        if (rectStep_ >= 4) rectStep_ = 0;
        placing_ = Placing::RectCorner;
        if (video_) video_->setCalibrationMode(true);
    }
    updateRectStepRows(); updateRectActionBtn(); updateComputeButton();
}

void CalibrationPanel::onRectReset() {
    placing_ = Placing::None;
    if (video_) video_->setCalibrationMode(false);
    rectCorners_.clear(); rectStep_ = 0;
    updateRectOverlay(); updateRectStepRows();
    updateRectActionBtn(); rectResetBtn_->setEnabled(false);
    updateComputeButton();
}

void CalibrationPanel::onRectDimChanged() {
    updateRectStepRows(); updateRectOverlay();
}

void CalibrationPanel::onManualSetOrigin() {
    if (!active_) return;
    placing_ = Placing::ManualOrigin;
    if (video_) video_->setCalibrationMode(true);
    updateManualStatus(); hidePointOverlay();
}

void CalibrationPanel::onManualAddPoint() {
    if (!active_) return;
    placing_ = Placing::ManualPoint;
    if (video_) video_->setCalibrationMode(true);
    updateManualStatus(); hidePointOverlay();
}

void CalibrationPanel::onManualRemove() {
    int row = manualList_->currentRow();
    if (row < 0) return;
    hidePointOverlay();
    if (hasOrigin_) {
        if (row == 0) {
            hasOrigin_ = false;
            if (video_) video_->clearCalibOriginPoint();
            manualOriginBtn_->setText("Set Origin");
            manualAddBtn_->setEnabled(false);
        } else {
            int idx = row - 1;
            if (idx < imagePoints_.size()) {
                imagePoints_.removeAt(idx);
                stagePoints_.removeAt(idx);
            }
        }
    } else {
        if (row < imagePoints_.size()) {
            imagePoints_.removeAt(row);
            stagePoints_.removeAt(row);
        }
    }
    updateManualList(); updateManualOverlay();
    updateManualStatus(); updateComputeButton();
}

void CalibrationPanel::onCompute() {
    if (scheme_ == Scheme::Rect3D) {
        if (rect3DCorners_.size() < 8) return;
        QList<QPointF> floorPts = rect3DCorners_.mid(0, 4);
        QList<QPointF> elevPts  = rect3DCorners_.mid(4, 4);
        double w = rect3DWSpin_->value(), h = rect3DHSpin_->value();
        float  mh = float(rect3DMarkerSpin_->value());
        QList<QPointF> stageXZ = {{0,0},{w,0},{0,h},{w,h}};
        double err = previewCal_->compute3D(floorPts, elevPts, stageXZ, mh);
        if (err < 0) {
            errorLabel_->setText("3D failed — check corner placement.");
            return;
        }
        errorLabel_->setText(QString("3D error: %1 px").arg(err, 0, 'f', 2));
        result_.imagePoints          = floorPts;
        result_.stagePoints          = stageXZ;
        result_.homography           = previewCal_->toList();
        result_.elevatedImagePoints  = elevPts;
        result_.markerHeight         = mh;
        result_.projectionMatrix     = previewCal_->projectionToList();
        if (video_) video_->setCalibration(previewCal_);
        emit calibrationChanged(result_);
        calibrateBtn_->setChecked(false);
        update3DControls();
        return;
    }

    QList<QPointF> allImg, allStg;
    if (scheme_ == Scheme::Rectangle) {
        if (rectCorners_.size() < 4) return;
        allImg = rectCorners_;
        double w = rectWSpin_->value(), h = rectHSpin_->value();
        allStg = {{0,0},{w,0},{0,h},{w,h}};
    } else {
        if (!hasOrigin_ || imagePoints_.size() < 3) return;
        allImg << originImagePoint_ << imagePoints_;
        allStg << QPointF(0,0) << stagePoints_;
    }
    double err = previewCal_->compute(allImg, allStg);
    if (err < 0) {
        errorLabel_->setText("Failed — ensure points not collinear.");
        return;
    }
    errorLabel_->setText(QString("Error: %1 px").arg(err, 0, 'f', 2));
    result_.imagePoints      = allImg;
    result_.stagePoints      = allStg;
    result_.homography       = previewCal_->toList();
    result_.elevatedImagePoints.clear();
    result_.markerHeight     = 0.0f;
    result_.projectionMatrix.clear();
    if (video_) video_->setCalibration(previewCal_);
    emit calibrationChanged(result_);
    calibrateBtn_->setChecked(false);
    update3DControls();
}

void CalibrationPanel::onTest(bool checked) {
    testMode_ = checked;
}

void CalibrationPanel::onMousePosInFrame(QPointF imagePos) {
    if (!testMode_ || !previewCal_->isValid()) return;
    QPointF s = previewCal_->pixelToStage(imagePos);
    errorLabel_->setText(QString("X=%1m  Y=%2m")
        .arg(s.x(), 0, 'f', 3).arg(s.y(), 0, 'f', 3));
}

void CalibrationPanel::onOverlayXChanged(double val) {
    if (editingIndex_ < 0 || editingIndex_ >= stagePoints_.size()) return;
    stagePoints_[editingIndex_].setX(val);
    updateManualList(); updateManualOverlay();
}

void CalibrationPanel::onOverlayZChanged(double val) {
    if (editingIndex_ < 0 || editingIndex_ >= stagePoints_.size()) return;
    stagePoints_[editingIndex_].setY(val);
    updateManualList(); updateManualOverlay();
}
