#pragma once
#include <QString>
#include <QColor>
#include <QList>
#include <QPointF>

struct TrackerConfig {
    int     id    = 1;
    QString name  = "Tracker 1";
    QColor  color = Qt::red;
};

struct CalibrationData {
    QList<QPointF> imagePoints;   // pixel coords in original frame space
    QList<QPointF> stagePoints;   // real XZ in meters
    QList<double>  homography;    // 3x3 row-major, empty = not calibrated
    bool isValid() const { return homography.size() == 9; }
};

enum class PsnMode { Multicast, Unicast, Broadcast };

struct NetworkConfig {
    PsnMode  psnMode         = PsnMode::Multicast;
    QString  multicastIp     = QStringLiteral("236.10.10.10");
    QString  unicastIp;
    QString  broadcastIp     = QStringLiteral("255.255.255.255");
    quint16  port            = 56565;
    QString  psnInterface;      // NIC name for PSN UDP output (empty = OS default)
    QString  sessionInterface;  // NIC name for DNS-SD + TCP session (empty = OS default)
};

struct Project {
    QString          name       = QStringLiteral("New Project");
    QString          ndiSource;
    QList<TrackerConfig> trackers;
    CalibrationData  calibration;
    NetworkConfig    network;

    static Project   load(const QString& path);
    void             save(const QString& path) const;
    static Project   defaultProject();
};
