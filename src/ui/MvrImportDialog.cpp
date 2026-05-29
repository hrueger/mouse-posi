#include "MvrImportDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLabel>

MvrImportDialog::MvrImportDialog(const QList<MvrLayer>& layers, QWidget* parent)
    : QDialog(parent), layers_(layers)
{
    setWindowTitle(QStringLiteral("Import MVR"));
    setMinimumSize(420, 480);

    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setRootIsDecorated(true);
    tree_->setExpandsOnDoubleClick(true);

    buildTree(layers_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Select layers and objects to import:")));
    layout->addWidget(tree_, 1);
    layout->addWidget(buttons);

    connect(tree_, &QTreeWidget::itemChanged,
            this, &MvrImportDialog::onItemChanged);
}

void MvrImportDialog::buildTree(const QList<MvrLayer>& layers)
{
    updatingTree_ = true;
    tree_->clear();

    for (int li = 0; li < layers.size(); ++li) {
        const MvrLayer& layer = layers[li];

        auto* layerItem = new QTreeWidgetItem(tree_);
        layerItem->setText(0, layer.name.isEmpty()
                              ? QStringLiteral("(unnamed layer)") : layer.name);
        layerItem->setText(1, QStringLiteral("Layer"));
        layerItem->setData(0, Qt::UserRole, li);      // layer index
        layerItem->setData(0, Qt::UserRole + 1, -1);  // object index (-1 = layer)
        layerItem->setCheckState(0, layer.enabled ? Qt::Checked : Qt::Unchecked);
        layerItem->setExpanded(true);

        for (int oi = 0; oi < layer.objects.size(); ++oi) {
            const MvrObject& obj = layer.objects[oi];

            auto* objItem = new QTreeWidgetItem(layerItem);
            objItem->setText(0, obj.name.isEmpty()
                                ? QStringLiteral("(unnamed)") : obj.name);

            QString typeStr;
            switch (obj.type) {
                case MvrObject::Type::Fixture:     typeStr = QStringLiteral("Fixture");     break;
                case MvrObject::Type::SceneObject: typeStr = QStringLiteral("SceneObject"); break;
                case MvrObject::Type::Truss:       typeStr = QStringLiteral("Truss");       break;
                case MvrObject::Type::Group:       typeStr = QStringLiteral("Group");       break;
                default:                           typeStr = QStringLiteral("?");           break;
            }
            objItem->setText(1, typeStr);
            objItem->setData(0, Qt::UserRole,     li);
            objItem->setData(0, Qt::UserRole + 1, oi);
            objItem->setCheckState(0, obj.enabled ? Qt::Checked : Qt::Unchecked);
        }
    }
    updatingTree_ = false;
}

void MvrImportDialog::onItemChanged(QTreeWidgetItem* item, int col)
{
    if (updatingTree_ || col != 0) return;

    const int li = item->data(0, Qt::UserRole).toInt();
    const int oi = item->data(0, Qt::UserRole + 1).toInt();
    const bool checked = (item->checkState(0) == Qt::Checked);

    if (oi < 0) {
        // Layer item: update layer enabled + propagate to children
        layers_[li].enabled = checked;
        updatingTree_ = true;
        for (int c = 0; c < item->childCount(); ++c) {
            item->child(c)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            layers_[li].objects[c].enabled = checked;
        }
        updatingTree_ = false;
    } else {
        layers_[li].objects[oi].enabled = checked;
        // Update parent check state to reflect children
        QTreeWidgetItem* parent = item->parent();
        if (parent) {
            int checkedCount = 0;
            for (int c = 0; c < parent->childCount(); ++c)
                if (parent->child(c)->checkState(0) == Qt::Checked) ++checkedCount;
            updatingTree_ = true;
            if (checkedCount == 0)
                parent->setCheckState(0, Qt::Unchecked);
            else if (checkedCount == parent->childCount())
                parent->setCheckState(0, Qt::Checked);
            else
                parent->setCheckState(0, Qt::PartiallyChecked);
            updatingTree_ = false;
        }
    }
}

QList<MvrLayer> MvrImportDialog::selectedLayers() const
{
    return layers_;
}
