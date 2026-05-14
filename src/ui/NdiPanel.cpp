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

    statusLabel_ = new QLabel("Click Scan to discover sources.");
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("color: palette(mid); font-size: 11px;");
    layout->addWidget(statusLabel_);

    list_ = new QListWidget;
    list_->setMaximumHeight(120);
    layout->addWidget(list_);

    auto* btnRow = new QHBoxLayout;
    scanBtn_    = new QPushButton("Scan");
    connectBtn_ = new QPushButton("Connect");
    connectBtn_->setEnabled(false);
    btnRow->addWidget(scanBtn_);
    btnRow->addWidget(connectBtn_);
    layout->addLayout(btnRow);

    connect(scanBtn_, &QPushButton::clicked, this, &NdiPanel::scan);
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
            list_->clear();
            list_->addItems(sources);
            statusLabel_->setText(sources.isEmpty()
                ? "No sources found." : QString("%1 source(s) found.").arg(sources.size()));
        });
    }

    // Auto-discover sources on startup.
    QTimer::singleShot(0, this, [this]() { scan(); });
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

void NdiPanel::scan() {
    statusLabel_->setText("Scanning…");
    list_->clear();
    if (ndi_) ndi_->discoverSources();
}
