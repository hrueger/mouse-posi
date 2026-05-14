#include "NetworkSettingsPanel.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QNetworkInterface>
#include <QAbstractSocket>

NetworkSettingsPanel::NetworkSettingsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    multicastRadio_ = new QRadioButton("Multicast (recommended)");
    unicastRadio_   = new QRadioButton("Unicast");
    broadcastRadio_ = new QRadioButton("Broadcast");
    multicastRadio_->setChecked(true);
    for (auto* r : {multicastRadio_, unicastRadio_, broadcastRadio_})
        r->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(multicastRadio_);
    layout->addWidget(unicastRadio_);
    layout->addWidget(broadcastRadio_);

    formLayout_ = new QFormLayout;
    formLayout_->setContentsMargins(0, 4, 0, 0);
    formLayout_->setSpacing(4);
    multicastIpEdit_ = new QLineEdit("236.10.10.10");
    unicastIpEdit_   = new QLineEdit;
    broadcastIpEdit_ = new QLineEdit("255.255.255.255");
    for (auto* e : {multicastIpEdit_, unicastIpEdit_, broadcastIpEdit_})
        e->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    portSpin_        = new QSpinBox;
    portSpin_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(56565);
    formLayout_->addRow("Multicast IP:", multicastIpEdit_);  // row 0
    formLayout_->addRow("Unicast IP:",   unicastIpEdit_);    // row 1
    formLayout_->addRow("Broadcast IP:", broadcastIpEdit_);  // row 2
    formLayout_->addRow("Port:",         portSpin_);

    psnIfaceCombo_     = new QComboBox;
    psnIfaceCombo_->setMinimumContentsLength(0);
    psnIfaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sessionIfaceCombo_ = new QComboBox;
    sessionIfaceCombo_->setMinimumContentsLength(0);
    sessionIfaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    formLayout_->addRow("PSN iface:",     psnIfaceCombo_);
    formLayout_->addRow("Session iface:", sessionIfaceCombo_);
    layout->addLayout(formLayout_);

    populateInterfaces();
    updateIpFieldVisibility();

    auto emitChange = [this]() { emit configChanged(config()); };
    connect(multicastRadio_,  &QRadioButton::toggled, this, [this, emitChange]{ updateIpFieldVisibility(); emitChange(); });
    connect(unicastRadio_,    &QRadioButton::toggled, this, [this, emitChange]{ updateIpFieldVisibility(); emitChange(); });
    connect(broadcastRadio_,  &QRadioButton::toggled, this, [this, emitChange]{ updateIpFieldVisibility(); emitChange(); });
    connect(multicastIpEdit_, &QLineEdit::editingFinished, this, emitChange);
    connect(unicastIpEdit_,   &QLineEdit::editingFinished, this, emitChange);
    connect(broadcastIpEdit_, &QLineEdit::editingFinished, this, emitChange);
    connect(portSpin_,        QOverload<int>::of(&QSpinBox::valueChanged), this, emitChange);
    connect(psnIfaceCombo_,     QOverload<int>::of(&QComboBox::currentIndexChanged), this, emitChange);
    connect(sessionIfaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, emitChange);
}

void NetworkSettingsPanel::populateInterfaces() {
    auto addDefaultItem = [](QComboBox* cb) {
        cb->addItem("(Default)", QString());
    };
    psnIfaceCombo_->clear();
    sessionIfaceCombo_->clear();
    addDefaultItem(psnIfaceCombo_);
    addDefaultItem(sessionIfaceCombo_);
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp))
            continue;

        QString ipv4;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4 = entry.ip().toString();
                break;
            }
        }
        if (ipv4.isEmpty())
            continue;

        QString label = iface.humanReadableName() + " — " + ipv4;
        psnIfaceCombo_->addItem(label, iface.name());
        sessionIfaceCombo_->addItem(label, iface.name());
    }
}

void NetworkSettingsPanel::updateIpFieldVisibility() {
    formLayout_->setRowVisible(0, multicastRadio_->isChecked());
    formLayout_->setRowVisible(1, unicastRadio_->isChecked());
    formLayout_->setRowVisible(2, broadcastRadio_->isChecked());
}

void NetworkSettingsPanel::setConfig(const NetworkConfig& cfg) {
    multicastRadio_->setChecked(cfg.psnMode == PsnMode::Multicast);
    unicastRadio_->setChecked(cfg.psnMode   == PsnMode::Unicast);
    broadcastRadio_->setChecked(cfg.psnMode == PsnMode::Broadcast);
    multicastIpEdit_->setText(cfg.multicastIp);
    unicastIpEdit_->setText(cfg.unicastIp);
    broadcastIpEdit_->setText(cfg.broadcastIp);
    portSpin_->setValue(cfg.port);

    auto selectIface = [](QComboBox* cb, const QString& name) {
        int idx = cb->findData(name);
        if (idx >= 0) cb->setCurrentIndex(idx);
    };
    selectIface(psnIfaceCombo_, cfg.psnInterface);
    selectIface(sessionIfaceCombo_, cfg.sessionInterface);
}

NetworkConfig NetworkSettingsPanel::config() const {
    NetworkConfig cfg;
    if (unicastRadio_->isChecked())        cfg.psnMode = PsnMode::Unicast;
    else if (broadcastRadio_->isChecked()) cfg.psnMode = PsnMode::Broadcast;
    else                                   cfg.psnMode = PsnMode::Multicast;
    cfg.multicastIp      = multicastIpEdit_->text();
    cfg.unicastIp        = unicastIpEdit_->text();
    cfg.broadcastIp      = broadcastIpEdit_->text();
    cfg.port             = static_cast<quint16>(portSpin_->value());
    cfg.psnInterface     = psnIfaceCombo_->currentData().toString();
    cfg.sessionInterface = sessionIfaceCombo_->currentData().toString();
    return cfg;
}
