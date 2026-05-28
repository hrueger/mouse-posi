#include "NetworkSettingsPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QButtonGroup>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QNetworkInterface>
#include <QAbstractSocket>

NetworkSettingsPanel::NetworkSettingsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // ── Session section ───────────────────────────────────────────────────
    auto* sessionHeader = new QLabel("Session");
    sessionHeader->setStyleSheet("font-weight: bold;");
    layout->addWidget(sessionHeader);

    auto* sessionForm = new QFormLayout;
    sessionForm->setContentsMargins(0, 2, 0, 0);
    sessionForm->setSpacing(4);
    sessionIfaceCombo_ = new QComboBox;
    sessionIfaceCombo_->setMinimumContentsLength(0);
    sessionIfaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sessionForm->addRow("Interface:", sessionIfaceCombo_);
    layout->addLayout(sessionForm);

    auto makeSep = []() {
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        return sep;
    };
    layout->addWidget(makeSep());

    // ── PSN section ───────────────────────────────────────────────────────
    auto* psnHeader = new QLabel("PSN");
    psnHeader->setStyleSheet("font-weight: bold;");
    layout->addWidget(psnHeader);

    multicastRadio_ = new QRadioButton("Multicast");
    unicastRadio_   = new QRadioButton("Unicast");
    broadcastRadio_ = new QRadioButton("Broadcast");
    multicastRadio_->setChecked(true);
    psnModeGroup_ = new QButtonGroup(this);
    psnModeGroup_->addButton(multicastRadio_);
    psnModeGroup_->addButton(unicastRadio_);
    psnModeGroup_->addButton(broadcastRadio_);
    auto* psnRadioRow = new QHBoxLayout;
    psnRadioRow->setContentsMargins(0, 0, 0, 0);
    psnRadioRow->setSpacing(4);
    psnRadioRow->addWidget(multicastRadio_);
    psnRadioRow->addWidget(unicastRadio_);
    psnRadioRow->addWidget(broadcastRadio_);
    layout->addLayout(psnRadioRow);

    formLayout_ = new QFormLayout;
    formLayout_->setContentsMargins(0, 4, 0, 0);
    formLayout_->setSpacing(4);
    multicastIpEdit_ = new QLineEdit("236.10.10.10");
    unicastIpEdit_   = new QLineEdit;
    broadcastIpEdit_ = new QLineEdit("255.255.255.255");
    for (auto* e : {multicastIpEdit_, unicastIpEdit_, broadcastIpEdit_})
        e->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    formLayout_->addRow("Multicast IP:", multicastIpEdit_);  // row 0
    formLayout_->addRow("Unicast IP:",   unicastIpEdit_);    // row 1
    formLayout_->addRow("Broadcast IP:", broadcastIpEdit_);  // row 2

    psnIfaceCombo_ = new QComboBox;
    psnIfaceCombo_->setMinimumContentsLength(0);
    psnIfaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    formLayout_->addRow("Interface:", psnIfaceCombo_);

    portSpin_ = new QSpinBox;
    portSpin_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(56565);
    formLayout_->addRow("Port:", portSpin_);
    layout->addLayout(formLayout_);

    layout->addWidget(makeSep());

    // ── sACN input section ────────────────────────────────────────────────
    auto* sacnHeader = new QLabel("sACN Input");
    sacnHeader->setStyleSheet("font-weight: bold;");
    layout->addWidget(sacnHeader);

    sacnEnableCheck_ = new QCheckBox("Enable sACN input");
    layout->addWidget(sacnEnableCheck_);

    sacnModeMulticast_ = new QRadioButton("Multicast (E1.31)");
    sacnModeUnicast_   = new QRadioButton("Unicast");
    sacnModeMulticast_->setChecked(true);
    sacnModeGroup_ = new QButtonGroup(this);
    sacnModeGroup_->addButton(sacnModeMulticast_);
    sacnModeGroup_->addButton(sacnModeUnicast_);
    auto* sacnRadioRow = new QHBoxLayout;
    sacnRadioRow->setContentsMargins(0, 0, 0, 0);
    sacnRadioRow->setSpacing(4);
    sacnRadioRow->addWidget(sacnModeMulticast_);
    sacnRadioRow->addWidget(sacnModeUnicast_);
    layout->addLayout(sacnRadioRow);

    auto* sacnForm = new QFormLayout;
    sacnForm->setContentsMargins(0, 2, 0, 0);
    sacnForm->setSpacing(4);

    sacnIfaceCombo_ = new QComboBox;
    sacnIfaceCombo_->setMinimumContentsLength(0);
    sacnIfaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    sacnUniverseSpin_ = new QSpinBox;
    sacnUniverseSpin_->setRange(1, 63999);
    sacnUniverseSpin_->setValue(1);

    sacnAddressSpin_ = new QSpinBox;
    sacnAddressSpin_->setRange(1, 512);
    sacnAddressSpin_->setValue(1);

    auto mkHeightSpin = [](double dflt) {
        auto* s = new QDoubleSpinBox;
        s->setRange(0.0, 99.0);
        s->setDecimals(2);
        s->setSingleStep(0.5);
        s->setSuffix(" m");
        s->setValue(dflt);
        return s;
    };
    sacnMinSpin_ = mkHeightSpin(0.0);
    sacnMaxSpin_ = mkHeightSpin(10.0);

    sacnForm->addRow("Interface:", sacnIfaceCombo_);
    sacnForm->addRow("Universe:",  sacnUniverseSpin_);
    sacnForm->addRow("Address:",   sacnAddressSpin_);
    sacnForm->addRow("Min height:", sacnMinSpin_);
    sacnForm->addRow("Max height:", sacnMaxSpin_);
    layout->addLayout(sacnForm);
    layout->addStretch();

    // Populate sACN interface combo alongside PSN/session combos
    auto populateSacnIface = [this]() {
        sacnIfaceCombo_->clear();
        sacnIfaceCombo_->addItem("(Default)", QString());
        for (const auto& iface : QNetworkInterface::allInterfaces()) {
            if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
            QString ipv4;
            for (const auto& entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    ipv4 = entry.ip().toString(); break;
                }
            }
            if (ipv4.isEmpty()) continue;
            sacnIfaceCombo_->addItem(iface.humanReadableName() + " — " + ipv4, iface.name());
        }
    };
    populateSacnIface();

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
    connect(sacnEnableCheck_,   &QCheckBox::toggled,     this, emitChange);
    connect(sacnModeMulticast_, &QRadioButton::toggled,  this, emitChange);
    connect(sacnModeUnicast_,   &QRadioButton::toggled,  this, emitChange);
    connect(sacnIfaceCombo_,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, emitChange);
    connect(sacnUniverseSpin_, QOverload<int>::of(&QSpinBox::valueChanged),         this, emitChange);
    connect(sacnAddressSpin_,  QOverload<int>::of(&QSpinBox::valueChanged),         this, emitChange);
    connect(sacnMinSpin_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitChange);
    connect(sacnMaxSpin_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitChange);
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
    // Block all child-widget signals while restoring state to suppress spurious
    // configChanged emissions that would start/stop the receivers with partial config.
    const QSignalBlocker b1(multicastRadio_),   b2(unicastRadio_),   b3(broadcastRadio_);
    const QSignalBlocker b4(multicastIpEdit_),  b5(unicastIpEdit_),  b6(broadcastIpEdit_);
    const QSignalBlocker b7(portSpin_);
    const QSignalBlocker b8(psnIfaceCombo_),    b9(sessionIfaceCombo_);
    const QSignalBlocker b10(sacnEnableCheck_);
    const QSignalBlocker b11(sacnModeMulticast_), b12(sacnModeUnicast_);
    const QSignalBlocker b13(sacnIfaceCombo_);
    const QSignalBlocker b14(sacnUniverseSpin_), b15(sacnAddressSpin_);
    const QSignalBlocker b16(sacnMinSpin_),      b17(sacnMaxSpin_);

    multicastRadio_->setChecked(cfg.psnMode == PsnMode::Multicast);
    unicastRadio_->setChecked(cfg.psnMode   == PsnMode::Unicast);
    broadcastRadio_->setChecked(cfg.psnMode == PsnMode::Broadcast);
    multicastIpEdit_->setText(cfg.multicastIp);
    unicastIpEdit_->setText(cfg.unicastIp);
    broadcastIpEdit_->setText(cfg.broadcastIp);
    portSpin_->setValue(cfg.port);

    auto selectIface = [](QComboBox* cb, const QString& name) {
        int idx = cb->findData(name);
        cb->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    selectIface(psnIfaceCombo_, cfg.psnInterface);
    selectIface(sessionIfaceCombo_, cfg.sessionInterface);

    sacnEnableCheck_->setChecked(cfg.sacnInput.enabled);
    sacnModeMulticast_->setChecked(cfg.sacnInput.mode == SacnMode::Multicast);
    sacnModeUnicast_->setChecked(cfg.sacnInput.mode   == SacnMode::Unicast);
    selectIface(sacnIfaceCombo_, cfg.sacnInput.iface);
    sacnUniverseSpin_->setValue(cfg.sacnInput.universe);
    sacnAddressSpin_->setValue(cfg.sacnInput.address);
    sacnMinSpin_->setValue(double(cfg.sacnInput.minHeight));
    sacnMaxSpin_->setValue(double(cfg.sacnInput.maxHeight));

    updateIpFieldVisibility();
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

    cfg.sacnInput.enabled   = sacnEnableCheck_->isChecked();
    cfg.sacnInput.mode      = sacnModeUnicast_->isChecked() ? SacnMode::Unicast : SacnMode::Multicast;
    cfg.sacnInput.iface     = sacnIfaceCombo_->currentData().toString();
    cfg.sacnInput.universe  = static_cast<quint16>(sacnUniverseSpin_->value());
    cfg.sacnInput.address   = static_cast<quint16>(sacnAddressSpin_->value());
    cfg.sacnInput.minHeight = float(sacnMinSpin_->value());
    cfg.sacnInput.maxHeight = float(sacnMaxSpin_->value());
    return cfg;
}
