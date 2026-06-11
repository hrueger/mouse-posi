#include "ModeSelectionWidget.h"
#include <QButtonGroup>
#include <QRadioButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QGroupBox>

struct ModeInfo { OperatingMode mode; QString name; QString desc; };
static const QList<ModeInfo> kModes = {
    { OperatingMode::Stage3DPSN,
      "3D Stage → PSN",
      "Camera is NOT co-located with the followspot.\n"
      "OnPoint renders a 3D stage model; the operator clicks to set performer position.\n"
      "Position is sent as PosiStageNet (PSN) to a lighting console (MA3, Eos, …)." },
    { OperatingMode::Camera2D,
      "Camera 2D → DMX",
      "Camera is mounted directly next to the followspot.\n"
      "Clicks map to pan/tilt via a 2D pixel-to-DMX calibration (homography).\n"
      "Pan/tilt values are sent directly as DMX (sACN or ArtNet) — no console needed." },
    { OperatingMode::Stage3DDMX,
      "3D Stage → DMX",
      "Camera is NOT co-located. OnPoint renders a 3D stage model.\n"
      "When the operator clicks, OnPoint computes pan/tilt angles from geometry\n"
      "using GDTF fixture data and sends them directly via DMX." },
};

ModeSelectionWidget::ModeSelectionWidget(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(8);
    lay->setContentsMargins(0, 0, 0, 0);
    group_ = new QButtonGroup(this);

    for (const auto& mi : kModes) {
        auto* card = new QGroupBox;
        auto* cardLay = new QVBoxLayout(card);

        auto* radio = new QRadioButton(mi.name);
        radio->setProperty("modeValue", int(mi.mode));
        group_->addButton(radio);
        cardLay->addWidget(radio);

        auto* desc = new QLabel(mi.desc);
        desc->setWordWrap(true);
        desc->setContentsMargins(20, 0, 0, 0);
        QFont f = desc->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        desc->setFont(f);
        cardLay->addWidget(desc);

        lay->addWidget(card);

        connect(radio, &QRadioButton::toggled, this, [this, radio](bool checked) {
            if (checked && emitChanges_)
                emit modeChanged(OperatingMode(radio->property("modeValue").toInt()));
        });
    }

    lay->addStretch();

    // Default: first mode selected
    if (!group_->buttons().isEmpty())
        qobject_cast<QRadioButton*>(group_->buttons().first())->setChecked(true);
}

void ModeSelectionWidget::setMode(OperatingMode mode) {
    emitChanges_ = false;
    for (auto* btn : group_->buttons()) {
        if (OperatingMode(btn->property("modeValue").toInt()) == mode) {
            qobject_cast<QRadioButton*>(btn)->setChecked(true);
            break;
        }
    }
    emitChanges_ = true;
}

OperatingMode ModeSelectionWidget::mode() const {
    for (auto* btn : group_->buttons())
        if (btn->isChecked())
            return OperatingMode(btn->property("modeValue").toInt());
    return OperatingMode::Stage3DPSN;
}
