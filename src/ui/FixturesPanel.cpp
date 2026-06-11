#include "FixturesPanel.h"
#include "DmxAddrEdit.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QColor>
#include <QPushButton>
#include <QComboBox>

// ── Columns ───────────────────────────────────────────────────────────────────
// Both modes:  ColName | ColFid | ...
// Status mode: ColName | ColFid | ColAddrText | ColTracker | ColPan | ColTilt | ColStatus
// Patch mode:  ColName | ColFid | ColAddr     | ColMode    | ColGdtf
enum Col {
    ColFid      = 0,   // fixture ID (FID) — first column
    ColName     = 1,
    ColAddrText = 2,   // "U.CH" text  (status mode)
    ColAddr     = 3,   // DmxAddrEdit  (patch mode)
    ColTracker  = 4,   // status mode
    ColPan      = 5,   // status mode
    ColTilt     = 6,   // status mode
    ColStatus   = 7,   // status mode
    ColMode     = 8,   // patch mode: "ModeName (X ch)"
    ColGdtf     = 9,   // patch mode: GDTF assignment (clickable item)
    ColCount    = 10
};

// QTableWidgetItem that sorts numerically for columns with integer-valued text
class NumericItem : public QTableWidgetItem {
public:
    explicit NumericItem(const QString& text, int sortValue)
        : QTableWidgetItem(text), sortValue_(sortValue) {}
    bool operator<(const QTableWidgetItem& o) const override {
        if (auto* n = dynamic_cast<const NumericItem*>(&o))
            return sortValue_ < n->sortValue_;
        return QTableWidgetItem::operator<(o);
    }
private:
    int sortValue_;
};

FixturesPanel::FixturesPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    emptyLabel_ = new QLabel("No MVR fixtures linked to trackers.\n"
                             "Import an MVR file and link fixtures to trackers in Object Properties.");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setContentsMargins(16, 16, 16, 16);
    lay->addWidget(emptyLabel_);

    // Toggle bar
    toggleBar_ = new QWidget;
    toggleBar_->hide();
    auto* toggleLay = new QHBoxLayout(toggleBar_);
    toggleLay->setContentsMargins(4, 2, 4, 2);
    toggleLay->setSpacing(2);

    btnPatch_ = new QPushButton("Patch");
    btnPatch_->setCheckable(true);
    btnPatch_->setChecked(false);
    btnPatch_->setFixedHeight(22);
    toggleLay->addWidget(btnPatch_);

    toggleLay->addStretch();

    btnPhysical_ = new QPushButton("° Physical");
    btnPhysical_->setCheckable(true);
    btnPhysical_->setChecked(true);
    btnPhysical_->setFixedHeight(22);

    btnDmx_ = new QPushButton("DMX");
    btnDmx_->setCheckable(true);
    btnDmx_->setChecked(false);
    btnDmx_->setFixedHeight(22);

    toggleLay->addWidget(btnPhysical_);
    toggleLay->addWidget(btnDmx_);
    lay->addWidget(toggleBar_);

    connect(btnPhysical_, &QPushButton::clicked, this, [this]() {
        displayMode_ = DisplayMode::Physical;
        btnPhysical_->setChecked(true);
        btnDmx_->setChecked(false);
        applyStatus();
    });
    connect(btnDmx_, &QPushButton::clicked, this, [this]() {
        displayMode_ = DisplayMode::Dmx;
        btnDmx_->setChecked(true);
        btnPhysical_->setChecked(false);
        applyStatus();
    });
    connect(btnPatch_, &QPushButton::clicked, this, [this](bool checked) {
        patchMode_ = checked;
        btnPhysical_->setEnabled(!checked);
        btnDmx_->setEnabled(!checked);
        rebuild();
    });

    // Table — ColCount columns, visibility toggled in rebuild()
    table_ = new QTableWidget(0, ColCount);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->hide();
    lay->addWidget(table_);

    // Click handler for the GDTF column in patch mode
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col != ColGdtf || !patchMode_) return;
        if (row >= 0 && row < rows_.size()) {
            const auto& r = rows_[row];
            emit gdtfAssignRequested(r.importIdx, r.layerIdx, r.objIdx);
        }
    });

    // Change cursor to hand when hovering over the GDTF column
    table_->viewport()->setMouseTracking(true);
    connect(table_, &QTableWidget::entered, this, [this](const QModelIndex& idx) {
        if (patchMode_ && idx.column() == ColGdtf)
            table_->viewport()->setCursor(Qt::PointingHandCursor);
        else
            table_->viewport()->unsetCursor();
    });

    // Sync rows_ after user-triggered sort
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, [this](int, Qt::SortOrder) {
        if (!rebuilding_) syncRowsFromTable();
    });
}

void FixturesPanel::setData(const QList<MvrImportData>& imports,
                             const QList<TrackerConfig>& trackers) {
    imports_  = imports;
    trackers_ = trackers;
    rebuild();
}

void FixturesPanel::rebuild() {
    rebuilding_ = true;

    rows_.clear();
    statusLabels_.clear();
    table_->setRowCount(0);

    table_->setSortingEnabled(false);

    if (patchMode_) {
        // ── Patch mode: Name | FID | Addr | Mode | GDTF ───────────────────────
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setHorizontalHeaderLabels(
            {"FID", "Fixture", "", "Addr", "", "", "", "", "Mode", "GDTF"});

        auto* hdr = table_->horizontalHeader();
        hdr->setSectionResizeMode(ColName,  QHeaderView::Stretch);
        hdr->setSectionResizeMode(ColFid,   QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColAddr,  QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColMode,  QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColGdtf,  QHeaderView::Stretch);
        table_->setColumnWidth(ColFid,  50);
        table_->setColumnWidth(ColAddr, 80);
        table_->setColumnWidth(ColMode, 130);

        table_->setColumnHidden(ColFid,      false);
        table_->setColumnHidden(ColAddrText, true);
        table_->setColumnHidden(ColAddr,     false);
        table_->setColumnHidden(ColTracker,  true);
        table_->setColumnHidden(ColPan,      true);
        table_->setColumnHidden(ColTilt,     true);
        table_->setColumnHidden(ColStatus,   true);
        table_->setColumnHidden(ColMode,     false);
        table_->setColumnHidden(ColGdtf,     false);

        int row = 0;
        for (int ii = 0; ii < imports_.size(); ++ii) {
            for (int li = 0; li < imports_[ii].layers.size(); ++li) {
                for (int oi = 0; oi < imports_[ii].layers[li].objects.size(); ++oi) {
                    const auto& obj = imports_[ii].layers[li].objects[oi];
                    if (obj.type != MvrObjectData::Type::Fixture) continue;

                    table_->insertRow(row);

                    // Name item — stores (ii, li, oi) for post-sort row lookup
                    auto* nameItem = new QTableWidgetItem(obj.name);
                    nameItem->setData(Qt::UserRole, QVariantList{ii, li, oi});
                    table_->setItem(row, ColName, nameItem);

                    // FID
                    const QString fidStr = obj.fixtureId.isEmpty()
                        ? (obj.unitNumber > 0 ? QString::number(obj.unitNumber) : QString())
                        : obj.fixtureId;
                    const int fidSort = obj.fixtureId.toInt() > 0
                        ? obj.fixtureId.toInt() : obj.unitNumber;
                    auto* fidItem = new NumericItem(fidStr, fidSort);
                    fidItem->setTextAlignment(Qt::AlignCenter);
                    fidItem->setFlags(Qt::ItemIsEnabled);
                    table_->setItem(row, ColFid, fidItem);

                    // DmxAddrEdit
                    auto* addrEdit = new DmxAddrEdit;
                    addrEdit->setValue(qMax(1, obj.universe), qMax(1, obj.dmxAddress));
                    connect(addrEdit, &DmxAddrEdit::valueChanged, this,
                        [this, ii, li, oi](int u, int a) {
                            if (rebuilding_) return;
                            imports_[ii].layers[li].objects[oi].universe   = u;
                            imports_[ii].layers[li].objects[oi].dmxAddress = a;
                            emit dmxAddressChanged(ii, li, oi, u, a);
                        });
                    // Item provides sort key for the addr column (widget is shown on top)
                    {
                        const int sortKey = (obj.universe - 1) * 512 + obj.dmxAddress;
                        auto* addrItem = new NumericItem(
                            QString("%1.%2")
                                .arg(obj.universe, 3, 10, QChar('0'))
                                .arg(obj.dmxAddress, 3, 10, QChar('0')),
                            sortKey);
                        table_->setItem(row, ColAddr, addrItem);
                    }
                    table_->setCellWidget(row, ColAddr, addrEdit);

                    // Mode column: "ModeName (X ch)", "X ch", mode name, or "—"
                    QString modeText = "—";
                    if (obj.gdtfProfile.valid) {
                        const bool hasFp   = obj.gdtfProfile.footprint > 0;
                        const bool hasName = !obj.gdtfProfile.modeName.isEmpty();
                        if (hasName && hasFp)
                            modeText = obj.gdtfProfile.modeName
                                       + " (" + QString::number(obj.gdtfProfile.footprint) + " ch)";
                        else if (hasFp)
                            modeText = QString::number(obj.gdtfProfile.footprint) + " ch";
                        else if (hasName)
                            modeText = obj.gdtfProfile.modeName;
                    }
                    auto* modeItem = new QTableWidgetItem(modeText);
                    modeItem->setTextAlignment(Qt::AlignCenter);
                    modeItem->setFlags(Qt::ItemIsEnabled);
                    table_->setItem(row, ColMode, modeItem);

                    // GDTF column: plain item with colored text; click handled by cellClicked
                    QString gdtfText;
                    bool    gdtfWarning;
                    if (obj.gdtfProfile.valid) {
                        gdtfText    = obj.gdtfSpec.isEmpty() ? "assigned" : obj.gdtfSpec;
                        gdtfWarning = false;
                    } else if (!obj.gdtfSpec.isEmpty()) {
                        gdtfText    = obj.gdtfSpec + " (no profile)";
                        gdtfWarning = true;
                    } else {
                        gdtfText    = "(no GDTF assigned)";
                        gdtfWarning = true;
                    }

                    auto* gdtfItem = new QTableWidgetItem(gdtfText);
                    gdtfItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    gdtfItem->setToolTip(obj.gdtfSpec.isEmpty()
                        ? "Click to assign a GDTF profile"
                        : obj.gdtfSpec + "\nClick to reassign");
                    if (gdtfWarning)
                        gdtfItem->setForeground(QColor("#e8a020"));
                    // Underline to signal it's clickable
                    QFont f = gdtfItem->font();
                    f.setUnderline(true);
                    gdtfItem->setFont(f);
                    table_->setItem(row, ColGdtf, gdtfItem);

                    table_->setRowHeight(row, 26);
                    ++row;
                }
            }
        }

    } else {
        // ── Status / monitoring mode: Name | FID | Addr | Tracker | Pan | Tilt | Status
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setHorizontalHeaderLabels(
            {"FID", "Fixture", "Addr", "", "Tracker", "Pan", "Tilt", "Status", "", ""});

        auto* hdr = table_->horizontalHeader();
        hdr->setSectionResizeMode(ColName,     QHeaderView::Stretch);
        hdr->setSectionResizeMode(ColFid,      QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColAddrText, QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColTracker,  QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColPan,      QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColTilt,     QHeaderView::Fixed);
        hdr->setSectionResizeMode(ColStatus,   QHeaderView::Fixed);
        table_->setColumnWidth(ColFid,      50);
        table_->setColumnWidth(ColAddrText, 72);
        table_->setColumnWidth(ColTracker,  100);
        table_->setColumnWidth(ColPan,       90);
        table_->setColumnWidth(ColTilt,      90);
        table_->setColumnWidth(ColStatus,   110);

        table_->setColumnHidden(ColFid,      false);
        table_->setColumnHidden(ColAddrText, false);
        table_->setColumnHidden(ColAddr,     true);
        table_->setColumnHidden(ColTracker,  false);
        table_->setColumnHidden(ColPan,      false);
        table_->setColumnHidden(ColTilt,     false);
        table_->setColumnHidden(ColStatus,   false);
        table_->setColumnHidden(ColMode,     true);
        table_->setColumnHidden(ColGdtf,     true);

        int row = 0;
        for (int ii = 0; ii < imports_.size(); ++ii) {
            for (int li = 0; li < imports_[ii].layers.size(); ++li) {
                for (int oi = 0; oi < imports_[ii].layers[li].objects.size(); ++oi) {
                    const auto& obj = imports_[ii].layers[li].objects[oi];
                    if (obj.type != MvrObjectData::Type::Fixture) continue;

                    table_->insertRow(row);

                    auto makeItem = [](const QString& t) {
                        auto* it = new QTableWidgetItem(t);
                        it->setTextAlignment(Qt::AlignCenter);
                        return it;
                    };

                    // Name — stores (ii, li, oi) for post-sort sync
                    auto* nameItem = new QTableWidgetItem(obj.name);
                    nameItem->setData(Qt::UserRole, QVariantList{ii, li, oi});
                    table_->setItem(row, ColName, nameItem);

                    // FID
                    const QString fidStr = obj.fixtureId.isEmpty()
                        ? (obj.unitNumber > 0 ? QString::number(obj.unitNumber) : QString())
                        : obj.fixtureId;
                    const int fidSort = obj.fixtureId.toInt() > 0
                        ? obj.fixtureId.toInt() : obj.unitNumber;
                    auto* fidItem = new NumericItem(fidStr, fidSort);
                    fidItem->setTextAlignment(Qt::AlignCenter);
                    fidItem->setFlags(Qt::ItemIsEnabled);
                    table_->setItem(row, ColFid, fidItem);

                    table_->setItem(row, ColAddrText,
                        new NumericItem(
                            QString("%1.%2").arg(obj.universe).arg(obj.dmxAddress),
                            (obj.universe - 1) * 512 + obj.dmxAddress));

                    // Tracker column: combo box for inline assignment
                    auto* trackerCombo = new QComboBox;
                    trackerCombo->addItem(QStringLiteral("—"), -1);
                    for (const auto& t : trackers_)
                        trackerCombo->addItem(t.name, t.id);
                    for (int ci = 0; ci < trackerCombo->count(); ++ci) {
                        if (trackerCombo->itemData(ci).toInt() == obj.trackerLink) {
                            trackerCombo->setCurrentIndex(ci); break;
                        }
                    }
                    connect(trackerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this, [this, ii, li, oi](int) {
                            if (rebuilding_) return;
                            auto* combo = qobject_cast<QComboBox*>(sender());
                            if (!combo) return;
                            const int trackerId = combo->currentData().toInt();
                            imports_[ii].layers[li].objects[oi].trackerLink = trackerId;
                            emit trackerLinkChanged(ii, li, oi, trackerId);
                            applyStatus();
                        });
                    // Underlying item used as sort key
                    {
                        const QString name = obj.trackerLink >= 0
                            ? [&]() -> QString {
                                for (const auto& t : trackers_)
                                    if (t.id == obj.trackerLink) return t.name;
                                return QString("T%1").arg(obj.trackerLink);
                            }() : QString("—");
                        auto* sortItem = new QTableWidgetItem(name);
                        sortItem->setFlags(Qt::ItemIsEnabled);
                        table_->setItem(row, ColTracker, sortItem);
                    }
                    table_->setCellWidget(row, ColTracker, trackerCombo);

                    table_->setItem(row, ColPan,  makeItem("—"));
                    table_->setItem(row, ColTilt, makeItem("—"));
                    table_->setRowHeight(row, 26);

                    // Status column: QLabel with optional clickable link
                    auto* lbl = new QLabel;
                    lbl->setAlignment(Qt::AlignCenter);
                    lbl->setTextFormat(Qt::RichText);
                    lbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
                    lbl->setOpenExternalLinks(false);
                    connect(lbl, &QLabel::linkActivated, this,
                        [this, ii, li, oi](const QString&) {
                            emit gdtfAssignRequested(ii, li, oi);
                        });
                    table_->setCellWidget(row, ColStatus, lbl);

                    ++row;
                }
            }
        }
    }

    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSortIndicator(ColFid, Qt::AscendingOrder);

    rebuilding_ = false;

    syncRowsFromTable();

    const bool hasRows = !rows_.isEmpty();
    emptyLabel_->setVisible(!hasRows);
    toggleBar_->setVisible(hasRows);
    table_->setVisible(hasRows);

    if (!patchMode_) applyStatus();
}

void FixturesPanel::updateStatus(const QMap<QString, FixtureStatus>& statusByKey) {
    lastStatus_ = statusByKey;
    if (!patchMode_) applyStatus();
}

void FixturesPanel::applyStatus() {
    if (patchMode_) return;

    for (int row = 0; row < rows_.size(); ++row) {
        const auto& r   = rows_[row];
        auto*       lbl = statusLabels_[row];
        auto        it  = lastStatus_.find(r.key);

        const bool linked  = obj_linkedToTracker(r);
        const bool hasGdtf = obj_hasGdtf(r);

        auto setCell = [&](int col, const QString& text, const QColor& fg = {}) {
            auto* item = table_->item(row, col);
            if (!item) return;
            item->setText(text);
            if (fg.isValid())
                item->setForeground(fg);
            else
                item->setData(Qt::ForegroundRole, QVariant());
        };

        const bool active = (it != lastStatus_.end() && it->active);

        if (!active) {
            setCell(ColPan,  "—");
            setCell(ColTilt, "—");
        } else {
            const auto& s = *it;
            if (displayMode_ == DisplayMode::Physical) {
                setCell(ColPan,  QString::number(double(s.panDeg),  'f', 1) + "°");
                setCell(ColTilt, QString::number(double(s.tiltDeg), 'f', 1) + "°");
            } else {
                setCell(ColPan,  hasGdtf
                    ? QString("%1 / %2").arg(s.panDmx  >> 8).arg(s.panDmx  & 0xFF) : "—");
                setCell(ColTilt, hasGdtf
                    ? QString("%1 / %2").arg(s.tiltDmx >> 8).arg(s.tiltDmx & 0xFF) : "—");
            }
        }

        if (lbl) {
            if (!linked) {
                lbl->setText("—");
            } else if (!hasGdtf) {
                lbl->setText("<a href='assign' style='color:#e8a020; text-decoration:none;'>"
                             "⚠ Assign GDTF</a>");
            } else if (!active) {
                lbl->setText("Waiting");
            } else {
                lbl->setText("Active");
            }
        }
    }
}

void FixturesPanel::syncRowsFromTable() {
    rows_.clear();
    statusLabels_.clear();
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* nameItem = table_->item(row, ColName);
        if (!nameItem) continue;
        const QVariantList data = nameItem->data(Qt::UserRole).toList();
        if (data.size() < 3) continue;
        Row r;
        r.importIdx = data[0].toInt();
        r.layerIdx  = data[1].toInt();
        r.objIdx    = data[2].toInt();
        r.key       = QString("%1-%2-%3").arg(r.importIdx).arg(r.layerIdx).arg(r.objIdx);
        rows_.append(r);
        statusLabels_.append(qobject_cast<QLabel*>(table_->cellWidget(row, ColStatus)));
    }
}

bool FixturesPanel::obj_linkedToTracker(const Row& r) const {
    if (r.importIdx >= imports_.size()) return false;
    const auto& layers = imports_[r.importIdx].layers;
    if (r.layerIdx >= layers.size()) return false;
    if (r.objIdx >= layers[r.layerIdx].objects.size()) return false;
    return layers[r.layerIdx].objects[r.objIdx].trackerLink >= 0;
}

bool FixturesPanel::obj_hasGdtf(const Row& r) const {
    if (r.importIdx >= imports_.size()) return false;
    const auto& layers = imports_[r.importIdx].layers;
    if (r.layerIdx >= layers.size()) return false;
    if (r.objIdx >= layers[r.layerIdx].objects.size()) return false;
    return layers[r.layerIdx].objects[r.objIdx].gdtfProfile.valid;
}
