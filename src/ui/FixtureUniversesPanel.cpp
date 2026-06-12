#include "FixtureUniversesPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────────

static void populateIfaceCombo(QComboBox* combo, const QString& current) {
    combo->blockSignals(true);
    combo->clear();
    combo->addItem("(default)", QString());
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        bool hasIpv4 = false;
        for (const auto& addr : iface.addressEntries())
            if (addr.ip().protocol() == QAbstractSocket::IPv4Protocol) { hasIpv4 = true; break; }
        if (!hasIpv4) continue;
        combo->addItem(iface.humanReadableName(), iface.humanReadableName());
    }
    const int idx = combo->findData(current);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->blockSignals(false);
}

static QComboBox* makeProtocolCombo(const DmxProtocol current) {
    auto* c = new QComboBox;
    c->addItem("sACN",   int(DmxProtocol::SACN));
    c->addItem("ArtNet", int(DmxProtocol::ArtNet));
    c->setCurrentIndex(current == DmxProtocol::ArtNet ? 1 : 0);
    return c;
}

static QComboBox* makeModeCombo(const DmxNetworkMode current) {
    auto* c = new QComboBox;
    c->addItem("Multicast", int(DmxNetworkMode::Multicast));
    c->addItem("Unicast",   int(DmxNetworkMode::Unicast));
    c->addItem("Broadcast", int(DmxNetworkMode::Broadcast));
    c->setCurrentIndex(int(current));
    return c;
}

static QComboBox* makeIfaceCombo(const QString& current) {
    auto* c = new QComboBox;
    populateIfaceCombo(c, current);
    return c;
}

static QWidget* centered(QWidget* w) {
    auto* c = new QWidget;
    auto* l = new QHBoxLayout(c);
    l->setContentsMargins(2, 1, 2, 1);
    l->addStretch();
    l->addWidget(w);
    l->addStretch();
    return c;
}

// ── FixtureUniversesPanel ─────────────────────────────────────────────────────

FixtureUniversesPanel::FixtureUniversesPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto* header = new QLabel(
        "Configure DMX universe routing for follow-spot fixtures. "
        "Enable Follow-spots to have OnPoint compute and send pan/tilt.", this);
    header->setWordWrap(true);
    header->setContentsMargins(8, 6, 8, 2);
    lay->addWidget(header);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setHorizontalHeaderLabels({
        "Universe", "Fixtures", "Follow-\nspots",
        "IN\nEnabled", "IN\nProtocol", "IN\nMode", "IN\nInterface", "IN IP",
        "Same\nOutput", "OUT\nUniverse",
        "OUT\nEnabled", "OUT\nProtocol", "OUT\nMode", "OUT\nInterface", "OUT IP",
        "Priority"
    });
    auto* hdr = table_->horizontalHeader();
    // Universe and Fixtures auto-size; all others are user-resizable with fixed initial widths
    hdr->setSectionResizeMode(QHeaderView::Interactive);
    hdr->setSectionResizeMode(ColUniverse,  QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(ColFixtures,  QHeaderView::ResizeToContents);
    hdr->setStretchLastSection(false);
    hdr->setDefaultAlignment(Qt::AlignCenter);
    hdr->setSortIndicatorShown(true);
    hdr->setSortIndicator(ColUniverse, Qt::AscendingOrder);
    connect(hdr, &QHeaderView::sectionClicked, this, &FixtureUniversesPanel::onHeaderClicked);

    // Initial column widths for widget columns
    table_->setColumnWidth(ColFollowSpots,  60);
    table_->setColumnWidth(ColInEnabled,    60);
    table_->setColumnWidth(ColInProtocol,   72);
    table_->setColumnWidth(ColInMode,       88);
    table_->setColumnWidth(ColInIface,      96);
    table_->setColumnWidth(ColInIp,         86);
    table_->setColumnWidth(ColSameOutput,   60);
    table_->setColumnWidth(ColOutUniverse,  64);
    table_->setColumnWidth(ColOutEnabled,   60);
    table_->setColumnWidth(ColOutProtocol,  72);
    table_->setColumnWidth(ColOutMode,      88);
    table_->setColumnWidth(ColOutIface,     96);
    table_->setColumnWidth(ColOutIp,        86);
    table_->setColumnWidth(ColOutPriority,  60);

    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    lay->addWidget(table_, 1);

    emptyLabel_ = new QLabel("No fixture universes detected.\nImport an MVR file to get started.", this);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setEnabled(false);
    emptyLabel_->setVisible(false);
    lay->addWidget(emptyLabel_);
}

void FixtureUniversesPanel::setConfigs(const QList<FixtureUniverseConfig>& configs) {
    configs_ = configs;
    std::stable_sort(configs_.begin(), configs_.end(),
        [](const FixtureUniverseConfig& a, const FixtureUniverseConfig& b) {
            return a.fixtureUniverse < b.fixtureUniverse;
        });
    sortCol_ = ColUniverse;
    sortAsc_ = true;
    rebuild();
}

void FixtureUniversesPanel::setMvrData(const MvrSettings& mvr) {
    mvr_ = mvr;
    rebuild();
}

QList<FixtureUniverseConfig> FixtureUniversesPanel::configs() const {
    QList<FixtureUniverseConfig> result;
    for (const auto& rw : rowWidgets_) {
        FixtureUniverseConfig c;
        c.fixtureUniverse  = rw.fixtureUniverse;
        c.hasFollowSpots   = rw.followSpots->isChecked();
        c.inputEnabled     = rw.inEnabled->isChecked();
        c.inputProtocol    = DmxProtocol(rw.inProtocol->currentData().toInt());
        c.inputNetMode     = DmxNetworkMode(rw.inMode->currentIndex());
        c.inputIface       = rw.inIface->currentData().toString();
        c.inputUnicastIp   = rw.inIp->text();
        c.outputUniverse   = rw.sameOutput->isChecked() ? -1 : rw.outUniverse->value();
        c.outputEnabled    = rw.outEnabled->isChecked();
        c.outputProtocol   = DmxProtocol(rw.outProtocol->currentData().toInt());
        c.outputNetMode    = DmxNetworkMode(rw.outMode->currentIndex());
        c.outputIface      = rw.outIface->currentData().toString();
        c.outputUnicastIp  = rw.outIp->text();
        c.outputPriority   = rw.outPriority->value();
        result.append(c);
    }
    return result;
}

int FixtureUniversesPanel::fixtureCountForUniverse(quint16 uni) const {
    int count = 0;
    for (const auto& imp : mvr_.imports)
        for (const auto& layer : imp.layers)
            for (const auto& obj : layer.objects)
                if (obj.type == MvrObjectData::Type::Fixture && quint16(obj.universe) == uni)
                    ++count;
    return count;
}

void FixtureUniversesPanel::emitChanged() {
    if (!updating_)
        emit fixtureUniverseConfigsChanged(configs());
}

void FixtureUniversesPanel::updateRowEnabled(int row) {
    const auto& rw = rowWidgets_[row];
    const bool active = rw.followSpots->isChecked();

    rw.inEnabled->setEnabled(active);
    rw.inProtocol->setEnabled(active);
    rw.inMode->setEnabled(active);
    rw.inIface->setEnabled(active);
    rw.inIp->setEnabled(active && DmxNetworkMode(rw.inMode->currentIndex()) != DmxNetworkMode::Multicast);
    rw.sameOutput->setEnabled(active);
    rw.outUniverse->setEnabled(active && !rw.sameOutput->isChecked());
    rw.outEnabled->setEnabled(active);
    rw.outProtocol->setEnabled(active);
    rw.outMode->setEnabled(active);
    rw.outIface->setEnabled(active);
    rw.outIp->setEnabled(active && DmxNetworkMode(rw.outMode->currentIndex()) != DmxNetworkMode::Multicast);
    rw.outPriority->setEnabled(active);
}

void FixtureUniversesPanel::onHeaderClicked(int col) {
    // Snapshot current widget state before re-sorting
    if (!rowWidgets_.isEmpty())
        configs_ = configs();

    if (sortCol_ == col)
        sortAsc_ = !sortAsc_;
    else {
        sortCol_ = col;
        sortAsc_ = true;
    }

    std::stable_sort(configs_.begin(), configs_.end(),
        [&](const FixtureUniverseConfig& a, const FixtureUniverseConfig& b) {
            bool less = false;
            switch (col) {
                case ColUniverse:    less = a.fixtureUniverse < b.fixtureUniverse; break;
                case ColFixtures:    less = fixtureCountForUniverse(a.fixtureUniverse)
                                              < fixtureCountForUniverse(b.fixtureUniverse); break;
                case ColFollowSpots: less = int(a.hasFollowSpots) < int(b.hasFollowSpots); break;
                case ColInEnabled:   less = int(a.inputEnabled) < int(b.inputEnabled); break;
                case ColSameOutput:  less = int(a.outputUniverse < 0) < int(b.outputUniverse < 0); break;
                case ColOutUniverse: less = a.outputUniverse < b.outputUniverse; break;
                case ColOutEnabled:  less = int(a.outputEnabled) < int(b.outputEnabled); break;
                case ColOutPriority: less = a.outputPriority < b.outputPriority; break;
                default:             less = a.fixtureUniverse < b.fixtureUniverse; break;
            }
            return sortAsc_ ? less : !less;
        });

    rebuild();
    table_->horizontalHeader()->setSortIndicator(sortCol_, sortAsc_ ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void FixtureUniversesPanel::rebuild() {
    updating_ = true;
    table_->setRowCount(0);
    rowWidgets_.clear();

    for (const auto& cfg : configs_) {
        const int row = table_->rowCount();
        table_->insertRow(row);

        // Universe number (provides sort key for ColUniverse)
        auto* uniItem = new QTableWidgetItem(QString("U%1").arg(cfg.fixtureUniverse));
        uniItem->setTextAlignment(Qt::AlignCenter);
        QFont f = uniItem->font(); f.setBold(true); uniItem->setFont(f);
        uniItem->setFlags(uniItem->flags() & ~Qt::ItemIsEditable);
        uniItem->setData(Qt::UserRole, int(cfg.fixtureUniverse));
        table_->setItem(row, ColUniverse, uniItem);

        // Fixture count
        const int count = fixtureCountForUniverse(cfg.fixtureUniverse);
        auto* cntItem = new QTableWidgetItem(count > 0 ? QString::number(count) : "-");
        cntItem->setTextAlignment(Qt::AlignCenter);
        cntItem->setFlags(cntItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, ColFixtures, cntItem);

        RowWidgets rw;
        rw.fixtureUniverse = cfg.fixtureUniverse;

        rw.followSpots = new QCheckBox;
        rw.followSpots->setChecked(cfg.hasFollowSpots);
        table_->setCellWidget(row, ColFollowSpots, centered(rw.followSpots));

        rw.inEnabled = new QCheckBox;
        rw.inEnabled->setChecked(cfg.inputEnabled);
        table_->setCellWidget(row, ColInEnabled, centered(rw.inEnabled));

        rw.inProtocol = makeProtocolCombo(cfg.inputProtocol);
        table_->setCellWidget(row, ColInProtocol, rw.inProtocol);

        rw.inMode = makeModeCombo(cfg.inputNetMode);
        table_->setCellWidget(row, ColInMode, rw.inMode);

        rw.inIface = makeIfaceCombo(cfg.inputIface);
        table_->setCellWidget(row, ColInIface, rw.inIface);

        rw.inIp = new QLineEdit;
        rw.inIp->setPlaceholderText("x.x.x.x");
        rw.inIp->setText(cfg.inputUnicastIp);
        table_->setCellWidget(row, ColInIp, rw.inIp);

        rw.sameOutput = new QCheckBox;
        rw.sameOutput->setChecked(cfg.outputUniverse < 0);
        table_->setCellWidget(row, ColSameOutput, centered(rw.sameOutput));

        rw.outUniverse = new QSpinBox;
        rw.outUniverse->setRange(1, 63999);
        rw.outUniverse->setValue(cfg.outputUniverse >= 0 ? cfg.outputUniverse : int(cfg.fixtureUniverse));
        table_->setCellWidget(row, ColOutUniverse, rw.outUniverse);

        rw.outEnabled = new QCheckBox;
        rw.outEnabled->setChecked(cfg.outputEnabled);
        table_->setCellWidget(row, ColOutEnabled, centered(rw.outEnabled));

        rw.outProtocol = makeProtocolCombo(cfg.outputProtocol);
        table_->setCellWidget(row, ColOutProtocol, rw.outProtocol);

        rw.outMode = makeModeCombo(cfg.outputNetMode);
        table_->setCellWidget(row, ColOutMode, rw.outMode);

        rw.outIface = makeIfaceCombo(cfg.outputIface);
        table_->setCellWidget(row, ColOutIface, rw.outIface);

        rw.outIp = new QLineEdit;
        rw.outIp->setPlaceholderText("x.x.x.x");
        rw.outIp->setText(cfg.outputUnicastIp);
        table_->setCellWidget(row, ColOutIp, rw.outIp);

        rw.outPriority = new QSpinBox;
        rw.outPriority->setRange(0, 200);
        rw.outPriority->setValue(cfg.outputPriority);
        table_->setCellWidget(row, ColOutPriority, rw.outPriority);

        rowWidgets_.append(rw);

        // Connections — capture row index for per-row logic
        connect(rw.followSpots, &QCheckBox::toggled, this, [this, row](bool) {
            updateRowEnabled(row);
            emitChanged();
        });
        connect(rw.sameOutput, &QCheckBox::toggled, this, [this, row](bool same) {
            rowWidgets_[row].outUniverse->setEnabled(!same);
            emitChanged();
        });
        connect(rw.inMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row](int idx) {
            rowWidgets_[row].inIp->setEnabled(DmxNetworkMode(idx) != DmxNetworkMode::Multicast);
            emitChanged();
        });
        connect(rw.outMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row](int idx) {
            rowWidgets_[row].outIp->setEnabled(DmxNetworkMode(idx) != DmxNetworkMode::Multicast);
            emitChanged();
        });

        auto changed = [this] { emitChanged(); };
        connect(rw.inEnabled,   &QCheckBox::toggled,                                     this, changed);
        connect(rw.inProtocol,  QOverload<int>::of(&QComboBox::currentIndexChanged),     this, changed);
        connect(rw.inIface,     QOverload<int>::of(&QComboBox::currentIndexChanged),     this, changed);
        connect(rw.inIp,        &QLineEdit::textChanged,                                 this, changed);
        connect(rw.outUniverse, QOverload<int>::of(&QSpinBox::valueChanged),             this, changed);
        connect(rw.outEnabled,  &QCheckBox::toggled,                                     this, changed);
        connect(rw.outProtocol, QOverload<int>::of(&QComboBox::currentIndexChanged),     this, changed);
        connect(rw.outIface,    QOverload<int>::of(&QComboBox::currentIndexChanged),     this, changed);
        connect(rw.outIp,       &QLineEdit::textChanged,                                 this, changed);
        connect(rw.outPriority, QOverload<int>::of(&QSpinBox::valueChanged),             this, changed);

        updateRowEnabled(row);
    }

    emptyLabel_->setVisible(configs_.isEmpty());
    table_->horizontalHeader()->setSortIndicator(sortCol_, sortAsc_ ? Qt::AscendingOrder : Qt::DescendingOrder);
    updating_ = false;
}
