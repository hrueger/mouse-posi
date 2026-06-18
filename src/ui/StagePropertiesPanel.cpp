#include "StagePropertiesPanel.h"
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <QMenu>
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
    propsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    propsForm_->setLabelAlignment(Qt::AlignRight);

    auto makeSpin = [](double min, double max, double step = 0.05, int dec = 2) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };

    nameLine_     = new QLineEdit;
    nameLine_->setMinimumWidth(0);
    nameLine_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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

    // ── MVR group properties ───────────────────────────────────────────────
    mvrPropsGroup_ = new QWidget;
    auto* mvrForm  = new QFormLayout(mvrPropsGroup_);
    mvrForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    mvrForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mvrForm->setLabelAlignment(Qt::AlignRight);

    auto makeMvrSpin = [](double min, double max, double step = 0.1, int dec = 2) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max); s->setSingleStep(step); s->setDecimals(dec);
        return s;
    };

    mvrNameEdit_    = new QLineEdit;
    mvrNameEdit_->setMinimumWidth(0);
    mvrNameEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    mvrOffsetXSpin_ = makeMvrSpin(-100.0, 100.0); mvrOffsetXSpin_->setSuffix(" m");
    mvrOffsetYSpin_ = makeMvrSpin(-20.0,  20.0);  mvrOffsetYSpin_->setSuffix(" m");
    mvrOffsetZSpin_ = makeMvrSpin(-100.0, 100.0); mvrOffsetZSpin_->setSuffix(" m");
    mvrRotSpin_     = makeMvrSpin(-360.0, 360.0, 1.0, 1); mvrRotSpin_->setSuffix(" °");

    mvrForm->addRow("Name:",      mvrNameEdit_);
    mvrForm->addRow("Offset X:",  mvrOffsetXSpin_);
    mvrForm->addRow("Offset Y:",  mvrOffsetYSpin_);
    mvrForm->addRow("Offset Z:",  mvrOffsetZSpin_);
    mvrForm->addRow("Rotation:",  mvrRotSpin_);

    mvrPropsGroup_->hide();
    layout->addWidget(mvrPropsGroup_);

    // ── MVR fixture properties (tracker link) ─────────────────────────────────
    fixtureGroup_ = new QWidget;
    auto* fixForm = new QFormLayout(fixtureGroup_);
    fixForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    fixForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fixForm->setLabelAlignment(Qt::AlignRight);

    fixtureNameLabel_ = new QLabel;
    fixtureDmxLabel_  = new QLabel;
    fixtureGdtfLabel_ = new QLabel;
    fixtureGdtfLabel_->setWordWrap(true);
    trackerLinkCombo_ = new QComboBox;

    fixForm->addRow("Fixture:", fixtureNameLabel_);
    fixForm->addRow("DMX:", fixtureDmxLabel_);
    fixForm->addRow("GDTF:", fixtureGdtfLabel_);
    fixForm->addRow("Tracker:", trackerLinkCombo_);

    connect(trackerLinkCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (updatingForm_) return;
        if (fixtureImportIdx_ < 0 || fixtureLayerIdx_ < 0 || fixtureObjIdx_ < 0) return;
        const int trackerLink = (idx == 0) ? -1 : trackerLinkCombo_->itemData(idx).toInt();
        emit mvrFixtureTrackerLinkChanged(fixtureImportIdx_, fixtureLayerIdx_, fixtureObjIdx_, trackerLink);
    });

    fixtureGroup_->hide();
    layout->addWidget(fixtureGroup_);

    // ── PSN origin properties ─────────────────────────────────────────────
    psnOriginGroup_ = new QWidget;
    auto* psnForm   = new QFormLayout(psnOriginGroup_);
    psnForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    psnForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    psnForm->setLabelAlignment(Qt::AlignRight);

    auto makePsnSpin = [](double min, double max, double step = 0.1, int dec = 2) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max); s->setSingleStep(step); s->setDecimals(dec);
        return s;
    };
    psnXSpin_   = makePsnSpin(-100.0, 100.0);  psnXSpin_->setSuffix(" m");
    psnYSpin_   = makePsnSpin(-20.0,  20.0);   psnYSpin_->setSuffix(" m");
    psnZSpin_   = makePsnSpin(-100.0, 100.0);  psnZSpin_->setSuffix(" m");
    psnRotSpin_ = makePsnSpin(-180.0, 180.0, 1.0, 1); psnRotSpin_->setSuffix(" °");

    psnForm->addRow("PSN Offset X:", psnXSpin_);
    psnForm->addRow("PSN Offset Y:", psnYSpin_);
    psnForm->addRow("PSN Offset Z:", psnZSpin_);
    psnForm->addRow("PSN Rotation:", psnRotSpin_);

    snapToMvrBtn_ = new QPushButton(QStringLiteral("Move to MVR origin…"));
    snapToMvrBtn_->setEnabled(false);
    psnForm->addRow("", snapToMvrBtn_);

    psnOriginGroup_->hide();
    layout->addWidget(psnOriginGroup_);

    for (auto* s : {psnXSpin_, psnYSpin_, psnZSpin_, psnRotSpin_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StagePropertiesPanel::onPsnOriginChanged);

    connect(snapToMvrBtn_, &QPushButton::clicked, this, [this] {
        QList<int> enabledIdx;
        for (int i = 0; i < mvrImports_.size(); ++i)
            if (mvrImports_[i].enabled) enabledIdx << i;
        if (enabledIdx.isEmpty()) return;

        auto applyImport = [this](int i) {
            const MvrImport& imp = mvrImports_[i];
            const QVector3D off(imp.offsetX, imp.offsetY, imp.offsetZ);
            setPsnOrigin(off, imp.rotDeg);
            emit psnOriginEdited(off, imp.rotDeg);
        };

        if (enabledIdx.size() == 1) {
            applyImport(enabledIdx.first());
        } else {
            QMenu menu(this);
            for (int i : enabledIdx) {
                const QString label = mvrImports_[i].name.isEmpty()
                                      ? QStringLiteral("MVR Import %1").arg(i + 1)
                                      : mvrImports_[i].name;
                menu.addAction(label, this, [applyImport, i]{ applyImport(i); });
            }
            menu.exec(snapToMvrBtn_->mapToGlobal(snapToMvrBtn_->rect().bottomLeft()));
        }
    });

    // ── Stage origin (read-only) ───────────────────────────────────────────
    stageOriginGroup_ = new QWidget;
    auto* soLayout    = new QVBoxLayout(stageOriginGroup_);
    soLayout->setContentsMargins(4, 4, 4, 4);
    auto* soLabel = new QLabel("Stage calibration origin\n(0, 0, 0) — read-only reference");
    soLabel->setAlignment(Qt::AlignCenter);
    soLabel->setStyleSheet("color: palette(placeholderText); font-style: italic;");
    soLayout->addWidget(soLabel);
    stageOriginGroup_->hide();
    layout->addWidget(stageOriginGroup_);

    layout->addStretch();

    for (auto* s : {heightSpin_, xSpin_, zSpin_, widthSpin_, depthSpin_, rotSpin_, fovSpin_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StagePropertiesPanel::onPropertiesChanged);
    connect(nameLine_, &QLineEdit::editingFinished, this, &StagePropertiesPanel::onPropertiesChanged);

    for (auto* s : {mvrOffsetXSpin_, mvrOffsetYSpin_, mvrOffsetZSpin_, mvrRotSpin_})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StagePropertiesPanel::onMvrPropertiesChanged);
    connect(mvrNameEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (updatingForm_) return;
        const QString newName = mvrNameEdit_->text().trimmed();
        if (!newName.isEmpty()) {
            mvrImport_.name = newName;
            emit mvrImportEdited(mvrImportIndex_, mvrImport_);
        }
    });
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

void StagePropertiesPanel::setTrackers(const QList<TrackerConfig>& trackers) {
    trackers_ = trackers;
}

void StagePropertiesPanel::setMvrFixture(int importIdx, int layerIdx, int objIdx,
                                          const MvrImportData& importData,
                                          const QList<TrackerConfig>& trackers) {
    trackers_ = trackers;
    fixtureImportIdx_ = importIdx;
    fixtureLayerIdx_  = layerIdx;
    fixtureObjIdx_    = objIdx;
    selectedId_ = -999;

    if (layerIdx < 0 || layerIdx >= importData.layers.size()) return;
    const auto& layer = importData.layers[layerIdx];
    if (objIdx < 0 || objIdx >= layer.objects.size()) return;
    const auto& obj = layer.objects[objIdx];

    updatingForm_ = true;
    fixtureNameLabel_->setText(obj.name.isEmpty() ? QStringLiteral("(unnamed)") : obj.name);
    fixtureDmxLabel_->setText(QString("%1.%2").arg(obj.universe).arg(obj.dmxAddress));
    fixtureGdtfLabel_->setText(obj.gdtfSpec.isEmpty() ? QStringLiteral("—") : obj.gdtfSpec);

    trackerLinkCombo_->clear();
    trackerLinkCombo_->addItem(QStringLiteral("(none)"), -1);
    for (const auto& t : trackers)
        trackerLinkCombo_->addItem(t.name, t.id);

    // Select the current link
    int comboIdx = 0;
    for (int i = 1; i < trackerLinkCombo_->count(); ++i) {
        if (trackerLinkCombo_->itemData(i).toInt() == obj.trackerLink) {
            comboIdx = i; break;
        }
    }
    trackerLinkCombo_->setCurrentIndex(comboIdx);
    updatingForm_ = false;

    propsGroup_->hide();
    mvrPropsGroup_->hide();
    psnOriginGroup_->hide();
    stageOriginGroup_->hide();
    noSelectionLabel_->hide();
    fixtureGroup_->show();
}

void StagePropertiesPanel::setPsnOrigin(QVector3D offset, float rotDeg)
{
    updatingForm_ = true;
    psnXSpin_->setValue(double(offset.x()));
    psnYSpin_->setValue(double(offset.y()));
    psnZSpin_->setValue(double(offset.z()));
    psnRotSpin_->setValue(double(rotDeg));
    updatingForm_ = false;
}

void StagePropertiesPanel::setMvrImports(const QList<MvrImport>& imports)
{
    mvrImports_ = imports;
    const bool hasEnabled = std::any_of(imports.begin(), imports.end(),
                                        [](const MvrImport& m){ return m.enabled; });
    snapToMvrBtn_->setEnabled(hasEnabled);
}

void StagePropertiesPanel::onPsnOriginChanged()
{
    if (updatingForm_) return;
    const QVector3D off(float(psnXSpin_->value()),
                        float(psnYSpin_->value()),
                        float(psnZSpin_->value()));
    emit psnOriginEdited(off, float(psnRotSpin_->value()));
}

void StagePropertiesPanel::setMvrImport(int index, const MvrImport& import)
{
    mvrImportIndex_ = index;
    mvrImport_  = import;
    selectedId_ = -999;

    propsGroup_->hide();
    noSelectionLabel_->hide();

    updatingForm_ = true;
    mvrNameEdit_->setText(import.name.isEmpty() ? QStringLiteral("MVR Import") : import.name);
    mvrOffsetXSpin_->setValue(double(import.offsetX));
    mvrOffsetYSpin_->setValue(double(import.offsetY));
    mvrOffsetZSpin_->setValue(double(import.offsetZ));
    mvrRotSpin_->setValue(double(import.rotDeg));
    updatingForm_ = false;

    propsGroup_->hide();
    fixtureGroup_->hide();
    psnOriginGroup_->hide();
    stageOriginGroup_->hide();
    noSelectionLabel_->hide();
    mvrPropsGroup_->show();
}

void StagePropertiesPanel::onPropertiesChanged()
{
    if (updatingForm_ || selectedId_ == -999) return;
    applyPropertiesToSelected();
}

void StagePropertiesPanel::onMvrPropertiesChanged()
{
    if (updatingForm_) return;
    mvrImport_.name    = mvrNameEdit_->text();
    mvrImport_.offsetX = float(mvrOffsetXSpin_->value());
    mvrImport_.offsetY = float(mvrOffsetYSpin_->value());
    mvrImport_.offsetZ = float(mvrOffsetZSpin_->value());
    mvrImport_.rotDeg  = float(mvrRotSpin_->value());
    emit mvrImportEdited(mvrImportIndex_, mvrImport_);
}

void StagePropertiesPanel::updatePropertiesForm(int id)
{
    mvrPropsGroup_->hide();
    fixtureGroup_->hide();
    psnOriginGroup_->hide();
    stageOriginGroup_->hide();

    if (id == -999) {
        propsGroup_->hide();
        noSelectionLabel_->show();
        return;
    }

    // PSN origin — editable spinboxes
    if (id == -21) {
        propsGroup_->hide();
        noSelectionLabel_->hide();
        psnOriginGroup_->show();
        return;
    }

    // Stage origin — read-only
    if (id == -20) {
        propsGroup_->hide();
        noSelectionLabel_->hide();
        stageOriginGroup_->show();
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
