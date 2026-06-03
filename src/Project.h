#pragma once
#include <QString>
#include <QByteArray>
#include <functional>
#include <QColor>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QPolygonF>
#include <QVector3D>

struct StageObject {
    int       id       = 0;
    QString   name;
    QColor    color    = QColor(100, 160, 220, 180);
    float     height   = 1.0f;   // metres above floor
    bool      isRect   = true;   // was created as rectangle
    QPointF   center;            // XZ centre (rect)
    float     width    = 2.0f;   // X dimension metres (rect)
    float     depth    = 1.0f;   // Z dimension metres (rect)
    float     rotation = 0.0f;   // degrees around Y axis (rect)
    QPolygonF polygon;           // XZ polygon — always kept in sync
    bool      visibleInVideo = true;
    bool      visibleIn3D    = true;
    bool      isStageOutline = false; // stage boundary — no height walls
    float     fovDeg         = 60.0f; // horizontal FOV (Camera system item only)
};

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
    bool  showFloorGrid         = false;
    float clickPlaneHeight      = 0.0f;
    bool  showClickPlane        = false;
    bool  showCalibRectInVideo  = true;
    bool  showCalibRectIn3D     = true;
    bool  showCameraIn3D        = false;
    float cameraFovDeg          = 60.0f;
};

enum class SacnMode { Multicast, Unicast };

struct SacnInputConfig {
    bool      enabled   = false;
    SacnMode  mode      = SacnMode::Multicast;
    QString   iface;
    quint16   universe  = 1;
    quint16   address   = 1;    // DMX channel 1–512
    float     minHeight = 0.0f;
    float     maxHeight = 10.0f;
};

struct NetworkConfig {
    PsnMode  psnMode         = PsnMode::Multicast;
    QString  multicastIp     = QStringLiteral("236.10.10.10");
    QString  unicastIp;
    QString  broadcastIp     = QStringLiteral("255.255.255.255");
    quint16  port            = 56565;
    QString  psnInterface;      // NIC name for PSN UDP output (empty = OS default)
    QString  sessionInterface;  // NIC name for DNS-SD + TCP session (empty = OS default)
    SacnInputConfig sacnInput;
};

struct Stage3DCameraState {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;
    float yaw     = 0.0f;
    float pitch   = 45.0f;
    float dist    = 10.0f;
};

enum class MvrRenderModeEnum { Flat, Shaded, Wireframe };

struct MvrObjectData {
    enum class Type { Fixture, SceneObject, Truss, Group, Unknown };

    QString   name;
    Type      type       = Type::Unknown;
    QVector3D positionM;
    QString   gdtfSpec;
    int       unitNumber = 0;
    int       dmxAddress = 0;
    bool      enabled    = true;
};

struct MvrLayerData {
    QString              name;
    QList<MvrObjectData> objects;
    bool                 enabled = true;
};

struct MvrImportData {
    QString              name;
    QList<MvrLayerData>  layers;
    float                offsetX = 0.f;
    float                offsetY = 0.f;
    float                offsetZ = 0.f;
    float                rotDeg  = 0.f;
    bool                 enabled = true;
    QByteArray           mvrData; // raw MVR file bytes embedded in the showfile
};

struct MvrSettings {
    QList<MvrImportData> imports;
    bool   showLabels = false;  // default off
    MvrRenderModeEnum renderMode = MvrRenderModeEnum::Shaded;
};

struct Project {
    QString                 videoSourceType;     // "ndi" | "webcam" | "decklink" (empty = ndi)
    QString                 ndiSource;
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
    QList<StageObject>        stageObjects;
    Stage3DCameraState        stage3dCamera;
    MvrSettings               mvr;

    static Project   load(const QString& path);
    // progress callback: called with entry index (0 = JSON, 1..n = MVR files) after each entry written
    void             save(const QString& path,
                          std::function<void(int)> progress = {}) const;
    static Project   defaultProject();
};
