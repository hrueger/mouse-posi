#pragma once
#include <QString>
#include <QColor>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QJsonObject>

struct TrackerConfig {
    int     id    = 1;
    QString name  = "Tracker 1";
    QColor  color = Qt::red;
};

struct CalibrationData {
    QList<QPointF> imagePoints;          // pixel coords (floor)
    QList<QPointF> stagePoints;          // real XZ in metres (floor)
    QList<double>  homography;           // 3×3 row-major, 9 values
    // 3D extension (optional — present when calibrated with two planes)
    QList<QPointF> elevatedImagePoints;  // pixel coords of elevated markers
    float          markerHeight = 0.0f;  // Y of elevated markers in metres
    QList<double>  projectionMatrix;     // 3×4 row-major, 12 values

    bool isValid()   const { return homography.size() == 9; }
    bool is3DValid() const { return projectionMatrix.size() == 12 && markerHeight > 0.0f; }
};

enum class PsnMode { Multicast, Unicast, Broadcast };

struct CalibrationViewSettings {
    bool  showFloorGrid    = false;
    float clickPlaneHeight = 0.0f;
    bool  showClickPlane   = false;
    float psnOutputHeight  = 0.0f;
};

struct NetworkConfig {
    PsnMode  psnMode         = PsnMode::Multicast;
    QString  multicastIp     = QStringLiteral("236.10.10.10");
    QString  unicastIp;
    QString  broadcastIp     = QStringLiteral("255.255.255.255");
    quint16  port            = 56565;
    QString  psnInterface;      // NIC name for PSN UDP output (empty = OS default)
    QString  sessionInterface;  // NIC name for DNS-SD + TCP session (empty = OS default)
};

struct CameraControlConfig {
    QString     type = QStringLiteral("cv370");
    bool        enabled = false;
    QJsonObject config;
};

struct Project {
    QString                 videoSourceType;     // "ndi" | "webcam" | "decklink" (empty = ndi)
    QString                 ndiSource;
    CameraControlConfig     cameraControl;
    QString                 decklinkDevice;      // persistent-ID hash (see DeckLinkCapture::DeviceInfo)
    QString                 decklinkConnection;
    bool                    decklinkAllow10Bit  = true;
    quint32                 decklinkDisplayMode = 0;  // 0 = Auto (bmdModeUnknown)
    QList<TrackerConfig>    trackers;
    CalibrationData         calibration;
    CalibrationViewSettings calibrationView;
    NetworkConfig           network;
    // Per-station tracker assignments: stationName -> list of assigned tracker IDs.
    // Populated by the host and persisted so rejoining stations get their last config.
    QMap<QString, QList<int>> stationTrackers;

    static Project   load(const QString& path);
    void             save(const QString& path) const;
    static Project   defaultProject();
};
