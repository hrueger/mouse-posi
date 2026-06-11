#include "InputAdaptersPanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QNetworkInterface>

static const QStringList kTargets = {
    "clickPlaneHeight", "dimmer", "zoom", "iris", "focus"
};

// ── MappingRowWidget ──────────────────────────────────────────────────────────

MappingRowWidget::MappingRowWidget(const InputAdapterMapping& m, bool isMidi, QWidget* parent)
    : QWidget(parent), isMidi_(isMidi)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(4);

    targetCombo_ = new QComboBox;
    targetCombo_->addItems(kTargets);
    targetCombo_->setCurrentText(m.target.isEmpty() ? kTargets[0] : m.target);
    lay->addWidget(targetCombo_);

    if (isMidi)
        buildMidiRow(m, lay);
    else
        buildDmxRow(m, lay);

    minSpin_ = new QDoubleSpinBox;
    minSpin_->setRange(-9999, 9999);
    minSpin_->setDecimals(2);
    minSpin_->setValue(m.minValue);
    minSpin_->setPrefix("Min: ");
    minSpin_->setFixedWidth(100);
    lay->addWidget(minSpin_);

    maxSpin_ = new QDoubleSpinBox;
    maxSpin_->setRange(-9999, 9999);
    maxSpin_->setDecimals(2);
    maxSpin_->setValue(m.maxValue);
    maxSpin_->setPrefix("Max: ");
    maxSpin_->setFixedWidth(100);
    lay->addWidget(maxSpin_);

    auto* removeBtn = new QPushButton("✕");
    removeBtn->setFixedWidth(28);
    removeBtn->setToolTip("Remove this mapping");
    lay->addWidget(removeBtn);

    connect(targetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MappingRowWidget::changed);
    connect(spin1_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(spin2_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(minSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(maxSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(removeBtn, &QPushButton::clicked, this, &MappingRowWidget::removeRequested);
}

void MappingRowWidget::buildDmxRow(const InputAdapterMapping& m, QHBoxLayout* lay) {
    spin1_ = new QSpinBox;
    spin1_->setRange(1, 32768);
    spin1_->setValue(m.universe > 0 ? m.universe : 1);
    spin1_->setPrefix("Uni: ");
    spin1_->setFixedWidth(90);
    lay->addWidget(spin1_);

    spin2_ = new QSpinBox;
    spin2_->setRange(1, 512);
    spin2_->setValue(m.channel > 0 ? m.channel : 1);
    spin2_->setPrefix("Ch: ");
    spin2_->setFixedWidth(80);
    lay->addWidget(spin2_);
}

void MappingRowWidget::buildMidiRow(const InputAdapterMapping& m, QHBoxLayout* lay) {
    spin1_ = new QSpinBox;
    spin1_->setRange(0, 127);
    spin1_->setValue(m.midiCC >= 0 ? m.midiCC : 0);
    spin1_->setPrefix("CC: ");
    spin1_->setFixedWidth(80);
    lay->addWidget(spin1_);

    spin2_ = new QSpinBox;
    spin2_->setRange(0, 16);
    spin2_->setValue(m.midiChannel);
    spin2_->setPrefix("Ch: ");
    spin2_->setSpecialValueText("All");
    spin2_->setFixedWidth(80);
    lay->addWidget(spin2_);
}

InputAdapterMapping MappingRowWidget::mapping() const {
    InputAdapterMapping m;
    m.target   = targetCombo_->currentText();
    m.minValue = float(minSpin_->value());
    m.maxValue = float(maxSpin_->value());
    if (isMidi_) {
        m.midiCC      = spin1_->value();
        m.midiChannel = spin2_->value();
    } else {
        m.universe = quint16(spin1_->value());
        m.channel  = quint16(spin2_->value());
    }
    return m;
}

// ── InputAdaptersPanel ────────────────────────────────────────────────────────

InputAdaptersPanel::InputAdaptersPanel(QWidget* parent) : QWidget(parent) {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* mainLay  = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->addWidget(splitter);

    // ── Left: adapter list + add/remove ──────────────────────────────────────
    auto* leftWidget = new QWidget;
    auto* leftLay    = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(4, 4, 4, 4);
    leftLay->setSpacing(4);

    adapterList_ = new QListWidget;
    adapterList_->setMaximumWidth(160);
    leftLay->addWidget(adapterList_);

    auto* btnRow = new QHBoxLayout;
    addAdapterBtn_    = new QPushButton("+");
    removeAdapterBtn_ = new QPushButton("−");
    addAdapterBtn_->setFixedWidth(32);
    removeAdapterBtn_->setFixedWidth(32);
    removeAdapterBtn_->setEnabled(false);
    btnRow->addWidget(addAdapterBtn_);
    btnRow->addWidget(removeAdapterBtn_);
    btnRow->addStretch();
    leftLay->addLayout(btnRow);
    splitter->addWidget(leftWidget);

    // ── Right: adapter detail ─────────────────────────────────────────────────
    detailWidget_ = new QWidget;
    auto* detailLay = new QVBoxLayout(detailWidget_);
    detailLay->setContentsMargins(8, 4, 4, 4);
    detailLay->setSpacing(6);

    // Type + enabled row
    auto* topRow = new QHBoxLayout;
    typeCombo_ = new QComboBox;
    typeCombo_->addItem("sACN / ArtNet");
    typeCombo_->addItem("MIDI");
    enabledCheck_ = new QCheckBox("Enabled");
    enabledCheck_->setChecked(true);
    topRow->addWidget(new QLabel("Type:"));
    topRow->addWidget(typeCombo_);
    topRow->addSpacing(16);
    topRow->addWidget(enabledCheck_);
    topRow->addStretch();
    detailLay->addLayout(topRow);

    // ── DMX connection fields ─────────────────────────────────────────────────
    dmxFields_ = new QGroupBox("DMX Connection");
    auto* dmxForm = new QFormLayout(dmxFields_);

    protocolCombo_ = new QComboBox;
    protocolCombo_->addItem("sACN (E1.31)", int(DmxProtocol::SACN));
    protocolCombo_->addItem("ArtNet",       int(DmxProtocol::ArtNet));
    dmxForm->addRow("Protocol:", protocolCombo_);

    netModeCombo_ = new QComboBox;
    netModeCombo_->addItem("Multicast", int(DmxNetworkMode::Multicast));
    netModeCombo_->addItem("Unicast",   int(DmxNetworkMode::Unicast));
    netModeCombo_->addItem("Broadcast", int(DmxNetworkMode::Broadcast));
    dmxForm->addRow("Network Mode:", netModeCombo_);

    ifaceCombo_ = new QComboBox;
    ifaceCombo_->addItem("(any / default)", QString());
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp))
            ifaceCombo_->addItem(iface.name(), iface.name());
    }
    dmxForm->addRow("Interface:", ifaceCombo_);

    unicastRow_ = new QWidget;
    auto* uniRowLay = new QHBoxLayout(unicastRow_);
    uniRowLay->setContentsMargins(0, 0, 0, 0);
    auto* uniIpEdit = new QLineEdit;
    uniIpEdit->setPlaceholderText("x.x.x.x");
    unicastIpEdit_ = uniIpEdit;
    uniRowLay->addWidget(uniIpEdit);
    dmxForm->addRow("Unicast IP:", unicastRow_);
    unicastRow_->setVisible(false);

    detailLay->addWidget(dmxFields_);

    // ── MIDI connection fields ─────────────────────────────────────────────────
    midiFields_ = new QGroupBox("MIDI Connection");
    auto* midiForm = new QFormLayout(midiFields_);

    midiPortCombo_ = new QComboBox;
    midiPortCombo_->addItem("(refresh to scan)", QString());
    auto* refreshBtn = new QPushButton("Refresh Ports");
    auto* midiPortRow = new QWidget;
    auto* midiPortRowLay = new QHBoxLayout(midiPortRow);
    midiPortRowLay->setContentsMargins(0, 0, 0, 0);
    midiPortRowLay->addWidget(midiPortCombo_, 1);
    midiPortRowLay->addWidget(refreshBtn);
    midiForm->addRow("MIDI Port:", midiPortRow);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        const QString current = midiPortCombo_->currentData().toString();
        midiPortCombo_->clear();
        midiPortCombo_->addItem("(none)", QString());
        for (const auto& p : MidiInputAdapter::availablePorts())
            midiPortCombo_->addItem(p, p);
        int idx = midiPortCombo_->findData(current);
        if (idx >= 0) midiPortCombo_->setCurrentIndex(idx);
    });

    detailLay->addWidget(midiFields_);
    midiFields_->setVisible(false);

    // ── Mapping list ─────────────────────────────────────────────────────────
    detailLay->addWidget(new QLabel("<b>Mappings</b>"));

    mappingScroll_ = new QScrollArea;
    mappingScroll_->setWidgetResizable(true);
    mappingScroll_->setFrameShape(QFrame::StyledPanel);
    mappingContainer_ = new QWidget;
    mappingLayout_    = new QVBoxLayout(mappingContainer_);
    mappingLayout_->setContentsMargins(4, 4, 4, 4);
    mappingLayout_->setSpacing(2);
    mappingLayout_->addStretch();
    mappingScroll_->setWidget(mappingContainer_);
    detailLay->addWidget(mappingScroll_, 1);

    addMappingBtn_ = new QPushButton("+ Add Mapping");
    addMappingBtn_->setEnabled(false);
    detailLay->addWidget(addMappingBtn_);

    detailLay->addStretch();
    splitter->addWidget(detailWidget_);
    splitter->setSizes({160, 500});

    // Initially hide detail until an adapter is selected
    detailWidget_->setEnabled(false);

    // ── Connect signals ───────────────────────────────────────────────────────
    connect(adapterList_, &QListWidget::currentRowChanged, this, &InputAdaptersPanel::onAdapterSelectionChanged);
    connect(addAdapterBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onAddAdapter);
    connect(removeAdapterBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onRemoveAdapter);
    connect(addMappingBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onAddMapping);
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InputAdaptersPanel::onTypeChanged);
    connect(enabledCheck_, &QCheckBox::toggled, this, &InputAdaptersPanel::onAdapterFieldChanged);
    connect(protocolCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InputAdaptersPanel::onAdapterFieldChanged);
    connect(netModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        unicastRow_->setVisible(netModeCombo_->itemData(idx).toInt() == int(DmxNetworkMode::Unicast));
        onAdapterFieldChanged();
    });
    connect(ifaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InputAdaptersPanel::onAdapterFieldChanged);
    connect(midiPortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InputAdaptersPanel::onAdapterFieldChanged);
    connect(qobject_cast<QLineEdit*>(unicastIpEdit_), &QLineEdit::textChanged, this, &InputAdaptersPanel::onAdapterFieldChanged);
}

void InputAdaptersPanel::setAdapters(const QList<InputAdapterConfig>& adapters) {
    adapters_ = adapters;
    rebuildAdapterList();
    if (!adapters_.isEmpty())
        adapterList_->setCurrentRow(0);
    else
        showAdapterDetail(-1);
}

void InputAdaptersPanel::rebuildAdapterList() {
    updating_ = true;
    adapterList_->clear();
    for (const auto& a : adapters_) {
        QString label = (a.type == InputAdapterType::SacnArtNet)
            ? QString("sACN/ArtNet")
            : QString("MIDI");
        if (!a.enabled) label += " (off)";
        adapterList_->addItem(label);
    }
    updating_ = false;
}

void InputAdaptersPanel::onAdapterSelectionChanged() {
    if (updating_) return;
    int row = adapterList_->currentRow();
    removeAdapterBtn_->setEnabled(row >= 0);
    showAdapterDetail(row);
}

void InputAdaptersPanel::showAdapterDetail(int index) {
    currentIndex_ = index;
    detailWidget_->setEnabled(index >= 0);
    addMappingBtn_->setEnabled(index >= 0);

    if (index < 0 || index >= adapters_.size()) {
        rebuildMappingRows();
        return;
    }

    updating_ = true;
    const auto& a = adapters_[index];

    typeCombo_->setCurrentIndex(a.type == InputAdapterType::SacnArtNet ? 0 : 1);
    enabledCheck_->setChecked(a.enabled);

    // DMX fields
    int pIdx = protocolCombo_->findData(int(a.protocol));
    if (pIdx >= 0) protocolCombo_->setCurrentIndex(pIdx);
    int nmIdx = netModeCombo_->findData(int(a.netMode));
    if (nmIdx >= 0) netModeCombo_->setCurrentIndex(nmIdx);
    unicastRow_->setVisible(a.netMode == DmxNetworkMode::Unicast);
    qobject_cast<QLineEdit*>(unicastIpEdit_)->setText(a.unicastIp);

    int ifIdx = ifaceCombo_->findData(a.iface);
    if (ifIdx >= 0) ifaceCombo_->setCurrentIndex(ifIdx);
    else ifaceCombo_->setCurrentIndex(0);

    // MIDI: refresh ports and select current
    if (a.type == InputAdapterType::Midi) {
        const QString currentPort = a.iface; // iface = MIDI port for MIDI adapters
        int mpIdx = midiPortCombo_->findData(currentPort);
        if (mpIdx >= 0) midiPortCombo_->setCurrentIndex(mpIdx);
    }

    bool isMidi = (a.type == InputAdapterType::Midi);
    dmxFields_->setVisible(!isMidi);
    midiFields_->setVisible(isMidi);

    updating_ = false;
    rebuildMappingRows();
}

void InputAdaptersPanel::onTypeChanged(int idx) {
    if (updating_ || currentIndex_ < 0) return;
    bool isMidi = (idx == 1);
    dmxFields_->setVisible(!isMidi);
    midiFields_->setVisible(isMidi);
    onAdapterFieldChanged();
}

void InputAdaptersPanel::collectCurrentDetail() {
    if (currentIndex_ < 0 || currentIndex_ >= adapters_.size()) return;
    auto& a = adapters_[currentIndex_];

    a.type    = (typeCombo_->currentIndex() == 0) ? InputAdapterType::SacnArtNet : InputAdapterType::Midi;
    a.enabled = enabledCheck_->isChecked();

    if (a.type == InputAdapterType::SacnArtNet) {
        a.protocol = DmxProtocol(protocolCombo_->currentData().toInt());
        a.netMode  = DmxNetworkMode(netModeCombo_->currentData().toInt());
        a.iface    = ifaceCombo_->currentData().toString();
        a.unicastIp = qobject_cast<QLineEdit*>(unicastIpEdit_)->text();
    } else {
        a.iface = midiPortCombo_->currentData().toString(); // port name stored in iface
    }

    // Collect mapping rows
    a.mappings.clear();
    for (auto* row : mappingRows_)
        a.mappings.append(row->mapping());
}

void InputAdaptersPanel::onAdapterFieldChanged() {
    if (updating_) return;
    collectCurrentDetail();
    // Update list display
    if (currentIndex_ >= 0 && currentIndex_ < adapters_.size()) {
        const auto& a = adapters_[currentIndex_];
        QString label = (a.type == InputAdapterType::SacnArtNet) ? "sACN/ArtNet" : "MIDI";
        if (!a.enabled) label += " (off)";
        updating_ = true;
        adapterList_->item(currentIndex_)->setText(label);
        updating_ = false;
    }
    emitChanged();
}

void InputAdaptersPanel::onAddAdapter() {
    InputAdapterConfig a;
    a.type    = InputAdapterType::SacnArtNet;
    a.enabled = true;
    adapters_.append(a);
    rebuildAdapterList();
    adapterList_->setCurrentRow(adapters_.size() - 1);
    emitChanged();
}

void InputAdaptersPanel::onRemoveAdapter() {
    int row = adapterList_->currentRow();
    if (row < 0 || row >= adapters_.size()) return;
    adapters_.removeAt(row);
    currentIndex_ = -1;
    rebuildAdapterList();
    if (!adapters_.isEmpty())
        adapterList_->setCurrentRow(qMin(row, adapters_.size() - 1));
    else
        showAdapterDetail(-1);
    emitChanged();
}

void InputAdaptersPanel::onAddMapping() {
    if (currentIndex_ < 0 || currentIndex_ >= adapters_.size()) return;
    collectCurrentDetail();
    InputAdapterMapping m;
    m.target = kTargets[0];
    adapters_[currentIndex_].mappings.append(m);
    rebuildMappingRows();
    emitChanged();
}

void InputAdaptersPanel::rebuildMappingRows() {
    // Remove old rows
    for (auto* r : mappingRows_) r->deleteLater();
    mappingRows_.clear();

    if (currentIndex_ < 0 || currentIndex_ >= adapters_.size()) return;

    const auto& a     = adapters_[currentIndex_];
    bool        isMidi = (a.type == InputAdapterType::Midi);

    // Insert before the stretch (last item in layout)
    for (const auto& m : a.mappings) {
        auto* row = new MappingRowWidget(m, isMidi, mappingContainer_);
        mappingRows_.append(row);
        // Insert before stretch
        mappingLayout_->insertWidget(mappingLayout_->count() - 1, row);

        connect(row, &MappingRowWidget::changed, this, [this]() {
            if (updating_) return;
            collectCurrentDetail();
            emitChanged();
        });
        connect(row, &MappingRowWidget::removeRequested, this, [this, row]() {
            int idx = mappingRows_.indexOf(row);
            if (idx < 0 || currentIndex_ < 0) return;
            collectCurrentDetail();
            adapters_[currentIndex_].mappings.removeAt(idx);
            rebuildMappingRows();
            emitChanged();
        });
    }
}

void InputAdaptersPanel::emitChanged() {
    emit adaptersChanged(adapters_);
}
