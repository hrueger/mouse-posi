#include "CameraControl.h"

#include "MarshallCv370Controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
void registerBuiltInCamera(CameraControlPanel* panel) {
    registerMarshallCv370Camera(panel);
}
}

CameraControlPanel::CameraControlPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);

    enabledCheck_ = new QCheckBox(QStringLiteral("Camera control"));
    enabledCheck_->setToolTip(QStringLiteral(
        "Enable control for supported cameras. Add another camera by registering a camera panel type."));
    layout->addWidget(enabledCheck_);

    auto* typeRow = new QHBoxLayout;
    auto* typeLabel = new QLabel(QStringLiteral("Type:"));
    typeCombo_ = new QComboBox;
    typeCombo_->setMinimumContentsLength(0);
    typeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    typeCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(typeCombo_, 1);
    layout->addLayout(typeRow);

    stack_ = new QStackedWidget;
    emptyLabel_ = new QLabel(QStringLiteral("No camera control types registered."));
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setStyleSheet(QStringLiteral("color: palette(placeholderText); font-size: 11px;"));
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_);

    registerBuiltInCameras();
    updateControls();

    connect(enabledCheck_, &QCheckBox::toggled, this, [this]() {
        updateControls();
        if (!setting_) emitConfigChanged();
    });
    connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        const QString type = typeCombo_->itemData(idx).toString();
        activateType(type);
        if (!setting_) emitConfigChanged();
    });
}

void CameraControlPanel::registerBuiltInCameras() {
    registerBuiltInCamera(this);
}

void CameraControlPanel::registerCamera(const QString& type, const QString& displayName, CameraFactory factory) {
    if (type.trimmed().isEmpty() || cameras_.contains(type))
        return;

    RegisteredCamera camera;
    camera.displayName = displayName;
    camera.factory = std::move(factory);
    cameras_.insert(type, camera);
    typeCombo_->addItem(displayName, type);

    if (currentType_.isEmpty()) {
        setting_ = true;
        typeCombo_->setCurrentIndex(typeCombo_->count() - 1);
        activateType(type);
        setting_ = false;
    }
    updateControls();
}

CameraControlConfig CameraControlPanel::config() const {
    CameraControlConfig cfg;
    cfg.type = currentType_;
    cfg.enabled = enabledCheck_->isChecked();
    cfg.config = perTypeConfig_;
    if (!currentType_.isEmpty())
        cfg.config[currentType_] = configForType(currentType_);
    return cfg;
}

void CameraControlPanel::setConfig(const CameraControlConfig& config) {
    setting_ = true;
    perTypeConfig_ = config.config;
    enabledCheck_->setChecked(config.enabled);

    QString type = config.type;
    if (type.isEmpty() && typeCombo_->count() > 0)
        type = typeCombo_->itemData(0).toString();

    const int idx = typeCombo_->findData(type);
    if (idx >= 0)
        typeCombo_->setCurrentIndex(idx);
    activateType(idx >= 0 ? type : QString{});
    setting_ = false;
    updateControls();
}

void CameraControlPanel::setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress) {
    currentNdiSource_ = sourceName;
    currentNdiEndpoint_ = ndiUrlAddress;
    if (auto it = cameras_.find(currentType_); it != cameras_.end() && it->panel)
        it->panel->setNdiSourceEndpoint(sourceName, ndiUrlAddress);
}

void CameraControlPanel::emitConfigChanged() {
    emit configChanged(config());
}

void CameraControlPanel::updateControls() {
    const bool hasTypes = typeCombo_->count() > 0;
    enabledCheck_->setEnabled(hasTypes);
    typeCombo_->setVisible(hasTypes);
    stack_->setVisible(hasTypes || emptyLabel_);
    stack_->setEnabled(enabledCheck_->isChecked());
}

void CameraControlPanel::activateType(const QString& type) {
    if (!currentType_.isEmpty())
        perTypeConfig_[currentType_] = configForType(currentType_);

    currentType_ = type;
    auto it = cameras_.find(type);
    if (it == cameras_.end()) {
        stack_->setCurrentWidget(emptyLabel_);
        return;
    }

    if (!it->panel) {
        it->panel = it->factory(stack_);
        stack_->addWidget(it->panel);
        connect(it->panel, &CameraSettingsPanel::configChanged, this, [this]() {
            if (!currentType_.isEmpty())
                perTypeConfig_[currentType_] = configForType(currentType_);
            if (!setting_) emitConfigChanged();
        });
    }

    it->panel->setConfigJson(perTypeConfig_.value(type).toObject());
    it->panel->setNdiSourceEndpoint(currentNdiSource_, currentNdiEndpoint_);
    stack_->setCurrentWidget(it->panel);
}

QJsonObject CameraControlPanel::configForType(const QString& type) const {
    auto it = cameras_.constFind(type);
    if (it == cameras_.constEnd() || !it->panel)
        return perTypeConfig_.value(type).toObject();
    return it->panel->configJson();
}
