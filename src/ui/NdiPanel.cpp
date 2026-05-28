#include "NdiPanel.h"
#include "../NdiReceiver.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

NdiPanel::NdiPanel(NdiReceiver* ndi, QWidget* parent)
    : QWidget(parent), ndi_(ndi)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* headerRow = new QHBoxLayout;
    statusLabel_ = new QLabel("Scanning for sources…");
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    headerRow->addWidget(statusLabel_, 1);

    refreshBtn_ = new QPushButton("↻");
    refreshBtn_->setFixedSize(22, 22);
    refreshBtn_->setToolTip("Refresh NDI sources");
    refreshBtn_->setStyleSheet("font-size: 14px; padding: 0;");
    headerRow->addWidget(refreshBtn_);
    layout->addLayout(headerRow);

    list_ = new QListWidget;
    list_->setMinimumWidth(0);
    list_->setMaximumHeight(120);
    layout->addWidget(list_);

    connectBtn_ = new QPushButton("Connect");
    connectBtn_->setEnabled(false);
    layout->addWidget(connectBtn_);

    connect(refreshBtn_, &QPushButton::clicked, this, &NdiPanel::refresh);
    connect(connectBtn_, &QPushButton::clicked, this, [this]() {
        auto* item = list_->currentItem();
        if (item) emit sourceSelected(item->text());
    });
    connect(list_, &QListWidget::itemSelectionChanged, this, [this]() {
        connectBtn_->setEnabled(list_->currentItem() != nullptr);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit sourceSelected(item->text());
    });

    if (ndi_) {
        connect(ndi_, &NdiReceiver::sourcesChanged, this, [this](QStringList sources) {
            QString current = selectedSource();
            list_->clear();
            list_->addItems(sources);
            if (!current.isEmpty()) {
                const auto items = list_->findItems(current, Qt::MatchExactly);
                if (!items.isEmpty()) list_->setCurrentItem(items.first());
            }
            statusLabel_->setText(sources.isEmpty()
                ? "No sources found." : QString("%1 source(s) found.").arg(sources.size()));
        });
    }

    // Auto-refresh every 5 seconds so new sources appear without manual intervention.
    autoRefreshTimer_ = new QTimer(this);
    autoRefreshTimer_->setInterval(5000);
    connect(autoRefreshTimer_, &QTimer::timeout, this, &NdiPanel::refresh);
    autoRefreshTimer_->start();

    QTimer::singleShot(0, this, [this]() { refresh(); });
}

QString NdiPanel::selectedSource() const {
    auto* item = list_->currentItem();
    return item ? item->text() : QString();
}

void NdiPanel::setCurrentSource(const QString& source) {
    auto items = list_->findItems(source, Qt::MatchExactly);
    if (!items.isEmpty())
        list_->setCurrentItem(items.first());
    else if (!source.isEmpty()) {
        list_->addItem(source);
        list_->setCurrentRow(list_->count() - 1);
    }
}

void NdiPanel::refresh() {
    if (ndi_) ndi_->discoverSources();
}
