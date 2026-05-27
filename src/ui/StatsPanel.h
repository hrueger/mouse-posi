#pragma once
#include <QWidget>

class QLabel;

class StatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatsPanel(QWidget* parent = nullptr);

    void setNdiInfo(const QString& source, int width, int height, double fps);
    void setPsnTxRate(int packetsPerSec);
    void setPsnRxRate(int packetsPerSec, int trackerCount);
    void setSacnRxInfo(bool enabled, int packetsPerSec, float height);
    void setSessionInfo(const QString& statusText, int peerCount);

private:
    QLabel* ndiLabel_;
    QLabel* psnTxLabel_;
    QLabel* psnRxLabel_;
    QLabel* sacnRxLabel_;
    QLabel* sessionLabel_;
};
