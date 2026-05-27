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
    sacnRxLabel_  = new QLabel("Disabled");
    sessionLabel_ = new QLabel("Offline");

    fl->addRow("NDI:",        ndiLabel_);
    fl->addRow("PSN TX:",     psnTxLabel_);
    fl->addRow("PSN RX:",     psnRxLabel_);
    fl->addRow("sACN RX:",    sacnRxLabel_);
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

void StatsPanel::setSacnRxInfo(bool enabled, int packetsPerSec, float height) {
    if (!enabled) {
        sacnRxLabel_->setText("Disabled");
        sacnRxLabel_->setStyleSheet("");
        return;
    }
    if (packetsPerSec > 0)
        sacnRxLabel_->setText(QString("%1 pkt/s  →  %2 m")
                               .arg(packetsPerSec).arg(double(height), 0, 'f', 2));
    else
        sacnRxLabel_->setText("0 pkt/s (no signal)");
    sacnRxLabel_->setStyleSheet(packetsPerSec > 0
        ? "color: #33cc55;"
        : "color: #cc8833;");
}

void StatsPanel::setSessionInfo(const QString& statusText, int peerCount) {
    sessionLabel_->setText(peerCount > 0
        ? QString("%1 (%2 peers)").arg(statusText).arg(peerCount)
        : statusText);
}
