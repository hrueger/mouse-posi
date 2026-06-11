#include "NewProjectWizard.h"
#include "ModeSelectionWidget.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QColorDialog>
#include <QFrame>

// ── ModePage ──────────────────────────────────────────────────────────────────

ModePage::ModePage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Operating Mode");
    setSubTitle("Choose how OnPoint will track followspots and where it sends position data.");

    auto* lay = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    modeWidget_ = new ModeSelectionWidget;
    scroll->setWidget(modeWidget_);
    lay->addWidget(scroll);
}

OperatingMode ModePage::selectedMode() const {
    return modeWidget_->mode();
}

// ── TrackersPage ──────────────────────────────────────────────────────────────

TrackersPage::TrackersPage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Trackers");
    setSubTitle("Add at least one tracker. Each tracker corresponds to one followspot operator.");

    auto* lay = new QVBoxLayout(this);

    table_ = new QTableWidget(0, 2);
    table_->setHorizontalHeaderLabels({"Name", "Color"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->setColumnWidth(1, 80);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(table_);

    auto* btnRow = new QHBoxLayout;
    addBtn_    = new QPushButton("+ Add Tracker");
    removeBtn_ = new QPushButton("Remove");
    removeBtn_->setEnabled(false);
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    connect(addBtn_,    &QPushButton::clicked, this, &TrackersPage::onAdd);
    connect(removeBtn_, &QPushButton::clicked, this, &TrackersPage::onRemove);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        removeBtn_->setEnabled(table_->currentRow() >= 0);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col != 1) return;
        auto* item = table_->item(row, col);
        if (!item) return;
        QColor c = QColorDialog::getColor(item->background().color(), this, "Tracker Color");
        if (c.isValid()) {
            item->setBackground(c);
            item->setForeground(c.lightness() > 128 ? Qt::black : Qt::white);
        }
        emit completeChanged();
    });

    // Start with one tracker
    onAdd();
}

void TrackersPage::onAdd() {
    const int row = table_->rowCount();
    table_->insertRow(row);

    auto* nameItem = new QTableWidgetItem(QString("Tracker %1").arg(nextId_));
    table_->setItem(row, 0, nameItem);

    static const QList<QColor> palette = {
        QColor(220,  60,  60), QColor( 60, 140, 220), QColor( 80, 200, 100),
        QColor(220, 180,  40), QColor(180,  80, 220), QColor( 40, 210, 210),
    };
    const QColor c = palette[(nextId_ - 1) % palette.size()];
    auto* colorItem = new QTableWidgetItem("double-click to change");
    colorItem->setBackground(c);
    colorItem->setForeground(c.lightness() > 128 ? Qt::black : Qt::white);
    colorItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 1, colorItem);

    ++nextId_;
    emit completeChanged();
}

void TrackersPage::onRemove() {
    int row = table_->currentRow();
    if (row >= 0) table_->removeRow(row);
    emit completeChanged();
}

bool TrackersPage::isComplete() const {
    return table_->rowCount() >= 1;
}

QList<TrackerConfig> TrackersPage::trackers() const {
    QList<TrackerConfig> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        TrackerConfig t;
        t.id    = row + 1;
        t.name  = table_->item(row, 0) ? table_->item(row, 0)->text() : QString("Tracker %1").arg(row + 1);
        t.color = table_->item(row, 1) ? table_->item(row, 1)->background().color() : Qt::red;
        result.append(t);
    }
    return result;
}

// ── NewProjectWizard ──────────────────────────────────────────────────────────

NewProjectWizard::NewProjectWizard(QWidget* parent) : QWizard(parent) {
    setWindowTitle("New Project");
    setWizardStyle(QWizard::ModernStyle);
    resize(640, 500);

    modePage_     = new ModePage(this);
    trackersPage_ = new TrackersPage(this);

    addPage(modePage_);
    addPage(trackersPage_);

    setButtonText(QWizard::FinishButton, "Create Project");
}

OperatingMode NewProjectWizard::selectedMode() const {
    return modePage_->selectedMode();
}

QList<TrackerConfig> NewProjectWizard::trackers() const {
    return trackersPage_->trackers();
}
