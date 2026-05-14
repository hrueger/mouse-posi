#include "NetworkSettingsPanel.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QNetworkInterface>

NetworkSettingsPanel::NetworkSettingsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // PSN mode
    auto* modeGroup = new QGroupBox("PSN Output Mode");
    auto* modeLayout = new QVBoxLayout(modeGroup);
    multicastRadio_ = new QRadioButton("Multicast (recommended)");
    unicastRadio_   = new QRadioButton("Unicast");
    broadcastRadio_ = new QRadioButton("Broadcast");
    multicastRadio_->setChecked(true);
    modeLayout->addWidget(multicastRadio_);
    modeLayout->addWidget(unicastRadio_);
    modeLayout->addWidget(broadcastRadio_);
    layout->addWidget(modeGroup);

    // IP / port fields
    auto* fl = new QFormLayout;
    multicastIpEdit_ = new QLineEdit("236.10.10.10");
    unicastIpEdit_   = new QLineEdit;
    broadcastIpEdit_ = new QLineEdit("255.255.255.255");
    portSpin_        = new QSpinBox;
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(56565);
    fl->addRow("Multicast IP:", multicastIpEdit_);
    fl->addRow("Unicast IP:",   unicastIpEdit_);
    fl->addRow("Broadcast IP:", broadcastIpEdit_);
    fl->addRow("Port:",         portSpin_);
    layout->addLayout(fl);

    // Interface selectors
    auto* ifGroup = new QGroupBox("Network Interfaces");
    auto* ifLayout = new QFormLayout(ifGroup);
    psnIfaceCombo_     = new QComboBox;
    sessionIfaceCombo_ = new QComboBox;
    ifLayout->addRow("PSN Interface:",     psnIfaceCombo_);
    ifLayout->addRow("Session Interface:", sessionIfaceCombo_);
    layout->addWidget(ifGroup);

    populateInterfaces();

    auto emitChange = [this]() { emit configChanged(config()); };
    connect(multicastRadio_,  &QRadioButton::toggled, this, emitChange);
    connect(unicastRadio_,    &QRadioButton::toggled, this, emitChange);
    connect(broadcastRadio_,  &QRadioButton::toggled, this, emitChange);
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
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack))
        {
            psnIfaceCombo_->addItem(iface.humanReadableName(), iface.name());
            sessionIfaceCombo_->addItem(iface.humanReadableName(), iface.name());
        }
    }
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
