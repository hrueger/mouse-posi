#include "StageItemsPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QToolButton>
#include <QMenu>

StageItemsPanel::StageItemsPanel(QWidget* parent) : QWidget(parent)
{
    objectTable_ = new QTableWidget;
    objectTable_->setColumnCount(3);
    objectTable_->setHorizontalHeaderLabels({"Name", "Vid", "3D"});
    objectTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    objectTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    objectTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    objectTable_->setColumnWidth(1, 36);
    objectTable_->setColumnWidth(2, 36);
    objectTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    objectTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    objectTable_->verticalHeader()->setVisible(false);
    objectTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objectTable_->setShowGrid(false);
    objectTable_->setAlternatingRowColors(true);

    addRectBtn_    = new QToolButton; addRectBtn_->setText("+ Rect");
    addPolyBtn_    = new QToolButton; addPolyBtn_->setText("+ Polygon");
    addOutlineBtn_ = new QToolButton; addOutlineBtn_->setText("+ Outline");
    deleteBtn_     = new QToolButton; deleteBtn_->setText("Delete");
    deleteBtn_->setEnabled(false);

    auto* listBtns = new QHBoxLayout;
    listBtns->setContentsMargins(0, 0, 0, 0);
    listBtns->addWidget(addRectBtn_);
    listBtns->addWidget(addPolyBtn_);
    listBtns->addWidget(addOutlineBtn_);
    listBtns->addWidget(deleteBtn_);
    listBtns->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(objectTable_, 1);
    layout->addLayout(listBtns);

    connect(addRectBtn_,    &QToolButton::clicked, this, &StageItemsPanel::addRectRequested);
    connect(addPolyBtn_,    &QToolButton::clicked, this, &StageItemsPanel::addPolygonRequested);
    connect(addOutlineBtn_, &QToolButton::clicked, this, &StageItemsPanel::addStageOutlineRequested);
    connect(deleteBtn_,     &QToolButton::clicked, this, [this]() {
        if (selectedId_ >= 0) emit deleteRequested(selectedId_);
    });

    connect(objectTable_, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { onTableRowChanged(row); });
    connect(objectTable_, &QTableWidget::itemChanged,
            this, &StageItemsPanel::onTableItemChanged);

    objectTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objectTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* item = objectTable_->itemAt(pos);
        if (!item) return;
        int id = item->data(Qt::UserRole).toInt();
        if (id < 0) return;  // system objects not in context menu

        QMenu menu(this);
        menu.addAction("Duplicate", [this, id]{ emit duplicateRequested(id); });
        menu.addSeparator();
        menu.addAction("Delete", [this, id]{ emit deleteRequested(id); });
        menu.exec(objectTable_->viewport()->mapToGlobal(pos));
    });
}

void StageItemsPanel::setAllObjects(const QList<StageObject>& all)
{
    objects_ = all;
    rebuildTable();
}

void StageItemsPanel::setSelectedObject(int id)
{
    selectedId_ = id;
    updateButtonStates();

    updatingTable_ = true;
    bool found = false;
    for (int row = 0; row < objectTable_->rowCount(); ++row) {
        auto* item = objectTable_->item(row, 0);
        if (item && item->data(Qt::UserRole).toInt() == id) {
            objectTable_->selectRow(row);
            found = true;
            break;
        }
    }
    if (!found) objectTable_->clearSelection();
    updatingTable_ = false;
}

void StageItemsPanel::onTableRowChanged(int row)
{
    if (updatingTable_) return;
    auto* item = objectTable_->item(row, 0);
    int id = item ? item->data(Qt::UserRole).toInt() : -999;
    selectedId_ = id;
    updateButtonStates();
    if (id != -999) emit selectionChanged(id);
}

void StageItemsPanel::onTableItemChanged(QTableWidgetItem* item)
{
    if (updatingTable_) return;
    const int col = item->column();
    if (col != 1 && col != 2) return;

    int id = item->data(Qt::UserRole).toInt();
    bool checked = (item->checkState() == Qt::Checked);

    for (auto& obj : objects_) {
        if (obj.id != id) continue;
        if (col == 1) obj.visibleInVideo = checked;
        else          obj.visibleIn3D    = checked;
        emit visibilityChanged(id, obj.visibleInVideo, obj.visibleIn3D);
        return;
    }
}

void StageItemsPanel::rebuildTable()
{
    int prevId = selectedId_;
    updatingTable_ = true;
    objectTable_->setRowCount(0);

    for (const auto& obj : objects_) {
        const int row = objectTable_->rowCount();
        objectTable_->insertRow(row);
        objectTable_->setRowHeight(row, 22);

        bool isSystem = (obj.id < 0);

        auto* nameItem = new QTableWidgetItem(obj.name);
        nameItem->setData(Qt::UserRole, obj.id);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (isSystem)
            nameItem->setForeground(QColor(160, 160, 160));
        objectTable_->setItem(row, 0, nameItem);

        auto* vidItem = new QTableWidgetItem;
        vidItem->setData(Qt::UserRole, obj.id);
        bool videoEditable = (obj.id != -1);
        if (videoEditable) {
            vidItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            vidItem->setCheckState(obj.visibleInVideo ? Qt::Checked : Qt::Unchecked);
        } else {
            vidItem->setFlags(Qt::ItemIsSelectable);
            vidItem->setCheckState(Qt::Unchecked);
            vidItem->setForeground(QColor(80, 80, 80));
        }
        objectTable_->setItem(row, 1, vidItem);

        auto* tdItem = new QTableWidgetItem;
        tdItem->setData(Qt::UserRole, obj.id);
        tdItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        tdItem->setCheckState(obj.visibleIn3D ? Qt::Checked : Qt::Unchecked);
        objectTable_->setItem(row, 2, tdItem);

        if (obj.id == prevId)
            objectTable_->selectRow(row);
    }

    updatingTable_ = false;
    updateButtonStates();
}

void StageItemsPanel::updateButtonStates()
{
    deleteBtn_->setEnabled(selectedId_ >= 0);
}
