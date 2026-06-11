#include "DmxUniversesPanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <QNetworkInterface>
#include <QAbstractSocket>

static const QStringList kTargets = {
    "clickPlaneHeight", "dimmer", "zoom", "iris", "focus"
};

static QString roleName(DmxUniverseRole r) {
    switch (r) {
    case DmxUniverseRole::InControl:  return "IN Control";
    case DmxUniverseRole::InFixtures: return "IN Fixtures";
    default:                          return "OUT Fixtures";
    }
}

// ── DmxMappingRowWidget ───────────────────────────────────────────────────────

DmxMappingRowWidget::DmxMappingRowWidget(const DmxChannelMapping& m, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(4);

    targetCombo_ = new QComboBox;
    targetCombo_->addItems(kTargets);
    targetCombo_->setCurrentText(m.target.isEmpty() ? kTargets[0] : m.target);
    lay->addWidget(targetCombo_);

    channelSpin_ = new QSpinBox;
    channelSpin_->setRange(1, 512);
    channelSpin_->setValue(m.channel > 0 ? m.channel : 1);
    channelSpin_->setPrefix("Ch: ");
    channelSpin_->setFixedWidth(80);
    lay->addWidget(channelSpin_);

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
    removeBtn->setToolTip("Remove mapping");
    lay->addWidget(removeBtn);

    connect(targetCombo_,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DmxMappingRowWidget::changed);
    connect(channelSpin_,  QOverload<int>::of(&QSpinBox::valueChanged),         this, &DmxMappingRowWidget::changed);
    connect(minSpin_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DmxMappingRowWidget::changed);
    connect(maxSpin_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DmxMappingRowWidget::changed);
    connect(removeBtn, &QPushButton::clicked, this, &DmxMappingRowWidget::removeRequested);
}

DmxChannelMapping DmxMappingRowWidget::mapping() const {
    DmxChannelMapping m;
    m.target   = targetCombo_->currentText();
    m.channel  = quint16(channelSpin_->value());
    m.minValue = float(minSpin_->value());
    m.maxValue = float(maxSpin_->value());
    return m;
}

// ── DmxUniversesPanel ─────────────────────────────────────────────────────────

DmxUniversesPanel::DmxUniversesPanel(QWidget* parent) : QWidget(parent) {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* mainLay  = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->addWidget(splitter);

    // ── Left pane: universe table ─────────────────────────────────────────────
    auto* leftWidget = new QWidget;
    auto* leftLay    = new QVBoxLayout(leftWidget);
    leftLay->setContentsMargins(4, 4, 4, 4);
    leftLay->setSpacing(4);

    universeTable_ = new QTableWidget(0, 4);
    universeTable_->setHorizontalHeaderLabels({"Role", "Name", "U#", "Protocol"});
    universeTable_->horizontalHeader()->setStretchLastSection(false);
    universeTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    universeTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    universeTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    universeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    universeTable_->verticalHeader()->setVisible(false);
    universeTable_->setMinimumWidth(240);
    leftLay->addWidget(universeTable_);

    auto* btnRow = new QHBoxLayout;
    addBtn_    = new QPushButton("+");
    removeBtn_ = new QPushButton("-");
    addBtn_->setFixedHeight(26);
    removeBtn_->setFixedHeight(26);
    removeBtn_->setEnabled(false);
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addStretch();
    leftLay->addLayout(btnRow);
    splitter->addWidget(leftWidget);

    // ── Right pane: detail ────────────────────────────────────────────────────
    detailWidget_ = new QWidget;
    auto* detailLay = new QVBoxLayout(detailWidget_);
    detailLay->setContentsMargins(8, 4, 4, 4);
    detailLay->setSpacing(6);

    auto* baseGroup = new QGroupBox("Universe");
    auto* baseForm  = new QFormLayout(baseGroup);

    nameEdit_ = new QLineEdit;
    baseForm->addRow("Name:", nameEdit_);

    numberSpin_ = new QSpinBox;
    numberSpin_->setRange(1, 63999);
    numberSpin_->setValue(1);
    baseForm->addRow("Universe #:", numberSpin_);

    roleCombo_ = new QComboBox;
    roleCombo_->addItem("IN Fixtures",  int(DmxUniverseRole::InFixtures));
    roleCombo_->addItem("OUT Fixtures", int(DmxUniverseRole::OutFixtures));
    baseForm->addRow("Role:", roleCombo_);

    protocolCombo_ = new QComboBox;
    protocolCombo_->addItem("sACN (E1.31)", int(DmxProtocol::SACN));
    protocolCombo_->addItem("ArtNet",       int(DmxProtocol::ArtNet));
    baseForm->addRow("Protocol:", protocolCombo_);

    netModeCombo_ = new QComboBox;
    netModeCombo_->addItem("Multicast", int(DmxNetworkMode::Multicast));
    netModeCombo_->addItem("Unicast",   int(DmxNetworkMode::Unicast));
    netModeCombo_->addItem("Broadcast", int(DmxNetworkMode::Broadcast));
    baseForm->addRow("Network Mode:", netModeCombo_);

    ifaceCombo_ = new QComboBox;
    populateIfaceCombo();
    baseForm->addRow("Interface:", ifaceCombo_);

    unicastRow_ = new QWidget;
    auto* uniRowLay = new QHBoxLayout(unicastRow_);
    uniRowLay->setContentsMargins(0, 0, 0, 0);
    unicastIpEdit_ = new QLineEdit;
    unicastIpEdit_->setPlaceholderText("x.x.x.x");
    uniRowLay->addWidget(unicastIpEdit_);
    baseForm->addRow("Unicast IP:", unicastRow_);
    unicastRow_->setVisible(false);

    detailLay->addWidget(baseGroup);

    // ── InControl section ─────────────────────────────────────────────────────
    inControlSection_ = new QGroupBox("Channel Mappings");
    auto* icLay = new QVBoxLayout(inControlSection_);
    icLay->setContentsMargins(4, 4, 4, 4);
    icLay->setSpacing(2);

    auto* mappingScroll = new QScrollArea;
    mappingScroll->setWidgetResizable(true);
    mappingScroll->setFrameShape(QFrame::StyledPanel);
    mappingScroll->setMinimumHeight(80);
    auto* mappingContainer = new QWidget;
    mappingLayout_ = new QVBoxLayout(mappingContainer);
    mappingLayout_->setContentsMargins(4, 4, 4, 4);
    mappingLayout_->setSpacing(2);
    mappingLayout_->addStretch();
    mappingScroll->setWidget(mappingContainer);
    icLay->addWidget(mappingScroll, 1);

    auto* addMappingBtn = new QPushButton("+ Add Mapping");
    icLay->addWidget(addMappingBtn);
    connect(addMappingBtn, &QPushButton::clicked, this, &DmxUniversesPanel::onAddMapping);

    detailLay->addWidget(inControlSection_);

    // ── OutFixtures section ───────────────────────────────────────────────────
    outFixturesSection_ = new QGroupBox("Pass-Through Merge");
    auto* ofForm = new QFormLayout(outFixturesSection_);

    mergeSourceCombo_ = new QComboBox;
    mergeSourceCombo_->addItem("None (output only)", -1);
    ofForm->addRow("Merge from:", mergeSourceCombo_);

    auto* mergeHint = new QLabel(
        "When set, OnPoint reads the selected IN Fixtures universe as base data,\n"
        "overrides only the computed channels (pan/tilt), and sends the result.\n"
        "Fixture addresses must be identical in both universes.");
    mergeHint->setWordWrap(true);
    mergeHint->setStyleSheet("font-size: 11px; color: palette(mid);");
    ofForm->addRow(mergeHint);

    detailLay->addWidget(outFixturesSection_);
    detailLay->addStretch();

    splitter->addWidget(detailWidget_);
    splitter->setSizes({260, 500});
    detailWidget_->setEnabled(false);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(universeTable_, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { onSelectionChanged(); Q_UNUSED(row); });
    connect(addBtn_,    &QPushButton::clicked, this, &DmxUniversesPanel::onAddUniverse);
    connect(removeBtn_, &QPushButton::clicked, this, &DmxUniversesPanel::onRemoveUniverse);

    connect(nameEdit_,     &QLineEdit::textChanged,                                            this, &DmxUniversesPanel::onFieldChanged);
    connect(numberSpin_,   QOverload<int>::of(&QSpinBox::valueChanged),                        this, &DmxUniversesPanel::onFieldChanged);
    connect(roleCombo_,    QOverload<int>::of(&QComboBox::currentIndexChanged),                this, &DmxUniversesPanel::onRoleChanged);
    connect(protocolCombo_,QOverload<int>::of(&QComboBox::currentIndexChanged),                this, &DmxUniversesPanel::onFieldChanged);
    connect(netModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),                this, &DmxUniversesPanel::onNetModeChanged);
    connect(ifaceCombo_,   QOverload<int>::of(&QComboBox::currentIndexChanged),                this, &DmxUniversesPanel::onFieldChanged);
    connect(unicastIpEdit_,&QLineEdit::textChanged,                                            this, &DmxUniversesPanel::onFieldChanged);
    connect(mergeSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),            this, &DmxUniversesPanel::onFieldChanged);
}

void DmxUniversesPanel::populateIfaceCombo() {
    ifaceCombo_->clear();
    ifaceCombo_->addItem("(any / default)", QString());
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
        QString ipv4;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4 = entry.ip().toString(); break;
            }
        }
        if (ipv4.isEmpty()) continue;
        ifaceCombo_->addItem(iface.humanReadableName() + " — " + ipv4, iface.name());
    }
}

void DmxUniversesPanel::setUniverses(const QList<DmxUniverseEntry>& universes) {
    inControlUniverses_.clear();
    universes_.clear();
    for (const auto& e : universes) {
        if (e.role == DmxUniverseRole::InControl)
            inControlUniverses_.append(e);
        else
            universes_.append(e);
    }
    currentIndex_ = -1;
    rebuildTable();
    if (!universes_.isEmpty()) {
        universeTable_->selectRow(0);
        showDetail(0);
    } else {
        showDetail(-1);
    }
}

void DmxUniversesPanel::rebuildTable() {
    updating_ = true;
    universeTable_->setRowCount(0);
    for (const auto& e : universes_) {
        int row = universeTable_->rowCount();
        universeTable_->insertRow(row);
        universeTable_->setItem(row, 0, new QTableWidgetItem(roleName(e.role)));
        universeTable_->setItem(row, 1, new QTableWidgetItem(e.name));
        universeTable_->setItem(row, 2, new QTableWidgetItem(QString::number(e.number)));
        universeTable_->setItem(row, 3, new QTableWidgetItem(
            e.protocol == DmxProtocol::SACN ? "sACN" : "ArtNet"));
        if (!e.enabled) {
            for (int c = 0; c < 4; ++c) {
                auto* item = universeTable_->item(row, c);
                if (item) item->setForeground(Qt::gray);
            }
        }
    }
    updating_ = false;
}

void DmxUniversesPanel::onSelectionChanged() {
    if (updating_) return;
    int row = universeTable_->currentRow();
    removeBtn_->setEnabled(row >= 0);
    showDetail(row);
}

void DmxUniversesPanel::showDetail(int index) {
    currentIndex_ = index;
    detailWidget_->setEnabled(index >= 0);

    if (index < 0 || index >= universes_.size()) {
        rebuildMappingRows();
        return;
    }

    updating_ = true;
    const auto& e = universes_[index];

    nameEdit_->setText(e.name);
    numberSpin_->setValue(e.number);

    int rIdx = roleCombo_->findData(int(e.role));
    if (rIdx >= 0) roleCombo_->setCurrentIndex(rIdx);

    int pIdx = protocolCombo_->findData(int(e.protocol));
    if (pIdx >= 0) protocolCombo_->setCurrentIndex(pIdx);

    int nmIdx = netModeCombo_->findData(int(e.netMode));
    if (nmIdx >= 0) netModeCombo_->setCurrentIndex(nmIdx);
    unicastRow_->setVisible(e.netMode == DmxNetworkMode::Unicast);
    unicastIpEdit_->setText(e.unicastIp);

    int ifIdx = ifaceCombo_->findData(e.iface);
    ifaceCombo_->setCurrentIndex(ifIdx >= 0 ? ifIdx : 0);

    inControlSection_->setVisible(e.role == DmxUniverseRole::InControl);
    outFixturesSection_->setVisible(e.role == DmxUniverseRole::OutFixtures);

    updating_ = false;

    rebuildMappingRows();
    rebuildMergeCombo();
}

void DmxUniversesPanel::rebuildMergeCombo() {
    if (currentIndex_ < 0 || currentIndex_ >= universes_.size()) return;
    updating_ = true;
    mergeSourceCombo_->clear();
    mergeSourceCombo_->addItem("None (output only)", -1);
    for (const auto& e : universes_) {
        if (e.role == DmxUniverseRole::InFixtures) {
            mergeSourceCombo_->addItem(
                QString("%1 (U%2)").arg(e.name).arg(e.number),
                e.number);
        }
    }
    int mergeUni = universes_[currentIndex_].mergeFromUniverse;
    int idx = mergeSourceCombo_->findData(mergeUni);
    mergeSourceCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    updating_ = false;
}

void DmxUniversesPanel::rebuildMappingRows() {
    for (auto* r : mappingRows_) r->deleteLater();
    mappingRows_.clear();

    if (currentIndex_ < 0 || currentIndex_ >= universes_.size()) return;
    const auto& e = universes_[currentIndex_];

    for (const auto& m : e.mappings) {
        auto* row = new DmxMappingRowWidget(m, mappingLayout_->parentWidget());
        mappingRows_.append(row);
        mappingLayout_->insertWidget(mappingLayout_->count() - 1, row);

        connect(row, &DmxMappingRowWidget::changed, this, [this]() {
            if (updating_) return;
            collectCurrentDetail();
            emitChanged();
        });
        connect(row, &DmxMappingRowWidget::removeRequested, this, [this, row]() {
            int idx = mappingRows_.indexOf(row);
            if (idx < 0 || currentIndex_ < 0) return;
            collectCurrentDetail();
            universes_[currentIndex_].mappings.removeAt(idx);
            rebuildMappingRows();
            emitChanged();
        });
    }
}

void DmxUniversesPanel::collectCurrentDetail() {
    if (currentIndex_ < 0 || currentIndex_ >= universes_.size()) return;
    auto& e = universes_[currentIndex_];

    e.name     = nameEdit_->text();
    e.number   = quint16(numberSpin_->value());
    e.role     = DmxUniverseRole(roleCombo_->currentData().toInt());
    e.protocol = DmxProtocol(protocolCombo_->currentData().toInt());
    e.netMode  = DmxNetworkMode(netModeCombo_->currentData().toInt());
    e.iface    = ifaceCombo_->currentData().toString();
    e.unicastIp = unicastIpEdit_->text();
    e.mergeFromUniverse = mergeSourceCombo_->currentData().toInt();

    e.mappings.clear();
    for (auto* row : mappingRows_)
        e.mappings.append(row->mapping());
}

void DmxUniversesPanel::onFieldChanged() {
    if (updating_) return;
    collectCurrentDetail();
    // Update table row display
    if (currentIndex_ >= 0 && currentIndex_ < universes_.size()) {
        const auto& e = universes_[currentIndex_];
        updating_ = true;
        if (auto* it = universeTable_->item(currentIndex_, 0)) it->setText(roleName(e.role));
        if (auto* it = universeTable_->item(currentIndex_, 1)) it->setText(e.name);
        if (auto* it = universeTable_->item(currentIndex_, 2)) it->setText(QString::number(e.number));
        if (auto* it = universeTable_->item(currentIndex_, 3))
            it->setText(e.protocol == DmxProtocol::SACN ? "sACN" : "ArtNet");
        updating_ = false;
    }
    emitChanged();
}

void DmxUniversesPanel::onRoleChanged(int /*idx*/) {
    if (updating_) return;
    collectCurrentDetail();
    if (currentIndex_ >= 0 && currentIndex_ < universes_.size()) {
        const auto& e = universes_[currentIndex_];
        inControlSection_->setVisible(e.role == DmxUniverseRole::InControl);
        outFixturesSection_->setVisible(e.role == DmxUniverseRole::OutFixtures);
        rebuildMergeCombo();
    }
    onFieldChanged();
}

void DmxUniversesPanel::onNetModeChanged(int idx) {
    unicastRow_->setVisible(
        netModeCombo_->itemData(idx).toInt() == int(DmxNetworkMode::Unicast));
    onFieldChanged();
}

void DmxUniversesPanel::onAddUniverse() {
    DmxUniverseEntry e;
    e.role   = DmxUniverseRole::OutFixtures;
    e.number = 1;
    e.name   = QString("Fixture Output (U1)");
    universes_.append(e);
    rebuildTable();
    universeTable_->selectRow(universes_.size() - 1);
    showDetail(universes_.size() - 1);
    emitChanged();
}

void DmxUniversesPanel::onRemoveUniverse() {
    int row = universeTable_->currentRow();
    if (row < 0 || row >= universes_.size()) return;
    universes_.removeAt(row);
    currentIndex_ = -1;
    rebuildTable();
    if (!universes_.isEmpty()) {
        universeTable_->selectRow(qMin(row, universes_.size() - 1));
        showDetail(qMin(row, universes_.size() - 1));
    } else {
        showDetail(-1);
    }
    emitChanged();
}

void DmxUniversesPanel::onAddMapping() {
    if (currentIndex_ < 0 || currentIndex_ >= universes_.size()) return;
    collectCurrentDetail();
    DmxChannelMapping m;
    m.target   = kTargets[0];
    m.channel  = 1;
    m.minValue = 0.f;
    m.maxValue = 10.f;
    universes_[currentIndex_].mappings.append(m);
    rebuildMappingRows();
    emitChanged();
}

void DmxUniversesPanel::emitChanged() {
    // InControl entries are not editable here but must be preserved in the project
    emit dmxUniversesChanged(inControlUniverses_ + universes_);
}
