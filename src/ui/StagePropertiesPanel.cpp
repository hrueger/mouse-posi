#include "StagePropertiesPanel.h"
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QtMath>
#include <cmath>

static QPolygonF buildRectPolyProps(QPointF center, float w, float d, float rotDeg)
{
    const float hw = w / 2.0f;
    const float hd = d / 2.0f;
    const float c  = std::cos(qDegreesToRadians(rotDeg));
    const float s  = std::sin(qDegreesToRadians(rotDeg));
    QPolygonF p;
    for (auto [lx, lz] : QList<QPair<float,float>>{{-hw,-hd},{hw,-hd},{hw,hd},{-hw,hd}})
        p << QPointF(center.x() + lx*c - lz*s, center.y() + lx*s + lz*c);
    return p;
}

StagePropertiesPanel::StagePropertiesPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    noSelectionLabel_ = new QLabel("No object selected.");
    noSelectionLabel_->setAlignment(Qt::AlignCenter);
    noSelectionLabel_->setStyleSheet("color: palette(placeholderText);");
    layout->addWidget(noSelectionLabel_);

    propsGroup_ = new QWidget;
    propsForm_  = new QFormLayout(propsGroup_);
    propsForm_->setRowWrapPolicy(QFormLayout::DontWrapRows);
    propsForm_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    propsForm_->setLabelAlignment(Qt::AlignRight);

    auto makeSpin = [](double min, double max, double step = 0.05, int dec = 2) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };

    nameLine_     = new QLineEdit;
    heightSpin_   = makeSpin(0.0, 20.0); heightSpin_->setSuffix(" m");
    xSpin_        = makeSpin(-100.0, 100.0, 0.1); xSpin_->setSuffix(" m");
    zSpin_        = makeSpin(-100.0, 100.0, 0.1); zSpin_->setSuffix(" m");
    widthSpin_    = makeSpin(0.01, 100.0, 0.1);   widthSpin_->setSuffix(" m");
    depthSpin_    = makeSpin(0.01, 100.0, 0.1);   depthSpin_->setSuffix(" m");
    rotSpin_      = makeSpin(-360.0, 360.0, 1.0, 1); rotSpin_->setSuffix(" °");
    fovSpin_      = makeSpin(1.0, 179.0, 1.0, 1);  fovSpin_->setSuffix(" °");
    fovNoteLabel_ = new QLabel("Position from 3D calibration.\nFOV only affects the view cone.");
    fovNoteLabel_->setStyleSheet("color: palette(placeholderText); font-style: italic;");
    fovNoteLabel_->setWordWrap(true);
    camPosLabel_  = new QLabel("–");
    camPosLabel_->setWordWrap(false);

    propsForm_->addRow("Name:", nameLine_);
    heightFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Height:", heightSpin_);
    xFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("X:", xSpin_);
    zFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Z:", zSpin_);
    widthFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Width:", widthSpin_);
    depthFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Depth:", depthSpin_);
    rotFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Rotation:", rotSpin_);
    fovFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Camera FOV:", fovSpin_);
    fovNoteFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("", fovNoteLabel_);
    camPosFormRow_ = propsForm_->rowCount();
    propsForm_->addRow("Position:", camPosLabel_);

    propsGroup_->hide();
    layout->addWidget(propsGroup_);
    layout->addStretch();

    for (auto* s : {heightSpin_, xSpin_, zSpin_, widthSpin_, depthSpin_, rotSpin_, fovSpin_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StagePropertiesPanel::onPropertiesChanged);
    connect(nameLine_, &QLineEdit::editingFinished, this, &StagePropertiesPanel::onPropertiesChanged);
}

void StagePropertiesPanel::setAllObjects(const QList<StageObject>& all)
{
    objects_ = all;
    updatePropertiesForm(selectedId_);
}

void StagePropertiesPanel::setSelectedObject(int id)
{
    selectedId_ = id;
    updatePropertiesForm(id);
}

void StagePropertiesPanel::setHas3DCalibration(bool has3D)
{
    if (has3DCalib_ == has3D) return;
    has3DCalib_ = has3D;
    if (selectedId_ == -1)
        updatePropertiesForm(-1);
}

void StagePropertiesPanel::onPropertiesChanged()
{
    if (updatingForm_ || selectedId_ == -999) return;
    applyPropertiesToSelected();
}

void StagePropertiesPanel::updatePropertiesForm(int id)
{
    if (id == -999) {
        propsGroup_->hide();
        noSelectionLabel_->show();
        return;
    }

    const StageObject* found = nullptr;
    for (const auto& o : objects_) if (o.id == id) { found = &o; break; }
    if (!found) {
        propsGroup_->hide();
        noSelectionLabel_->show();
        return;
    }

    noSelectionLabel_->hide();
    propsGroup_->show();

    const bool isCamera    = (id == -1);
    const bool isCalibRect = (id == -2);
    const bool isOutline   = found->isStageOutline;
    const bool isPlatform  = !isCamera && !isCalibRect && !isOutline;

    updatingForm_ = true;

    nameLine_->setText(found->name);
    nameLine_->setReadOnly(id < 0);

    propsForm_->setRowVisible(heightFormRow_,  isPlatform);
    propsForm_->setRowVisible(xFormRow_,       !isCamera && !isCalibRect);
    propsForm_->setRowVisible(zFormRow_,       !isCamera && !isCalibRect);
    propsForm_->setRowVisible(widthFormRow_,   isPlatform || isOutline);
    propsForm_->setRowVisible(depthFormRow_,   isPlatform || isOutline);
    propsForm_->setRowVisible(rotFormRow_,     isPlatform || isOutline);
    propsForm_->setRowVisible(fovFormRow_,     isCamera);
    propsForm_->setRowVisible(fovNoteFormRow_, false);
    propsForm_->setRowVisible(camPosFormRow_,  isCamera);

    heightSpin_->setValue(double(found->height));
    xSpin_->setValue(found->center.x());
    zSpin_->setValue(found->center.y());
    widthSpin_->setEnabled(found->isRect || isOutline);
    depthSpin_->setEnabled(found->isRect || isOutline);
    rotSpin_->setEnabled(found->isRect || isOutline);
    widthSpin_->setValue(double(found->width));
    depthSpin_->setValue(double(found->depth));
    rotSpin_->setValue(double(found->rotation));

    if (isCamera) {
        fovSpin_->setValue(double(found->fovDeg));
        fovSpin_->setEnabled(!has3DCalib_);
        propsForm_->setRowVisible(fovNoteFormRow_, has3DCalib_);

        if (found->center.isNull() && found->height == 0.0f) {
            camPosLabel_->setText("Pending calibration");
        } else {
            camPosLabel_->setText(
                QString("X:%1  Y:%2  Z:%3 m")
                .arg(found->center.x(), 0, 'f', 1)
                .arg(found->height,     0, 'f', 1)
                .arg(found->center.y(), 0, 'f', 1));
        }
    }

    if (isCalibRect) {
        for (auto* w : QList<QWidget*>{heightSpin_, xSpin_, zSpin_,
                                       widthSpin_, depthSpin_, rotSpin_, fovSpin_})
            w->setEnabled(false);
    }

    updatingForm_ = false;
}

void StagePropertiesPanel::applyPropertiesToSelected()
{
    for (auto& obj : objects_) {
        if (obj.id != selectedId_) continue;

        if (obj.id == -1) {
            obj.fovDeg = float(fovSpin_->value());
            emit objectEdited(obj);
            return;
        }
        if (obj.id < 0) return;

        const QString newName = nameLine_->text().trimmed();
        if (!newName.isEmpty()) obj.name = newName;

        if (!obj.isStageOutline)
            obj.height = float(heightSpin_->value());

        obj.center = QPointF(xSpin_->value(), zSpin_->value());

        if (obj.isRect || obj.isStageOutline) {
            obj.width    = float(widthSpin_->value());
            obj.depth    = float(depthSpin_->value());
            obj.rotation = float(rotSpin_->value());
            obj.polygon  = buildRectPolyProps(obj.center, obj.width, obj.depth, obj.rotation);
        } else {
            QPointF delta = obj.center - obj.polygon.boundingRect().center();
            if (!delta.isNull()) {
                QPolygonF moved;
                for (const QPointF& v : obj.polygon) moved << (v + delta);
                obj.polygon = moved;
            }
        }

        emit objectEdited(obj);
        return;
    }
}
