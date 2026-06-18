#include "InputAdaptersPanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QDateTime>

static const QStringList kTargets = {
    "clickPlaneHeight", "dimmer", "zoom", "iris", "focus"
};

// ── MappingRowWidget (MIDI only) ──────────────────────────────────────────────

MappingRowWidget::MappingRowWidget(const InputAdapterMapping& m, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(4);

    targetCombo_ = new QComboBox;
    targetCombo_->addItems(kTargets);
    targetCombo_->setCurrentText(m.target.isEmpty() ? kTargets[0] : m.target);
    lay->addWidget(targetCombo_);

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

    learnBtn_ = new QPushButton("Learn");
    learnBtn_->setCheckable(true);
    learnBtn_->setFixedWidth(50);
    learnBtn_->setToolTip("Click then move a MIDI knob to auto-assign CC and channel");
    lay->addWidget(learnBtn_);

    auto* removeBtn = new QPushButton("X");
    removeBtn->setFixedWidth(28);
    removeBtn->setToolTip("Remove this mapping");
    lay->addWidget(removeBtn);

    connect(targetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MappingRowWidget::changed);
    connect(spin1_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(spin2_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(minSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(maxSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MappingRowWidget::changed);
    connect(learnBtn_, &QPushButton::clicked, this, &MappingRowWidget::learnRequested);
    connect(removeBtn, &QPushButton::clicked, this, &MappingRowWidget::removeRequested);
}

void MappingRowWidget::applyLearnedCC(int cc, int channel) {
    QSignalBlocker b1(spin1_), b2(spin2_);
    spin1_->setValue(cc);
    spin2_->setValue(channel);
    learnBtn_->setChecked(false);
    emit changed();
}

void MappingRowWidget::setLearning(bool on) {
    learnBtn_->setChecked(on);
}

InputAdapterMapping MappingRowWidget::mapping() const {
    InputAdapterMapping m;
    m.target      = targetCombo_->currentText();
    m.midiCC      = spin1_->value();
    m.midiChannel = spin2_->value();
    m.minValue    = float(minSpin_->value());
    m.maxValue    = float(maxSpin_->value());
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
    removeAdapterBtn_ = new QPushButton("-");
    addAdapterBtn_->setFixedHeight(26);
    removeAdapterBtn_->setFixedHeight(26);
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

    // Type label (always MIDI) + enabled
    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel("Type: MIDI"));
    topRow->addSpacing(16);
    enabledCheck_ = new QCheckBox("Enabled");
    enabledCheck_->setChecked(true);
    topRow->addWidget(enabledCheck_);
    topRow->addStretch();
    detailLay->addLayout(topRow);

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

    // ── MIDI event log ────────────────────────────────────────────────────────
    auto* logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel("<b>MIDI Log</b>"));
    auto* clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setFixedHeight(20);
    logHeader->addStretch();
    logHeader->addWidget(clearLogBtn);
    detailLay->addLayout(logHeader);

    midiLog_ = new QPlainTextEdit;
    midiLog_->setReadOnly(true);
    midiLog_->setMaximumBlockCount(200);
    midiLog_->setFixedHeight(90);
    midiLog_->setPlaceholderText("MIDI CC events appear here…");
    QFont mono = midiLog_->font();
    mono.setFamily("Menlo");
    mono.setPointSize(10);
    midiLog_->setFont(mono);
    detailLay->addWidget(midiLog_);

    connect(clearLogBtn, &QPushButton::clicked, midiLog_, &QPlainTextEdit::clear);

    detailLay->addStretch();
    splitter->addWidget(detailWidget_);
    splitter->setSizes({160, 500});

    detailWidget_->setEnabled(false);

    // ── Connect signals ───────────────────────────────────────────────────────
    connect(adapterList_, &QListWidget::currentRowChanged, this, &InputAdaptersPanel::onAdapterSelectionChanged);
    connect(addAdapterBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onAddAdapter);
    connect(removeAdapterBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onRemoveAdapter);
    connect(addMappingBtn_, &QPushButton::clicked, this, &InputAdaptersPanel::onAddMapping);
    connect(enabledCheck_, &QCheckBox::toggled, this, &InputAdaptersPanel::onAdapterFieldChanged);
    connect(midiPortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InputAdaptersPanel::onAdapterFieldChanged);
}

void InputAdaptersPanel::setAdapters(const QList<InputAdapterConfig>& adapters) {
    adapters_.clear();
    for (const auto& a : adapters) {
        if (a.type == InputAdapterType::Midi)
            adapters_.append(a);
    }
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
        QString label = "MIDI";
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
    enabledCheck_->setChecked(a.enabled);

    const QString currentPort = a.iface;
    int mpIdx = midiPortCombo_->findData(currentPort);
    if (mpIdx >= 0) midiPortCombo_->setCurrentIndex(mpIdx);

    updating_ = false;
    rebuildMappingRows();
}

void InputAdaptersPanel::collectCurrentDetail() {
    if (currentIndex_ < 0 || currentIndex_ >= adapters_.size()) return;
    auto& a = adapters_[currentIndex_];
    a.type    = InputAdapterType::Midi;
    a.enabled = enabledCheck_->isChecked();
    a.iface   = midiPortCombo_->currentData().toString();

    a.mappings.clear();
    for (auto* row : mappingRows_)
        a.mappings.append(row->mapping());
}

void InputAdaptersPanel::onAdapterFieldChanged() {
    if (updating_) return;
    collectCurrentDetail();
    if (currentIndex_ >= 0 && currentIndex_ < adapters_.size()) {
        const auto& a = adapters_[currentIndex_];
        QString label = "MIDI";
        if (!a.enabled) label += " (off)";
        updating_ = true;
        adapterList_->item(currentIndex_)->setText(label);
        updating_ = false;
    }
    emitChanged();
}

void InputAdaptersPanel::onAddAdapter() {
    InputAdapterConfig a;
    a.type    = InputAdapterType::Midi;
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
    for (auto* r : mappingRows_) r->deleteLater();
    mappingRows_.clear();

    if (currentIndex_ < 0 || currentIndex_ >= adapters_.size()) return;
    const auto& a = adapters_[currentIndex_];

    for (const auto& m : a.mappings) {
        auto* row = new MappingRowWidget(m, mappingContainer_);
        mappingRows_.append(row);
        mappingLayout_->insertWidget(mappingLayout_->count() - 1, row);

        connect(row, &MappingRowWidget::changed, this, [this]() {
            if (updating_) return;
            collectCurrentDetail();
            emitChanged();
        });
        connect(row, &MappingRowWidget::removeRequested, this, [this, row]() {
            int idx = mappingRows_.indexOf(row);
            if (idx < 0 || currentIndex_ < 0) return;
            if (learnRow_ == row) learnRow_ = nullptr;
            collectCurrentDetail();
            adapters_[currentIndex_].mappings.removeAt(idx);
            rebuildMappingRows();
            emitChanged();
        });
        connect(row, &MappingRowWidget::learnRequested, this, [this, row]() {
            // Cancel any previous learn row
            if (learnRow_ && learnRow_ != row)
                learnRow_->setLearning(false);
            learnRow_ = row;
            emit requestLearn();
        });
    }
}

void InputAdaptersPanel::logMidiEvent(int cc, int ch, int rawVal) {
    if (!midiLog_) return;
    midiLog_->appendPlainText(
        QStringLiteral("CC %1  Ch %2  Val %3")
            .arg(cc, 3).arg(ch).arg(rawVal, 3));
}

void InputAdaptersPanel::applyLearnedCC(int cc, int ch) {
    if (!learnRow_) return;
    learnRow_->applyLearnedCC(cc, ch);
    learnRow_ = nullptr;
    collectCurrentDetail();
    emitChanged();
}

void InputAdaptersPanel::emitChanged() {
    emit adaptersChanged(adapters_);
}
