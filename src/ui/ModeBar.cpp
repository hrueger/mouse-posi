#include "ModeBar.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QButtonGroup>

ModeBar::ModeBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(36);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(2);

    auto* label = new QLabel("Mode:");
    label->setContentsMargins(0, 0, 8, 0);
    lay->addWidget(label);

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    auto makeBtn = [&](const QString& text, const QString& tip) {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setToolTip(tip);
        btn->setMinimumWidth(130);
        group->addButton(btn);
        lay->addWidget(btn);
        return btn;
    };

    btnPsn_ = makeBtn("3D Stage → PSN",
        "3D stage mode: click sets tracker position, output via PSN to lighting console");
    btnCam2D_ = makeBtn("Camera 2D → DMX",
        "Camera mode: camera co-located with followspot, clicks map to pan/tilt via 2D calibration");
    btnDmx3D_ = makeBtn("3D Stage → DMX",
        "3D stage mode: OnPoint computes pan/tilt angles from geometry, outputs directly via DMX");

    lay->addStretch();

    connect(btnPsn_, &QPushButton::clicked, this, [this]() {
        if (updating_) return;
        mode_ = OperatingMode::Stage3DPSN;
        emit modeChanged(mode_);
    });
    connect(btnCam2D_, &QPushButton::clicked, this, [this]() {
        if (updating_) return;
        mode_ = OperatingMode::Camera2D;
        emit modeChanged(mode_);
    });
    connect(btnDmx3D_, &QPushButton::clicked, this, [this]() {
        if (updating_) return;
        mode_ = OperatingMode::Stage3DDMX;
        emit modeChanged(mode_);
    });

    updateButtons();
}

void ModeBar::setMode(OperatingMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    updateButtons();
}

void ModeBar::updateButtons() {
    updating_ = true;
    btnPsn_->setChecked(mode_ == OperatingMode::Stage3DPSN);
    btnCam2D_->setChecked(mode_ == OperatingMode::Camera2D);
    btnDmx3D_->setChecked(mode_ == OperatingMode::Stage3DDMX);
    updating_ = false;
}
