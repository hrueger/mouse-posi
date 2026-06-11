#pragma once
#include <QString>
#include <QByteArray>
#include <functional>
#include <QColor>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QPolygonF>
#include <QMatrix4x4>
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

// Legacy sACN height input — superseded by InputAdapterConfig, kept for file compat
enum class SacnMode { Multicast, Unicast };
struct SacnInputConfig {
    bool      enabled   = false;
    SacnMode  mode      = SacnMode::Multicast;
    QString   iface;
    quint16   universe  = 1;
    quint16   address   = 1;
    float     minHeight = 0.0f;
    float     maxHeight = 10.0f;
};

struct NetworkConfig {
    PsnMode  psnMode         = PsnMode::Multicast;
    QString  multicastIp     = QStringLiteral("236.10.10.10");
    QString  unicastIp;
    QString  broadcastIp     = QStringLiteral("255.255.255.255");
    quint16  port            = 56565;
    QString  psnInterface;
    QString  sessionInterface;
    SacnInputConfig sacnInput; // legacy
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

// ─── DMX / Operating Mode ────────────────────────────────────────────────────

enum class OperatingMode {
    Stage3DPSN, // default: 3D stage + ClickPlane → PSN output
    Camera2D,   // camera next to light, 2D pixel→pan/tilt calibration → DMX
    Stage3DDMX, // 3D stage + ClickPlane, OnPoint computes pan/tilt → DMX
};

enum class DmxOutputMode  { PanTiltOnly, Replacement };
enum class DmxProtocol    { SACN, ArtNet };
enum class DmxNetworkMode { Multicast, Unicast, Broadcast };

struct DmxUniverseConfig {
    quint16        universe  = 1;
    DmxProtocol    protocol  = DmxProtocol::SACN;
    DmxNetworkMode netMode   = DmxNetworkMode::Multicast;
    QString        iface;
    QString        unicastIp;
};

struct DmxOutputConfig {
    DmxOutputMode            outputMode = DmxOutputMode::PanTiltOnly;
    QList<DmxUniverseConfig> outputs;  // one per active output universe
    QList<DmxUniverseConfig> inputs;   // replacement mode: source universes (keyed by universe #)
};

// ─── Unified DMX Universe system ─────────────────────────────────────────────

enum class DmxUniverseRole { InControl, InFixtures, OutFixtures };

struct DmxChannelMapping {
    quint16 channel  = 1;
    QString target;       // "clickPlaneHeight", "dimmer", "zoom", "iris", "focus"
    float   minValue = 0.f;
    float   maxValue = 10.f;
};

struct DmxUniverseEntry {
    QString          name;
    quint16          number   = 1;
    DmxUniverseRole  role     = DmxUniverseRole::OutFixtures;
    DmxProtocol      protocol = DmxProtocol::SACN;
    DmxNetworkMode   netMode  = DmxNetworkMode::Multicast;
    QString          iface;
    QString          unicastIp;
    bool             enabled  = true;
    QList<DmxChannelMapping> mappings;       // only for role == InControl
    int  mergeFromUniverse = -1;             // only for role == OutFixtures; -1 = none
};

// ─── GDTF DMX profile (pan/tilt channel info extracted from GDTF) ────────────

struct GdtfChannelInfo {
    int   address  = -1;     // 1-based address relative to fixture dmxAddress; -1 = not found
    int   address2 = -1;     // fine (16-bit) channel, -1 = 8-bit only
    bool  is16bit  = false;
    float minDeg   = -270.f;
    float maxDeg   =  270.f;
};

struct GdtfDmxProfile {
    GdtfChannelInfo pan;
    GdtfChannelInfo tilt;
    bool    valid     = false;
    int     footprint = 0;       // total DMX channels in the active mode
    QString modeName;            // active mode name (from GDTF / MVR GdtfMode attribute)
    QMap<int, QString> channelNames; // 1-based relative addr → attribute name for all channels
};

// ─── Camera 2D calibration (pixel → pan/tilt DMX) ───────────────────────────

struct Camera2DCalibPoint {
    QPointF pixel;
    float   panDmx  = 0.f; // 0–65535
    float   tiltDmx = 0.f;
};

struct Camera2DCalibration {
    QList<Camera2DCalibPoint> points;
    QList<double>             homography; // 9 doubles (3×3, pixel→pan/tilt DMX)
    bool                      valid = false;
};

// ─── Input adapter plugin system ─────────────────────────────────────────────

enum class InputAdapterType { SacnArtNet, Midi };

struct InputAdapterMapping {
    // "clickPlaneHeight", "dimmer", "zoom", "iris", "focus"
    QString target;

    // sACN/ArtNet adapter fields
    quint16 universe = 1;
    quint16 channel  = 1;   // DMX channel 1–512
    float   minValue = 0.f;
    float   maxValue = 10.f;

    // MIDI adapter fields
    QString midiPort;
    int     midiCC      = -1;
    int     midiChannel = 1;
};

struct InputAdapterConfig {
    InputAdapterType             type     = InputAdapterType::SacnArtNet;
    DmxProtocol                  protocol = DmxProtocol::SACN;
    DmxNetworkMode               netMode  = DmxNetworkMode::Multicast;
    QString                      iface;
    QString                      unicastIp;
    QList<InputAdapterMapping>   mappings;
    bool                         enabled  = true;
};

// ─── MVR object ──────────────────────────────────────────────────────────────

struct MvrObjectData {
    enum class Type { Fixture, SceneObject, Truss, Group, Unknown };

    QString        name;
    Type           type        = Type::Unknown;
    QVector3D      positionM;
    QMatrix4x4     xformRot;            // rotation part of MVR transform (no translation); identity = upright
    QString        gdtfSpec;
    int            unitNumber  = 0;
    QString        fixtureId;           // user-defined fixture ID (FID) from MVR FixtureID attribute
    int            dmxAddress  = 0;
    int            universe    = 1;    // DMX universe this fixture lives in
    bool           enabled     = true;
    GdtfDmxProfile gdtfProfile;       // parsed pan/tilt channel info (populated on MVR import)
    int            trackerLink = -1;   // tracker ID this fixture follows (-1 = unlinked)
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
    QByteArray           mvrData;
};

struct MvrSettings {
    QList<MvrImportData> imports;
    bool              showLabels  = false;
    MvrRenderModeEnum renderMode  = MvrRenderModeEnum::Shaded;
};

// ─── Project ─────────────────────────────────────────────────────────────────

struct Project {
    QString                 videoSourceType;
    QString                 ndiSource;
    QString                 webcamDevice;
    QString                 decklinkDevice;
    QString                 decklinkConnection;
    bool                    decklinkAllow10Bit  = true;
    quint32                 decklinkDisplayMode = 0;
    QList<TrackerConfig>    trackers;
    CalibrationData         calibration;
    CalibrationViewSettings calibrationView;
    NetworkConfig           network;
    QMap<QString, QList<int>> stationTrackers;
    QList<StageObject>        stageObjects;
    Stage3DCameraState        stage3dCamera;
    MvrSettings               mvr;

    // ─ New fields ─
    OperatingMode             operatingMode  = OperatingMode::Stage3DPSN;
    DmxOutputConfig           dmxOutput;
    Camera2DCalibration       camera2DCalib;
    QList<InputAdapterConfig> inputAdapters;
    QList<DmxUniverseEntry>   dmxUniverses;

    static Project   load(const QString& path);
    void             save(const QString& path,
                          std::function<void(int)> progress = {}) const;
    static Project   defaultProject();
};
