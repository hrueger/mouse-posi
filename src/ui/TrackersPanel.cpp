#include "TrackersPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QColorDialog>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMessageBox>
#include <QLabel>

static QIcon colorSwatch(const QColor& c) {
    QPixmap pm(16, 16);
    pm.fill(c);
    return QIcon(pm);
}

static bool editTrackerDialog(QWidget* parent, TrackerConfig& t,
                               const QList<TrackerConfig>& existing)
{
    QDialog dlg(parent);
    dlg.setWindowTitle("Edit Tracker");
    auto* fl = new QFormLayout(&dlg);

    auto* idSpin   = new QSpinBox;
    idSpin->setRange(1, 9);
    idSpin->setValue(t.id);

    auto* nameEdit = new QLineEdit(t.name);

    QColor chosenColor = t.color;
    auto* colorBtn = new QPushButton;
    colorBtn->setFixedWidth(60);
    auto updateColorBtn = [&]() {
        QPixmap pm(48, 18); pm.fill(chosenColor);
        colorBtn->setIcon(QIcon(pm));
        colorBtn->setText(chosenColor.name());
    };
    updateColorBtn();
    QObject::connect(colorBtn, &QPushButton::clicked, &dlg, [&]() {
        QColor c = QColorDialog::getColor(chosenColor, &dlg);
        if (c.isValid()) { chosenColor = c; updateColorBtn(); }
    });

    fl->addRow("ID (1–9):", idSpin);
    fl->addRow("Name:",     nameEdit);
    fl->addRow("Color:",    colorBtn);

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    fl->addRow(bbox);
    QObject::connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;

    int newId = idSpin->value();
    for (const auto& other : existing) {
        if (other.id == newId && other.id != t.id) {
            QMessageBox::warning(parent, "Duplicate ID",
                QString("ID %1 is already used by \"%2\".").arg(newId).arg(other.name));
            return false;
        }
    }
    t.id    = newId;
    t.name  = nameEdit->text().isEmpty() ? QString("Tracker %1").arg(newId) : nameEdit->text();
    t.color = chosenColor;
    return true;
}

TrackersPanel::TrackersPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* hint = new QLabel("Press 1–9 to select");
    hint->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    layout->addWidget(hint);

    list_ = new QListWidget;
    list_->setAlternatingRowColors(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setMaximumHeight(200);
    layout->addWidget(list_);

    auto* btnRow = new QHBoxLayout;
    addBtn_    = new QPushButton("+");
    removeBtn_ = new QPushButton("−");
    editBtn_   = new QPushButton("Edit…");
    addBtn_->setFixedHeight(28);
    removeBtn_->setFixedHeight(28);
    addBtn_->setToolTip("Add tracker");
    removeBtn_->setToolTip("Remove selected tracker");
    editBtn_->setToolTip("Edit selected tracker");
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addWidget(editBtn_);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(addBtn_,    &QPushButton::clicked, this, &TrackersPanel::onAddTracker);
    connect(removeBtn_, &QPushButton::clicked, this, &TrackersPanel::onRemoveTracker);
    connect(editBtn_,   &QPushButton::clicked, this, &TrackersPanel::onEditTracker);
    connect(list_, &QListWidget::itemClicked,  this, [this](QListWidgetItem* item) {
        int id = item->data(Qt::UserRole).toInt();
        activeId_ = id;
        setActiveTrackerId(id);
        QColor color = Qt::white;
        for (const auto& t : trackers_)
            if (t.id == id) { color = t.color; break; }
        emit activeTrackerChanged(id, color);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onEditTracker();
    });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](QPoint pos) {
        if (!list_->itemAt(pos)) return;
        QMenu menu;
        menu.addAction("Edit…",  this, &TrackersPanel::onEditTracker);
        menu.addAction("Remove", this, &TrackersPanel::onRemoveTracker);
        menu.exec(list_->mapToGlobal(pos));
    });
}

void TrackersPanel::setTrackers(const QList<TrackerConfig>& trackers) {
    trackers_ = trackers;
    rebuildList();
}

void TrackersPanel::setActiveTrackerId(int id) {
    activeId_ = id;
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        int tid = item->data(Qt::UserRole).toInt();
        QFont f = item->font();
        f.setBold(tid == id);
        item->setFont(f);
        item->setBackground(tid == id ? QColor(50, 80, 50) : Qt::transparent);
    }
}

QColor TrackersPanel::activeColor() const {
    for (const auto& t : trackers_)
        if (t.id == activeId_) return t.color;
    return Qt::white;
}

void TrackersPanel::rebuildList() {
    int prevId = activeId_;
    list_->clear();
    for (const auto& t : trackers_) {
        auto* item = new QListWidgetItem(
            colorSwatch(t.color),
            QString("[%1]  %2").arg(t.id).arg(t.name));
        item->setData(Qt::UserRole, t.id);
        list_->addItem(item);
    }
    setActiveTrackerId(prevId);
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
    static const QColor palette[] = {
        {255,80,80}, {80,200,80}, {80,140,255},
        {255,200,60}, {200,80,255}, {60,220,220},
        {255,140,40}, {180,255,80}, {255,80,200}
    };
    TrackerConfig t;
    t.id    = newId;
    t.name  = QString("Spot %1").arg(newId);
    t.color = palette[(newId - 1) % 9];
    if (!editTrackerDialog(this, t, trackers_)) return;
    trackers_.append(t);
    std::sort(trackers_.begin(), trackers_.end(),
              [](const TrackerConfig& a, const TrackerConfig& b){ return a.id < b.id; });
    rebuildList();
    emit trackersChanged(trackers_);
}

void TrackersPanel::onRemoveTracker() {
    int row = list_->currentRow();
    if (row < 0 || row >= trackers_.size()) return;
    if (trackers_.size() == 1) {
        QMessageBox::information(this, "Cannot remove", "At least one tracker is required.");
        return;
    }
    trackers_.removeAt(row);
    rebuildList();
    emit trackersChanged(trackers_);
}

void TrackersPanel::onEditTracker() {
    editTrackerAt(list_->currentRow());
}

void TrackersPanel::editTrackerAt(int row) {
    if (row < 0 || row >= trackers_.size()) return;
    TrackerConfig t = trackers_[row];
    if (!editTrackerDialog(this, t, trackers_)) return;
    trackers_[row] = t;
    std::sort(trackers_.begin(), trackers_.end(),
              [](const TrackerConfig& a, const TrackerConfig& b){ return a.id < b.id; });
    rebuildList();
    emit trackersChanged(trackers_);
}
