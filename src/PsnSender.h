#pragma once
#include "Project.h"
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QMap>
#include <QElapsedTimer>
#include <tuple>
#include <atomic>

class PsnSender : public QObject {
    Q_OBJECT
public:
    explicit PsnSender(QObject* parent = nullptr);

    void configure(const NetworkConfig& cfg);

    // positions: tracker_id → (x_m, z_m); heights: tracker_id → y_m output
    void sendPositions(const QMap<int, QPair<float,float>>& positions,
                       const QList<TrackerConfig>& trackers,
                       const QMap<int, float>& heights);

    quint64 totalPacketsSent() const { return totalPacketsSent_.load(std::memory_order_relaxed); }

private:
    void sendDataPacketsV2(const QMap<int, QPair<float,float>>& positions,
                           const QMap<int, float>& heights);
    void sendInfoPacketsV2(const QList<TrackerConfig>& trackers);

    QUdpSocket   socket_;
    QHostAddress targetAddr_;
    quint16      port_      = 56565;
    PsnMode      psnMode_   = PsnMode::Multicast;

    float psnOffsetX_ = 0.f;
    float psnOffsetY_ = 0.f;
    float psnOffsetZ_ = 0.f;
    float psnRotDeg_  = 0.f;

    QString      systemName_;

    quint8  dataFrameId_ = 1;
    quint8  infoFrameId_ = 1;
    int     ticksSinceInfo_= 0;

    std::atomic<quint64> totalPacketsSent_{0};

    // For velocity computation
    QMap<int, QPair<float,float>> lastPos_;
    QMap<int, qint64>             lastTimeMs_;
    QElapsedTimer                 timer_;
};
