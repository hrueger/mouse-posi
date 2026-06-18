#include "StageItemsPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QToolButton>
#include <QMenu>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>

static QIcon crosshairIcon()
{
    static QIcon icon;
    if (!icon.isNull()) return icon;
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor blue(80, 160, 255);
    const float cx = 7.0f, cy = 7.0f, r = 2.5f, gap = r + 1.2f;
    // Four arms from gap to edge
    p.setPen(QPen(blue, 1.5f, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(0,   cy), QPointF(cx - gap, cy));
    p.drawLine(QPointF(14,  cy), QPointF(cx + gap, cy));
    p.drawLine(QPointF(cx, 0),   QPointF(cx, cy - gap));
    p.drawLine(QPointF(cx, 14),  QPointF(cx, cy + gap));
    // Centre circle
    p.setPen(QPen(blue, 1.2f));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), r, r);
    p.end();
    icon = QIcon(pm);
    return icon;
}

static QIcon fixtureIcon()
{
    static QIcon icon;
    if (!icon.isNull()) return icon;
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // body
    p.setPen(QPen(QColor(180, 110, 0), 0.8));
    p.setBrush(QColor(255, 185, 40));
    p.drawEllipse(QRectF(1.5, 1.5, 11, 11));
    // lens highlight
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 240, 170, 190));
    p.drawEllipse(QRectF(4, 3, 4, 4));
    p.end();
    icon = QIcon(pm);
    return icon;
}

static constexpr int COL_NAME = 0;
static constexpr int COL_TYPE = 1;
static constexpr int COL_VID  = 2;
static constexpr int COL_3D   = 3;

StageItemsPanel::StageItemsPanel(QWidget* parent) : QWidget(parent)
{
    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText("Filter by name or type (e.g. \"Fixture\")…");
    filterEdit_->setClearButtonEnabled(true);

    tree_ = new QTreeWidget;
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                            QStringLiteral("Vid"), QStringLiteral("3D")});
    tree_->header()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(COL_TYPE, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(COL_VID,  QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(COL_3D,   QHeaderView::Fixed);
    tree_->setColumnWidth(COL_TYPE, 90);
    tree_->setColumnWidth(COL_VID, 36);
    tree_->setColumnWidth(COL_3D,  36);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    tree_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    tree_->setRootIsDecorated(true);
    tree_->setExpandsOnDoubleClick(false);  // we handle rename on double-click manually
    tree_->setIndentation(16);

    addRectBtn_    = new QToolButton; addRectBtn_->setText(QStringLiteral("+ Rect"));
    addPolyBtn_    = new QToolButton; addPolyBtn_->setText(QStringLiteral("+ Polygon"));
    addOutlineBtn_ = new QToolButton; addOutlineBtn_->setText(QStringLiteral("+ Outline"));
    deleteBtn_     = new QToolButton; deleteBtn_->setText(QStringLiteral("Delete"));
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
    layout->addWidget(filterEdit_);
    layout->addWidget(tree_, 1);
    layout->addLayout(listBtns);

    connect(filterEdit_, &QLineEdit::textChanged, this, &StageItemsPanel::applyFilter);
    connect(addRectBtn_,    &QToolButton::clicked, this, &StageItemsPanel::addRectRequested);
    connect(addPolyBtn_,    &QToolButton::clicked, this, &StageItemsPanel::addPolygonRequested);
    connect(addOutlineBtn_, &QToolButton::clicked, this, &StageItemsPanel::addStageOutlineRequested);
    connect(deleteBtn_,     &QToolButton::clicked, this, [this]() {
        if (selectedId_ >= 0) emit deleteRequested(selectedId_);
    });

    connect(tree_, &QTreeWidget::itemSelectionChanged,
            this, &StageItemsPanel::onItemSelectionChanged);
    connect(tree_, &QTreeWidget::itemChanged,
            this, &StageItemsPanel::onItemChanged);
    connect(tree_, &QTreeWidget::itemDoubleClicked,
            this, &StageItemsPanel::onItemDoubleClicked);

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* item = tree_->itemAt(pos);
        if (!item) return;
        const QString kind = item->data(COL_NAME, RoleKind).toString();

        if (kind == QLatin1String("stage")) {
            const int id = item->data(COL_NAME, RoleId).toInt();
            if (id < 0) return;
            QMenu menu(this);
            menu.addAction(QStringLiteral("Rename"), [this, item]{
                tree_->editItem(item, COL_NAME);
            });
            menu.addAction(QStringLiteral("Duplicate"), [this, id]{ emit duplicateRequested(id); });
            menu.addSeparator();
            menu.addAction(QStringLiteral("Delete"), [this, id]{ emit deleteRequested(id); });
            menu.exec(tree_->viewport()->mapToGlobal(pos));

        } else if (kind == QLatin1String("mvr-root")) {
            const int ii = item->data(COL_NAME, RoleMvrImport).toInt();
            QMenu menu(this);
            menu.addAction(QStringLiteral("Rename"), [this, item]{
                tree_->editItem(item, COL_NAME);
            });
            menu.addSeparator();
            menu.addAction(QStringLiteral("Delete"), [this, ii]{ emit mvrImportDeleteRequested(ii); });
            menu.exec(tree_->viewport()->mapToGlobal(pos));
        }
    });
}

void StageItemsPanel::setAllObjects(const QList<StageObject>& all)
{
    objects_ = all;
    rebuildTree();
}

void StageItemsPanel::setMvrImports(const QList<MvrImport>& imports)
{
    mvrImports_ = imports;
    rebuildTree();
}

void StageItemsPanel::setOriginVisibility(bool stageOriginIn3D, bool psnOriginIn3D)
{
    stageOriginVisible_ = stageOriginIn3D;
    psnOriginVisible_   = psnOriginIn3D;
    rebuildTree();
}

void StageItemsPanel::setSelectedObject(int id)
{
    selectedId_ = id;
    updateButtonStates();

    updatingTree_ = true;
    QTreeWidgetItemIterator it(tree_);
    bool found = false;
    while (*it) {
        if ((*it)->data(COL_NAME, RoleKind) == QLatin1String("stage") &&
            (*it)->data(COL_NAME, RoleId).toInt() == id) {
            tree_->setCurrentItem(*it);
            found = true;
            break;
        }
        ++it;
    }
    if (!found) tree_->clearSelection();
    updatingTree_ = false;
}

void StageItemsPanel::onItemSelectionChanged()
{
    if (updatingTree_) return;
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!cur) { selectedId_ = -999; updateButtonStates(); return; }

    const QString kind = cur->data(COL_NAME, RoleKind).toString();
    if (kind == QLatin1String("stage")) {
        selectedId_ = cur->data(COL_NAME, RoleId).toInt();
        updateButtonStates();
        if (selectedId_ != -999) emit selectionChanged(selectedId_);
    } else if (kind == QLatin1String("mvr-root")) {
        selectedId_ = -999;
        updateButtonStates();
        emit mvrImportSelected(cur->data(COL_NAME, RoleMvrImport).toInt());
    } else if (kind == QLatin1String("mvr-obj")) {
        selectedId_ = -999;
        updateButtonStates();
        emit mvrChildItemSelected();
        const int importIdx = cur->data(COL_NAME, RoleMvrImport).toInt();
        const int layerIdx  = cur->data(COL_NAME, RoleMvrLayer).toInt();
        const int objIdx    = cur->data(COL_NAME, RoleMvrObj).toInt();
        if (importIdx >= 0 && layerIdx >= 0 && objIdx >= 0)
            emit mvrFixtureSelected(importIdx, layerIdx, objIdx);
    } else {
        selectedId_ = -999;
        updateButtonStates();
        emit mvrChildItemSelected();
    }
}

void StageItemsPanel::onItemDoubleClicked(QTreeWidgetItem* item, int col)
{
    if (col != COL_NAME) return;
    const QString kind = item->data(COL_NAME, RoleKind).toString();
    if (kind == QLatin1String("mvr-root") ||
        (kind == QLatin1String("stage") && item->data(COL_NAME, RoleId).toInt() >= 0))
        tree_->editItem(item, COL_NAME);
}

void StageItemsPanel::onItemChanged(QTreeWidgetItem* item, int col)
{
    if (updatingTree_) return;
    const QString kind = item->data(COL_NAME, RoleKind).toString();

    // MVR root renamed
    if (kind == QLatin1String("mvr-root") && col == COL_NAME) {
        const int ii = item->data(COL_NAME, RoleMvrImport).toInt();
        if (ii < 0 || ii >= mvrImports_.size()) return;
        const QString newName = item->text(COL_NAME).trimmed();
        if (!newName.isEmpty() && newName != mvrImports_[ii].name) {
            mvrImports_[ii].name = newName;
            emit mvrImportRenamed(ii, newName);
        }
        return;
    }

    // MVR root visibility toggled
    if (kind == QLatin1String("mvr-root") && col == COL_3D) {
        const int ii = item->data(COL_NAME, RoleMvrImport).toInt();
        if (ii < 0 || ii >= mvrImports_.size()) return;
        const bool checked = (item->checkState(COL_3D) == Qt::Checked);
        mvrImports_[ii].enabled = checked;
        // Cascade to all layers and objects
        updatingTree_ = true;
        for (int li = 0; li < mvrImports_[ii].layers.size(); ++li) {
            auto* layerItem = item->child(li);
            if (!layerItem) continue;
            layerItem->setCheckState(COL_3D, checked ? Qt::Checked : Qt::Unchecked);
            mvrImports_[ii].layers[li].enabled = checked;
            for (int oi = 0; oi < mvrImports_[ii].layers[li].objects.size(); ++oi) {
                auto* objItem = layerItem->child(oi);
                if (!objItem) continue;
                objItem->setCheckState(COL_3D, checked ? Qt::Checked : Qt::Unchecked);
                mvrImports_[ii].layers[li].objects[oi].enabled = checked;
            }
        }
        updatingTree_ = false;
        emit mvrVisibilityChanged(ii, -1, -1, checked);
        return;
    }

    if (kind == QLatin1String("stage")) {
        const int id = item->data(COL_NAME, RoleId).toInt();

        // Origin system items (-20 = stage, -21 = PSN) — only 3D checkbox
        if (id == -20 || id == -21) {
            if (col != COL_3D) return;
            const bool checked = (item->checkState(COL_3D) == Qt::Checked);
            if (id == -20) stageOriginVisible_ = checked;
            else           psnOriginVisible_   = checked;
            emit visibilityChanged(id, false, checked);
            return;
        }

        if (col == COL_NAME && id >= 0) {
            const QString newName = item->text(COL_NAME).trimmed();
            if (!newName.isEmpty()) {
                for (auto& obj : objects_)
                    if (obj.id == id) { obj.name = newName; break; }
                emit objectRenamed(id, newName);
            }
            return;
        }
        if (col != COL_VID && col != COL_3D) return;
        const bool checked = (item->checkState(col) == Qt::Checked);
        for (auto& obj : objects_) {
            if (obj.id != id) continue;
            if (col == COL_VID) obj.visibleInVideo = checked;
            else                obj.visibleIn3D    = checked;
            emit visibilityChanged(id, obj.visibleInVideo, obj.visibleIn3D);
            return;
        }

    } else if (kind == QLatin1String("mvr-layer")) {
        if (col != COL_3D) return;
        const int ii = item->data(COL_NAME, RoleMvrImport).toInt();
        const int li = item->data(COL_NAME, RoleMvrLayer).toInt();
        if (ii < 0 || ii >= mvrImports_.size()) return;
        if (li < 0 || li >= mvrImports_[ii].layers.size()) return;
        const bool checked = (item->checkState(COL_3D) == Qt::Checked);
        mvrImports_[ii].layers[li].enabled = checked;
        updatingTree_ = true;
        for (int c = 0; c < item->childCount(); ++c) {
            item->child(c)->setCheckState(COL_3D, checked ? Qt::Checked : Qt::Unchecked);
            mvrImports_[ii].layers[li].objects[c].enabled = checked;
        }
        updatingTree_ = false;
        emit mvrVisibilityChanged(ii, li, -1, checked);

    } else if (kind == QLatin1String("mvr-obj")) {
        if (col != COL_3D) return;
        const int ii = item->data(COL_NAME, RoleMvrImport).toInt();
        const int li = item->data(COL_NAME, RoleMvrLayer).toInt();
        const int oi = item->data(COL_NAME, RoleMvrObj).toInt();
        if (ii < 0 || ii >= mvrImports_.size()) return;
        if (li < 0 || li >= mvrImports_[ii].layers.size()) return;
        if (oi < 0 || oi >= mvrImports_[ii].layers[li].objects.size()) return;
        const bool checked = (item->checkState(COL_3D) == Qt::Checked);
        mvrImports_[ii].layers[li].objects[oi].enabled = checked;
        QTreeWidgetItem* parent = item->parent();
        if (parent) {
            int cnt = 0;
            for (int c = 0; c < parent->childCount(); ++c)
                if (parent->child(c)->checkState(COL_3D) == Qt::Checked) ++cnt;
            updatingTree_ = true;
            if (cnt == 0)
                parent->setCheckState(COL_3D, Qt::Unchecked);
            else if (cnt == parent->childCount())
                parent->setCheckState(COL_3D, Qt::Checked);
            else
                parent->setCheckState(COL_3D, Qt::PartiallyChecked);
            updatingTree_ = false;
        }
        emit mvrVisibilityChanged(ii, li, oi, checked);
    }
}

void StageItemsPanel::rebuildTree()
{
    updatingTree_ = true;
    const int prevId = selectedId_;
    tree_->clear();

    // ── Fixed system origin items ──────────────────────────────────────────
    auto makeOriginItem = [&](const QString& label, int id, bool visibleIn3D) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(COL_NAME, label);
        item->setText(COL_TYPE, QStringLiteral("Origin"));
        item->setData(COL_NAME, RoleKind, QStringLiteral("stage"));
        item->setData(COL_NAME, RoleId,   id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setForeground(COL_NAME, QColor(160, 160, 160));
        item->setIcon(COL_NAME, crosshairIcon());
        item->setCheckState(COL_3D, visibleIn3D ? Qt::Checked : Qt::Unchecked);
        if (id == prevId) tree_->setCurrentItem(item);
    };
    makeOriginItem(QStringLiteral("Stage Origin"), -20, stageOriginVisible_);
    makeOriginItem(QStringLiteral("PSN Origin"),   -21, psnOriginVisible_);

    // ── Manual stage objects ───────────────────────────────────────────────
    for (const auto& obj : objects_) {
        auto* item = new QTreeWidgetItem(tree_);
        const bool isSystem = (obj.id < 0);

        // Determine type label
        QString typeStr;
        if (obj.id == -1)          typeStr = QStringLiteral("Camera");
        else if (obj.id == -2)     typeStr = QStringLiteral("Calib");
        else if (obj.isStageOutline) typeStr = QStringLiteral("Outline");
        else if (obj.isRect)       typeStr = QStringLiteral("Rectangle");
        else                       typeStr = QStringLiteral("Polygon");

        item->setText(COL_NAME, obj.name);
        item->setText(COL_TYPE, typeStr);
        item->setData(COL_NAME, RoleKind,    QStringLiteral("stage"));
        item->setData(COL_NAME, RoleId,      obj.id);
        item->setData(COL_NAME, RoleTypeStr, typeStr);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                       (isSystem ? Qt::ItemFlag{} : Qt::ItemIsEditable));
        if (isSystem) item->setForeground(COL_NAME, QColor(160, 160, 160));

        if (obj.id != -1) {
            item->setData(COL_VID, RoleKind, QStringLiteral("stage"));
            item->setData(COL_VID, RoleId,   obj.id);
            item->setCheckState(COL_VID, obj.visibleInVideo ? Qt::Checked : Qt::Unchecked);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        }

        item->setData(COL_3D, RoleKind, QStringLiteral("stage"));
        item->setData(COL_3D, RoleId,   obj.id);
        item->setCheckState(COL_3D, obj.visibleIn3D ? Qt::Checked : Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        if (obj.id == prevId) tree_->setCurrentItem(item);
    }

    // ── MVR imports (one root per imported file) ───────────────────────────
    for (int ii = 0; ii < mvrImports_.size(); ++ii) {
        const MvrImport& import = mvrImports_[ii];

        auto* mvrRoot = new QTreeWidgetItem(tree_);
        mvrRoot->setText(COL_NAME, import.name.isEmpty()
                                    ? QStringLiteral("MVR Import") : import.name);
        mvrRoot->setData(COL_NAME, RoleKind,      QStringLiteral("mvr-root"));
        mvrRoot->setData(COL_NAME, RoleMvrImport, ii);
        // Editable so user can rename it inline (F2 or double-click)
        mvrRoot->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        mvrRoot->setExpanded(false);
        mvrRoot->setForeground(COL_NAME, QColor(100, 180, 255));
        mvrRoot->setCheckState(COL_3D, import.enabled ? Qt::Checked : Qt::Unchecked);

        for (int li = 0; li < import.layers.size(); ++li) {
            const MvrLayer& layer = import.layers[li];

            auto* layerItem = new QTreeWidgetItem(mvrRoot);
            layerItem->setText(COL_NAME, layer.name.isEmpty()
                                          ? QStringLiteral("(layer)") : layer.name);
            layerItem->setData(COL_NAME, RoleKind,      QStringLiteral("mvr-layer"));
            layerItem->setData(COL_NAME, RoleMvrImport, ii);
            layerItem->setData(COL_NAME, RoleMvrLayer,  li);
            layerItem->setData(COL_NAME, RoleMvrObj,    -1);
            layerItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            layerItem->setCheckState(COL_3D, layer.enabled ? Qt::Checked : Qt::Unchecked);
            layerItem->setExpanded(false);

            bool layerHasFixture = false;
            for (int oi = 0; oi < layer.objects.size(); ++oi) {
                const MvrObject& obj = layer.objects[oi];
                auto* objItem = new QTreeWidgetItem(layerItem);
                objItem->setText(COL_NAME, obj.name.isEmpty()
                                             ? QStringLiteral("(obj)") : obj.name);

                QString typeStr;
                switch (obj.type) {
                    case MvrObject::Type::Fixture:     typeStr = QStringLiteral("Fixture");     break;
                    case MvrObject::Type::Truss:       typeStr = QStringLiteral("Truss");       break;
                    case MvrObject::Type::SceneObject: typeStr = QStringLiteral("Scene Object"); break;
                    case MvrObject::Type::Group:       typeStr = QStringLiteral("Group");        break;
                    default:                           typeStr = QStringLiteral("Object");       break;
                }
                objItem->setText(COL_TYPE, typeStr);
                objItem->setData(COL_NAME, RoleKind,      QStringLiteral("mvr-obj"));
                objItem->setData(COL_NAME, RoleMvrImport, ii);
                objItem->setData(COL_NAME, RoleMvrLayer,  li);
                objItem->setData(COL_NAME, RoleMvrObj,    oi);
                objItem->setData(COL_NAME, RoleTypeStr,   typeStr);
                objItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                objItem->setCheckState(COL_3D, obj.enabled ? Qt::Checked : Qt::Unchecked);

                if (obj.type == MvrObject::Type::Fixture) {
                    objItem->setIcon(COL_NAME, fixtureIcon());
                    layerHasFixture = true;
                }
            }

            if (layerHasFixture)
                layerItem->setIcon(COL_NAME, fixtureIcon());
        }
    }

    updatingTree_ = false;
    updateButtonStates();
}

void StageItemsPanel::updateButtonStates()
{
    deleteBtn_->setEnabled(selectedId_ >= 0);
}

void StageItemsPanel::applyFilter(const QString& text)
{
    const QString lc = text.trimmed().toLower();

    // Recurse through all top-level items and their children
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* top = tree_->topLevelItem(i);
        const QString kind = top->data(COL_NAME, RoleKind).toString();

        if (kind == QLatin1String("stage")) {
            if (lc.isEmpty()) { top->setHidden(false); continue; }
            const QString name = top->text(COL_NAME).toLower();
            const QString type = top->data(COL_NAME, RoleTypeStr).toString().toLower();
            top->setHidden(!name.contains(lc) && !type.contains(lc));
        } else {
            // MVR root — check children
            bool anyVisible = false;
            for (int li = 0; li < top->childCount(); ++li) {
                auto* layer = top->child(li);
                bool layerHasMatch = false;
                for (int oi = 0; oi < layer->childCount(); ++oi) {
                    auto* obj = layer->child(oi);
                    if (lc.isEmpty()) { obj->setHidden(false); layerHasMatch = true; continue; }
                    const QString name = obj->text(COL_NAME).toLower();
                    const QString type = obj->data(COL_NAME, RoleTypeStr).toString().toLower();
                    const bool match = name.contains(lc) || type.contains(lc);
                    obj->setHidden(!match);
                    if (match) layerHasMatch = true;
                }
                layer->setHidden(!layerHasMatch);
                if (layerHasMatch) { anyVisible = true; layer->setExpanded(!lc.isEmpty()); }
            }
            top->setHidden(!anyVisible && !lc.isEmpty());
            if (anyVisible && !lc.isEmpty()) top->setExpanded(true);
        }
    }
}
