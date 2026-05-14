#include "StatsPanel.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>

StatsPanel::StatsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* fl = new QFormLayout;
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(2);

    ndiLabel_     = new QLabel("—");
    psnTxLabel_   = new QLabel("—");
    psnRxLabel_   = new QLabel("—");
    sessionLabel_ = new QLabel("Offline");

    fl->addRow("NDI:",        ndiLabel_);
    fl->addRow("PSN TX:",     psnTxLabel_);
    fl->addRow("PSN RX:",     psnRxLabel_);
    fl->addRow("Session:",    sessionLabel_);

    layout->addLayout(fl);
    layout->addStretch();
}

void StatsPanel::setNdiInfo(const QString& source, int width, int height, double fps) {
    if (source.isEmpty())
        ndiLabel_->setText("No source");
    else
        ndiLabel_->setText(QString("%1\n%2×%3 @ %4fps")
            .arg(source).arg(width).arg(height).arg(fps, 0, 'f', 1));
}

void StatsPanel::setPsnTxRate(int packetsPerSec) {
    psnTxLabel_->setText(QString("%1 pkt/s").arg(packetsPerSec));
}

void StatsPanel::setPsnRxRate(int packetsPerSec, int trackerCount) {
    psnRxLabel_->setText(QString("%1 pkt/s, %2 trackers").arg(packetsPerSec).arg(trackerCount));
}

void StatsPanel::setSessionInfo(const QString& statusText, int peerCount) {
    sessionLabel_->setText(peerCount > 0
        ? QString("%1 (%2 peers)").arg(statusText).arg(peerCount)
        : statusText);
}
