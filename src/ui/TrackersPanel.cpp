#include "TrackersPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QSet>
#include <algorithm>

// Paints the tracker color directly — bypasses theme overrides on setBackground().
class ColorCellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QColor c = idx.data(Qt::UserRole).value<QColor>();
        if (c.isValid()) p->fillRect(opt.rect, c);
    }
};

static const QColor kTrackerPalette[] = {
    {255,80,80}, {80,200,80}, {80,140,255},
    {255,200,60}, {200,80,255}, {60,220,220},
    {255,140,40}, {180,255,80}, {255,80,200}
};

TrackersPanel::TrackersPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    table_ = new QTableWidget(0, COL_PEERS);
    table_->setHorizontalHeaderLabels({"ID", "Name", "Color"});
    table_->horizontalHeader()->setSectionResizeMode(COL_ID,    QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(COL_NAME,  QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(COL_COLOR, QHeaderView::Fixed);
    table_->setColumnWidth(COL_ID,    36);
    table_->setColumnWidth(COL_COLOR, 50);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setItemDelegateForColumn(COL_COLOR, new ColorCellDelegate(table_));
    layout->addWidget(table_);

    auto* btnRow = new QHBoxLayout;
    addBtn_    = new QPushButton("+");
    removeBtn_ = new QPushButton("−");
    addBtn_->setFixedHeight(26);
    removeBtn_->setFixedHeight(26);
    addBtn_->setToolTip("Add tracker");
    removeBtn_->setToolTip("Remove selected tracker");
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(addBtn_,    &QPushButton::clicked,         this, &TrackersPanel::onAddTracker);
    connect(removeBtn_, &QPushButton::clicked,         this, &TrackersPanel::onRemoveTracker);
    connect(table_,     &QTableWidget::cellDoubleClicked, this, &TrackersPanel::onCellDoubleClicked);
    connect(table_,     &QTableWidget::itemChanged,    this, &TrackersPanel::onItemChanged);
}

void TrackersPanel::setTrackers(const QList<TrackerConfig>& trackers) {
    trackers_ = trackers;
    rebuildTable();
}

void TrackersPanel::setActiveTrackerId(int id) {
    activeId_ = id;
    for (int row = 0; row < table_->rowCount(); ++row)
        updateRowStyle(row);
}

QColor TrackersPanel::activeColor() const {
    for (const auto& t : trackers_)
        if (t.id == activeId_) return t.color;
    return Qt::white;
}

void TrackersPanel::setCalibrationActive(bool on) {
    calibActive_ = on;
    table_->setSelectionMode(on ? QAbstractItemView::NoSelection
                                : QAbstractItemView::SingleSelection);
    if (on) table_->clearSelection();
}

void TrackersPanel::setSessionContext(bool isAdminOrHost,
                                       const QStringList& peerNames,
                                       const QMap<QString, QList<int>>& peerAssignments)
{
    isAdminOrHost_   = isAdminOrHost;
    peerNames_       = peerNames;
    peerAssignments_ = peerAssignments;
    rebuildTable();
}

void TrackersPanel::rebuildTable() {
    updatingTable_ = true;

    // Column layout: fixed cols + (Host + N peers) when admin
    int peerCols  = isAdminOrHost_ ? (1 + peerNames_.size()) : 0;
    int totalCols = COL_PEERS + peerCols;

    table_->setRowCount(0);
    table_->setColumnCount(totalCols);

    // Headers
    QStringList headers = {"ID", "Name", "Color"};
    if (isAdminOrHost_) {
        headers << "Host";
        headers += peerNames_;
    }
    table_->setHorizontalHeaderLabels(headers);

    // Resize modes (re-apply after column count change)
    table_->horizontalHeader()->setSectionResizeMode(COL_ID,    QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(COL_NAME,  QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(COL_COLOR, QHeaderView::Fixed);
    table_->setColumnWidth(COL_ID,    36);
    table_->setColumnWidth(COL_COLOR, 50);
    if (isAdminOrHost_) {
        // Host column — compact fixed width
        table_->horizontalHeader()->setSectionResizeMode(COL_PEERS, QHeaderView::Fixed);
        table_->setColumnWidth(COL_PEERS, 46);
        for (int c = COL_PEERS + 1; c < totalCols; ++c)
            table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }

    table_->setRowCount(trackers_.size());

    for (int row = 0; row < trackers_.size(); ++row) {
        const auto& t = trackers_[row];

        auto* idItem = new QTableWidgetItem(QString::number(t.id));
        idItem->setTextAlignment(Qt::AlignCenter);
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(row, COL_ID, idItem);

        auto* nameItem = new QTableWidgetItem(t.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(row, COL_NAME, nameItem);

        auto* colorItem = new QTableWidgetItem;
        colorItem->setData(Qt::UserRole, t.color);  // painted by ColorCellDelegate
        colorItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(row, COL_COLOR, colorItem);

        if (isAdminOrHost_) {
            // Host column — always-on indicator, not user-toggleable
            auto* hostItem = new QTableWidgetItem;
            hostItem->setFlags(Qt::ItemIsUserCheckable); // no IsEnabled → greyed look
            hostItem->setCheckState(Qt::Checked);
            table_->setItem(row, COL_PEERS, hostItem);

            // One column per peer
            for (int pi = 0; pi < peerNames_.size(); ++pi) {
                const QString& name = peerNames_[pi];
                QList<int> ids = peerAssignments_.value(name);
                auto* peerItem = new QTableWidgetItem;
                peerItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                peerItem->setCheckState(ids.contains(t.id) ? Qt::Checked : Qt::Unchecked);
                table_->setItem(row, COL_PEERS + 1 + pi, peerItem);
            }
        }

        updateRowStyle(row);
    }

    updatingTable_ = false;
}

void TrackersPanel::updateRowStyle(int row) {
    if (row < 0 || row >= trackers_.size()) return;
    bool active = (trackers_[row].id == activeId_);
    QFont f;
    f.setBold(active);
    QVariant bg = active ? QVariant(QColor(50, 80, 50)) : QVariant();

    for (int col : {COL_ID, COL_NAME}) {
        auto* item = table_->item(row, col);
        if (!item) continue;
        item->setFont(f);
        item->setData(Qt::BackgroundRole, bg);
    }
    // COL_COLOR is painted by ColorCellDelegate — no item styling needed
}

void TrackersPanel::onAddTracker() {
    QSet<int> used;
    for (const auto& t : trackers_) used.insert(t.id);
    int newId = 1;
    while (used.contains(newId) && newId <= 9) ++newId;
    if (newId > 9) {
        QMessageBox::information(this, "No IDs left", "All tracker IDs 1–9 are in use.");
        return;
    }
    TrackerConfig t;
    t.id    = newId;
    t.name  = QString("Spot %1").arg(newId);
    t.color = kTrackerPalette[(newId - 1) % 9];
    trackers_.append(t);
    std::sort(trackers_.begin(), trackers_.end(),
              [](const TrackerConfig& a, const TrackerConfig& b){ return a.id < b.id; });
    rebuildTable();
    emit trackersChanged(trackers_);
}

void TrackersPanel::onRemoveTracker() {
    int row = table_->currentRow();
    if (row < 0 || row >= trackers_.size()) return;
    if (trackers_.size() == 1) {
        QMessageBox::information(this, "Cannot remove", "At least one tracker is required.");
        return;
    }
    trackers_.removeAt(row);
    rebuildTable();
    emit trackersChanged(trackers_);
}

void TrackersPanel::onCellDoubleClicked(int row, int col) {
    if (row < 0 || row >= trackers_.size()) return;
    TrackerConfig& t = trackers_[row];

    if (col == COL_ID) {
        bool ok;
        int newId = QInputDialog::getInt(this, "Edit ID",
            "Tracker ID (1–9):", t.id, 1, 9, 1, &ok);
        if (!ok || newId == t.id) return;
        for (int i = 0; i < trackers_.size(); ++i) {
            if (i != row && trackers_[i].id == newId) {
                QMessageBox::warning(this, "Duplicate ID",
                    QString("ID %1 is already used by \"%2\".").arg(newId).arg(trackers_[i].name));
                return;
            }
        }
        t.id = newId;
        std::sort(trackers_.begin(), trackers_.end(),
                  [](const TrackerConfig& a, const TrackerConfig& b){ return a.id < b.id; });
        rebuildTable();
        emit trackersChanged(trackers_);
    }
    else if (col == COL_NAME) {
        bool ok;
        QString newName = QInputDialog::getText(this, "Edit Name",
            "Tracker name:", QLineEdit::Normal, t.name, &ok);
        if (!ok || newName == t.name) return;
        t.name = newName.trimmed().isEmpty() ? QString("Tracker %1").arg(t.id) : newName.trimmed();
        rebuildTable();
        emit trackersChanged(trackers_);
    }
    else if (col == COL_COLOR) {
        QColor c = QColorDialog::getColor(t.color, this, "Choose Tracker Color");
        if (!c.isValid() || c == t.color) return;
        t.color = c;
        rebuildTable();
        emit trackersChanged(trackers_);
    }
}

void TrackersPanel::onItemChanged(QTableWidgetItem* item) {
    if (updatingTable_) return;

    int row = table_->row(item);
    int col = table_->column(item);

    // Only peer checkbox columns are user-editable here
    if (!isAdminOrHost_ || col < COL_PEERS + 1 || row < 0 || row >= trackers_.size()) return;

    int peerIdx = col - (COL_PEERS + 1);
    if (peerIdx < 0 || peerIdx >= peerNames_.size()) return;

    const QString& peerName = peerNames_[peerIdx];
    int trackerId = trackers_[row].id;

    QList<int> ids = peerAssignments_.value(peerName);
    bool nowChecked = (item->checkState() == Qt::Checked);
    bool wasChecked = ids.contains(trackerId);
    if (nowChecked == wasChecked) return;

    if (nowChecked) ids.append(trackerId);
    else ids.removeAll(trackerId);
    peerAssignments_[peerName] = ids;

    emit trackerAccessChanged(peerName, ids);
}
