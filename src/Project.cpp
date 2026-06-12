#include "Project.h"
#include <QFile>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QFileInfo>
#include <archive.h>
#include <archive_entry.h>
#include <stdexcept>

Project Project::defaultProject() {
    Project p;
    p.trackers = {
        {1, "Spot 1", QColor(255, 80,  80)},
        {2, "Spot 2", QColor(80,  200, 80)},
        {3, "Spot 3", QColor(80,  140, 255)},
        {4, "Spot 4", QColor(255, 200, 60)},
    };
    return p;
}

static QJsonArray intListToJson(const QList<int>& ids) {
    QJsonArray arr;
    for (int id : ids) arr << id;
    return arr;
}

static QList<int> jsonToIntList(const QJsonArray& arr) {
    QList<int> ids;
    for (const auto& v : arr) ids << v.toInt();
    return ids;
}

static QJsonArray pointListToJson(const QList<QPointF>& pts) {
    QJsonArray arr;
    for (const auto& p : pts) {
        QJsonArray pt; pt << p.x() << p.y();
        arr << pt;
    }
    return arr;
}

static QList<QPointF> jsonToPointList(const QJsonArray& arr) {
    QList<QPointF> pts;
    for (const auto& v : arr) {
        QJsonArray pt = v.toArray();
        if (pt.size() >= 2)
            pts << QPointF(pt[0].toDouble(), pt[1].toDouble());
    }
    return pts;
}

// ─── MVR Serialization (metadata only, no mesh geometry) ─────────────────────

static QJsonObject gdtfChannelInfoToJson(const GdtfChannelInfo& c) {
    QJsonObject o;
    o["address"]  = c.address;
    o["address2"] = c.address2;
    o["is16bit"]  = c.is16bit;
    o["minDeg"]   = double(c.minDeg);
    o["maxDeg"]   = double(c.maxDeg);
    return o;
}

static GdtfChannelInfo jsonToGdtfChannelInfo(const QJsonObject& o) {
    GdtfChannelInfo c;
    c.address  = o["address"].toInt(-1);
    c.address2 = o["address2"].toInt(-1);
    c.is16bit  = o["is16bit"].toBool(false);
    c.minDeg   = float(o["minDeg"].toDouble(-270.0));
    c.maxDeg   = float(o["maxDeg"].toDouble(270.0));
    return c;
}

static QJsonObject mvrObjectToJson(const MvrObjectData& obj) {
    QJsonObject json;
    json["name"]        = obj.name;
    json["type"]        = int(obj.type);
    json["position"]    = QJsonArray{obj.positionM.x(), obj.positionM.y(), obj.positionM.z()};
    {
        const QMatrix4x4& m = obj.xformRot;
        json["rot"] = QJsonArray{
            m(0,0), m(0,1), m(0,2),
            m(1,0), m(1,1), m(1,2),
            m(2,0), m(2,1), m(2,2)
        };
    }
    json["gdtfSpec"]    = obj.gdtfSpec;
    json["unitNumber"]  = obj.unitNumber;
    if (!obj.fixtureId.isEmpty())
        json["fixtureId"] = obj.fixtureId;
    json["dmxAddress"]  = obj.dmxAddress;
    json["universe"]    = obj.universe;
    json["enabled"]     = obj.enabled;
    json["trackerLink"] = obj.trackerLink;
    if (obj.gdtfProfile.valid) {
        QJsonObject profile;
        profile["pan"]       = gdtfChannelInfoToJson(obj.gdtfProfile.pan);
        profile["tilt"]      = gdtfChannelInfoToJson(obj.gdtfProfile.tilt);
        profile["footprint"] = obj.gdtfProfile.footprint;
        if (!obj.gdtfProfile.modeName.isEmpty())
            profile["modeName"] = obj.gdtfProfile.modeName;
        json["gdtfProfile"] = profile;
    }
    return json;
}

static MvrObjectData jsonToMvrObject(const QJsonObject& json) {
    MvrObjectData obj;
    obj.name        = json["name"].toString();
    obj.type        = MvrObjectData::Type(json["type"].toInt(int(MvrObjectData::Type::Unknown)));
    QJsonArray pos  = json["position"].toArray();
    if (pos.size() >= 3)
        obj.positionM = QVector3D(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());
    if (json.contains("rot")) {
        const QJsonArray r = json["rot"].toArray();
        if (r.size() == 9) {
            obj.xformRot(0,0) = float(r[0].toDouble()); obj.xformRot(0,1) = float(r[1].toDouble()); obj.xformRot(0,2) = float(r[2].toDouble());
            obj.xformRot(1,0) = float(r[3].toDouble()); obj.xformRot(1,1) = float(r[4].toDouble()); obj.xformRot(1,2) = float(r[5].toDouble());
            obj.xformRot(2,0) = float(r[6].toDouble()); obj.xformRot(2,1) = float(r[7].toDouble()); obj.xformRot(2,2) = float(r[8].toDouble());
        }
    }
    obj.gdtfSpec    = json["gdtfSpec"].toString();
    obj.unitNumber  = json["unitNumber"].toInt();
    obj.fixtureId   = json["fixtureId"].toString();
    obj.dmxAddress  = json["dmxAddress"].toInt();
    obj.universe    = json["universe"].toInt(1);
    obj.enabled     = json["enabled"].toBool(true);
    obj.trackerLink = json["trackerLink"].toInt(-1);
    if (json.contains("gdtfProfile")) {
        QJsonObject profile = json["gdtfProfile"].toObject();
        obj.gdtfProfile.pan       = jsonToGdtfChannelInfo(profile["pan"].toObject());
        obj.gdtfProfile.tilt      = jsonToGdtfChannelInfo(profile["tilt"].toObject());
        obj.gdtfProfile.footprint = profile["footprint"].toInt(0);
        obj.gdtfProfile.modeName  = profile["modeName"].toString();
        obj.gdtfProfile.valid     = true;
    }
    return obj;
}

static QJsonObject mvrLayerToJson(const MvrLayerData& layer) {
    QJsonObject json;
    json["name"] = layer.name;
    json["enabled"] = layer.enabled;
    QJsonArray objsArr;
    for (const auto& obj : layer.objects)
        objsArr.append(mvrObjectToJson(obj));
    json["objects"] = objsArr;
    return json;
}

static MvrLayerData jsonToMvrLayer(const QJsonObject& json) {
    MvrLayerData layer;
    layer.name = json["name"].toString();
    layer.enabled = json["enabled"].toBool(true);
    for (const auto& v : json["objects"].toArray())
        layer.objects.append(jsonToMvrObject(v.toObject()));
    return layer;
}

static QJsonObject mvrImportToJson(const MvrImportData& import, int index) {
    QJsonObject json;
    json["name"] = import.name;
    json["offsetX"] = double(import.offsetX);
    json["offsetY"] = double(import.offsetY);
    json["offsetZ"] = double(import.offsetZ);
    json["rotDeg"] = double(import.rotDeg);
    json["enabled"] = import.enabled;
    json["mvrFile"] = QString("mvr/%1.mvr").arg(index);
    QJsonArray layersArr;
    for (const auto& layer : import.layers)
        layersArr.append(mvrLayerToJson(layer));
    json["layers"] = layersArr;
    return json;
}

static MvrImportData jsonToMvrImport(const QJsonObject& json) {
    MvrImportData import;
    import.name = json["name"].toString();
    import.offsetX = float(json["offsetX"].toDouble(0.0));
    import.offsetY = float(json["offsetY"].toDouble(0.0));
    import.offsetZ = float(json["offsetZ"].toDouble(0.0));
    import.rotDeg = float(json["rotDeg"].toDouble(0.0));
    import.enabled = json["enabled"].toBool(true);
    for (const auto& v : json["layers"].toArray())
        import.layers.append(jsonToMvrLayer(v.toObject()));
    return import;
}

// ─── DMX / Input adapter serialization ───────────────────────────────────────

static QString dmxProtocolToString(DmxProtocol p) {
    return p == DmxProtocol::ArtNet ? QStringLiteral("artnet") : QStringLiteral("sacn");
}
static DmxProtocol stringToDmxProtocol(const QString& s) {
    return s == QStringLiteral("artnet") ? DmxProtocol::ArtNet : DmxProtocol::SACN;
}

static QString dmxNetModeToString(DmxNetworkMode m) {
    switch (m) {
    case DmxNetworkMode::Unicast:   return QStringLiteral("unicast");
    case DmxNetworkMode::Broadcast: return QStringLiteral("broadcast");
    default:                        return QStringLiteral("multicast");
    }
}
static DmxNetworkMode stringToDmxNetMode(const QString& s) {
    if (s == QStringLiteral("unicast"))   return DmxNetworkMode::Unicast;
    if (s == QStringLiteral("broadcast")) return DmxNetworkMode::Broadcast;
    return DmxNetworkMode::Multicast;
}

static QJsonObject dmxUniverseConfigToJson(const DmxUniverseConfig& c) {
    QJsonObject o;
    o["universe"]  = c.universe;
    o["protocol"]  = dmxProtocolToString(c.protocol);
    o["netMode"]   = dmxNetModeToString(c.netMode);
    o["iface"]     = c.iface;
    o["unicastIp"] = c.unicastIp;
    return o;
}
static DmxUniverseConfig jsonToDmxUniverseConfig(const QJsonObject& o) {
    DmxUniverseConfig c;
    c.universe  = static_cast<quint16>(o["universe"].toInt(1));
    c.protocol  = stringToDmxProtocol(o["protocol"].toString());
    c.netMode   = stringToDmxNetMode(o["netMode"].toString());
    c.iface     = o["iface"].toString();
    c.unicastIp = o["unicastIp"].toString();
    return c;
}

static QJsonObject dmxOutputConfigToJson(const DmxOutputConfig& cfg) {
    QJsonObject o;
    o["outputMode"] = cfg.outputMode == DmxOutputMode::Replacement
                      ? QStringLiteral("replacement") : QStringLiteral("panTiltOnly");
    QJsonArray outs, ins;
    for (const auto& u : cfg.outputs) outs.append(dmxUniverseConfigToJson(u));
    for (const auto& u : cfg.inputs)  ins.append(dmxUniverseConfigToJson(u));
    o["outputs"] = outs;
    o["inputs"]  = ins;
    return o;
}
static DmxOutputConfig jsonToDmxOutputConfig(const QJsonObject& o) {
    DmxOutputConfig cfg;
    cfg.outputMode = o["outputMode"].toString() == QStringLiteral("replacement")
                     ? DmxOutputMode::Replacement : DmxOutputMode::PanTiltOnly;
    for (const auto& v : o["outputs"].toArray())
        cfg.outputs.append(jsonToDmxUniverseConfig(v.toObject()));
    for (const auto& v : o["inputs"].toArray())
        cfg.inputs.append(jsonToDmxUniverseConfig(v.toObject()));
    return cfg;
}

static QJsonObject camera2DCalibPointToJson(const Camera2DCalibPoint& p) {
    return QJsonObject{
        {"px", p.pixel.x()}, {"py", p.pixel.y()},
        {"panDmx", double(p.panDmx)}, {"tiltDmx", double(p.tiltDmx)}
    };
}
static Camera2DCalibPoint jsonToCamera2DCalibPoint(const QJsonObject& o) {
    Camera2DCalibPoint p;
    p.pixel   = QPointF(o["px"].toDouble(), o["py"].toDouble());
    p.panDmx  = float(o["panDmx"].toDouble());
    p.tiltDmx = float(o["tiltDmx"].toDouble());
    return p;
}

static QJsonObject camera2DCalibToJson(const Camera2DCalibration& c) {
    QJsonObject o;
    QJsonArray pts, hom;
    for (const auto& p : c.points) pts.append(camera2DCalibPointToJson(p));
    for (double v : c.homography) hom.append(v);
    o["points"]    = pts;
    o["homography"] = hom;
    o["valid"]     = c.valid;
    return o;
}
static Camera2DCalibration jsonToCamera2DCalib(const QJsonObject& o) {
    Camera2DCalibration c;
    for (const auto& v : o["points"].toArray())
        c.points.append(jsonToCamera2DCalibPoint(v.toObject()));
    for (const auto& v : o["homography"].toArray())
        c.homography.append(v.toDouble());
    c.valid = o["valid"].toBool(false);
    return c;
}

static QJsonObject inputAdapterMappingToJson(const InputAdapterMapping& m) {
    QJsonObject o;
    o["target"]      = m.target;
    o["universe"]    = m.universe;
    o["channel"]     = m.channel;
    o["minValue"]    = double(m.minValue);
    o["maxValue"]    = double(m.maxValue);
    o["midiPort"]    = m.midiPort;
    o["midiCC"]      = m.midiCC;
    o["midiChannel"] = m.midiChannel;
    return o;
}
static InputAdapterMapping jsonToInputAdapterMapping(const QJsonObject& o) {
    InputAdapterMapping m;
    m.target      = o["target"].toString();
    m.universe    = static_cast<quint16>(o["universe"].toInt(1));
    m.channel     = static_cast<quint16>(o["channel"].toInt(1));
    m.minValue    = float(o["minValue"].toDouble(0.0));
    m.maxValue    = float(o["maxValue"].toDouble(10.0));
    m.midiPort    = o["midiPort"].toString();
    m.midiCC      = o["midiCC"].toInt(-1);
    m.midiChannel = o["midiChannel"].toInt(1);
    return m;
}

static QJsonObject inputAdapterConfigToJson(const InputAdapterConfig& cfg) {
    QJsonObject o;
    o["type"]     = cfg.type == InputAdapterType::Midi
                    ? QStringLiteral("midi") : QStringLiteral("sacnartnet");
    o["protocol"] = dmxProtocolToString(cfg.protocol);
    o["netMode"]  = dmxNetModeToString(cfg.netMode);
    o["iface"]    = cfg.iface;
    o["unicastIp"]= cfg.unicastIp;
    o["enabled"]  = cfg.enabled;
    QJsonArray mappings;
    for (const auto& m : cfg.mappings) mappings.append(inputAdapterMappingToJson(m));
    o["mappings"] = mappings;
    return o;
}
static InputAdapterConfig jsonToInputAdapterConfig(const QJsonObject& o) {
    InputAdapterConfig cfg;
    cfg.type     = o["type"].toString() == QStringLiteral("midi")
                   ? InputAdapterType::Midi : InputAdapterType::SacnArtNet;
    cfg.protocol = stringToDmxProtocol(o["protocol"].toString());
    cfg.netMode  = stringToDmxNetMode(o["netMode"].toString());
    cfg.iface    = o["iface"].toString();
    cfg.unicastIp= o["unicastIp"].toString();
    cfg.enabled  = o["enabled"].toBool(true);
    for (const auto& v : o["mappings"].toArray())
        cfg.mappings.append(jsonToInputAdapterMapping(v.toObject()));
    return cfg;
}

// ─── FixtureUniverseConfig serialization ─────────────────────────────────────

static QJsonObject fixtureUniverseConfigToJson(const FixtureUniverseConfig& c) {
    QJsonObject o;
    o["fixtureUniverse"]  = c.fixtureUniverse;
    o["hasFollowSpots"]   = c.hasFollowSpots;
    o["inputEnabled"]     = c.inputEnabled;
    o["inputProtocol"]    = dmxProtocolToString(c.inputProtocol);
    o["inputNetMode"]     = dmxNetModeToString(c.inputNetMode);
    o["inputIface"]       = c.inputIface;
    o["inputUnicastIp"]   = c.inputUnicastIp;
    o["outputUniverse"]   = c.outputUniverse;
    o["outputEnabled"]    = c.outputEnabled;
    o["outputProtocol"]   = dmxProtocolToString(c.outputProtocol);
    o["outputNetMode"]    = dmxNetModeToString(c.outputNetMode);
    o["outputIface"]      = c.outputIface;
    o["outputUnicastIp"]  = c.outputUnicastIp;
    o["outputPriority"]   = c.outputPriority;
    return o;
}

static FixtureUniverseConfig jsonToFixtureUniverseConfig(const QJsonObject& o) {
    FixtureUniverseConfig c;
    c.fixtureUniverse  = static_cast<quint16>(o["fixtureUniverse"].toInt(1));
    c.hasFollowSpots   = o["hasFollowSpots"].toBool(false);
    c.inputEnabled     = o["inputEnabled"].toBool(true);
    c.inputProtocol    = stringToDmxProtocol(o["inputProtocol"].toString());
    c.inputNetMode     = stringToDmxNetMode(o["inputNetMode"].toString());
    c.inputIface       = o["inputIface"].toString();
    c.inputUnicastIp   = o["inputUnicastIp"].toString();
    c.outputUniverse   = o["outputUniverse"].toInt(-1);
    c.outputEnabled    = o["outputEnabled"].toBool(true);
    c.outputProtocol   = stringToDmxProtocol(o["outputProtocol"].toString());
    c.outputNetMode    = stringToDmxNetMode(o["outputNetMode"].toString());
    c.outputIface      = o["outputIface"].toString();
    c.outputUnicastIp  = o["outputUnicastIp"].toString();
    c.outputPriority   = o["outputPriority"].toInt(100);
    return c;
}

static QString operatingModeToString(OperatingMode m) {
    switch (m) {
    case OperatingMode::Camera2D:   return QStringLiteral("camera2d");
    case OperatingMode::Stage3DDMX: return QStringLiteral("stage3ddmx");
    default:                        return QStringLiteral("stage3dpsn");
    }
}
static OperatingMode stringToOperatingMode(const QString& s) {
    if (s == QStringLiteral("camera2d"))   return OperatingMode::Camera2D;
    if (s == QStringLiteral("stage3ddmx")) return OperatingMode::Stage3DDMX;
    return OperatingMode::Stage3DPSN;
}

static QString mvrRenderModeToString(MvrRenderModeEnum mode) {
    switch (mode) {
    case MvrRenderModeEnum::Flat:      return QStringLiteral("flat");
    case MvrRenderModeEnum::Shaded:    return QStringLiteral("shaded");
    case MvrRenderModeEnum::Wireframe: return QStringLiteral("wireframe");
    }
    return QStringLiteral("shaded");
}

static MvrRenderModeEnum stringToMvrRenderMode(const QString& str) {
    if (str == QStringLiteral("flat"))      return MvrRenderModeEnum::Flat;
    if (str == QStringLiteral("wireframe")) return MvrRenderModeEnum::Wireframe;
    return MvrRenderModeEnum::Shaded;
}

// ─── ZIP helpers (libarchive) ─────────────────────────────────────────────────

static void zipWriteEntry(struct archive* a, const QString& name, const QByteArray& data)
{
    struct archive_entry* entry = archive_entry_new();
    const QByteArray utf8Name = name.toUtf8();
    archive_entry_set_pathname(entry, utf8Name.constData());
    archive_entry_set_size(entry, data.size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archive_write_data(a, data.constData(), size_t(data.size()));
    archive_entry_free(entry);
}

// Returns map: entry path → bytes, for all entries in a ZIP file.
static QMap<QString, QByteArray> zipReadAll(const QString& path)
{
    QMap<QString, QByteArray> result;
    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    const QByteArray utf8 = path.toUtf8();
    if (archive_read_open_filename(a, utf8.constData(), 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return result;
    }
    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const QString name = QString::fromUtf8(archive_entry_pathname(entry));
        const la_int64_t size = archive_entry_size(entry);
        if (size > 0) {
            QByteArray data(int(size), '\0');
            archive_read_data(a, data.data(), size_t(size));
            result[name] = data;
        } else {
            archive_read_data_skip(a);
        }
    }
    archive_read_free(a);
    return result;
}

// ─── Load ─────────────────────────────────────────────────────────────────────

Project Project::load(const QString& path)
{
    const auto entries = zipReadAll(path);
    if (!entries.contains(QStringLiteral("project.json")))
        throw std::runtime_error("Invalid showfile: missing project.json");

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(entries["project.json"], &parseErr);
    if (doc.isNull() || !doc.isObject())
        throw std::runtime_error("Invalid showfile (JSON parse error): "
                                 + parseErr.errorString().toStdString());

    QJsonObject root = doc.object();
    Project p;

    p.videoSourceType       = root["videoSourceType"].toString();
    p.ndiSource             = root["ndiSource"].toString();
    p.webcamDevice          = root["webcamDevice"].toString();
    p.decklinkDevice        = root["decklinkDevice"].toString();
    p.decklinkConnection    = root["decklinkConnection"].toString();
    p.decklinkAllow10Bit    = root["decklinkAllow10Bit"].toBool(true);
    p.decklinkDisplayMode   = static_cast<quint32>(root["decklinkDisplayMode"].toInt(0));

    p.trackers.clear();
    for (const auto& tv : root["trackers"].toArray()) {
        QJsonObject to = tv.toObject();
        TrackerConfig t;
        t.id    = to["id"].toInt(1);
        t.name  = to["name"].toString();
        t.color = QColor(to["color"].toString("#ff5050"));
        if (!t.color.isValid()) t.color = QColor(255, 80, 80);
        p.trackers << t;
    }
    if (p.trackers.isEmpty())
        p.trackers = defaultProject().trackers;

    QJsonObject cal = root["calibration"].toObject();
    p.calibration.imagePoints = jsonToPointList(cal["imagePoints"].toArray());
    p.calibration.stagePoints = jsonToPointList(cal["stagePoints"].toArray());
    for (const auto& v : cal["homography"].toArray())
        p.calibration.homography << v.toDouble();
    p.calibration.elevatedImagePoints = jsonToPointList(cal["elevatedImagePoints"].toArray());
    p.calibration.markerHeight = float(cal["markerHeight"].toDouble(0.0));
    for (const auto& v : cal["projectionMatrix"].toArray())
        p.calibration.projectionMatrix << v.toDouble();

    if (p.calibration.imagePoints.size() != p.calibration.stagePoints.size()
        || (p.calibration.homography.size() != 9 && !p.calibration.homography.isEmpty()))
        p.calibration = {};

    QJsonObject net = root["network"].toObject();
    QString modeStr = net["mode"].toString("multicast");
    if (modeStr == "unicast")        p.network.psnMode = PsnMode::Unicast;
    else if (modeStr == "broadcast") p.network.psnMode = PsnMode::Broadcast;
    else                             p.network.psnMode = PsnMode::Multicast;
    p.network.multicastIp      = net["multicastIp"].toString("236.10.10.10");
    p.network.unicastIp        = net["unicastIp"].toString();
    p.network.broadcastIp      = net["broadcastIp"].toString("255.255.255.255");
    p.network.port             = static_cast<quint16>(net["port"].toInt(56565));
    p.network.psnInterface     = net["psnInterface"].toString();
    p.network.sessionInterface = net["sessionInterface"].toString();
    QJsonObject sacn = net["sacnInput"].toObject();
    p.network.sacnInput.enabled   = sacn["enabled"].toBool(false);
    p.network.sacnInput.mode      = sacn["mode"].toString() == "unicast"
                                    ? SacnMode::Unicast : SacnMode::Multicast;
    p.network.sacnInput.iface     = sacn["iface"].toString();
    p.network.sacnInput.universe  = static_cast<quint16>(sacn["universe"].toInt(1));
    p.network.sacnInput.address   = static_cast<quint16>(sacn["address"].toInt(1));
    p.network.sacnInput.minHeight = float(sacn["minHeight"].toDouble(0.0));
    p.network.sacnInput.maxHeight = float(sacn["maxHeight"].toDouble(10.0));

    QJsonObject cv = root["calibrationView"].toObject();
    p.calibrationView.showFloorGrid        = cv["showFloorGrid"].toBool(false);
    p.calibrationView.clickPlaneHeight     = float(cv["clickPlaneHeight"].toDouble(0.0));
    p.calibrationView.showClickPlane       = cv["showClickPlane"].toBool(false);
    p.calibrationView.showCalibRectInVideo = cv["showCalibRectInVideo"].toBool(true);
    p.calibrationView.showCalibRectIn3D    = cv["showCalibRectIn3D"].toBool(true);
    p.calibrationView.showCameraIn3D       = cv["showCameraIn3D"].toBool(false);
    p.calibrationView.cameraFovDeg         = float(cv["cameraFovDeg"].toDouble(60.0));

    QJsonObject stMap = root["stationTrackers"].toObject();
    for (auto it = stMap.constBegin(); it != stMap.constEnd(); ++it)
        p.stationTrackers[it.key()] = jsonToIntList(it.value().toArray());

    QJsonObject cam3d = root["stage3dCamera"].toObject();
    p.stage3dCamera.centerX = float(cam3d["centerX"].toDouble(0.0));
    p.stage3dCamera.centerY = float(cam3d["centerY"].toDouble(0.0));
    p.stage3dCamera.centerZ = float(cam3d["centerZ"].toDouble(0.0));
    p.stage3dCamera.yaw     = float(cam3d["yaw"].toDouble(0.0));
    p.stage3dCamera.pitch   = float(cam3d["pitch"].toDouble(45.0));
    p.stage3dCamera.dist    = float(cam3d["dist"].toDouble(10.0));

    for (const auto& sv : root["stageObjects"].toArray()) {
        QJsonObject so = sv.toObject();
        StageObject obj;
        obj.id       = so["id"].toInt();
        obj.name     = so["name"].toString();
        obj.color    = QColor(so["color"].toString("#64a0dc"));
        if (!obj.color.isValid()) obj.color = QColor(100, 160, 220, 180);
        obj.color.setAlpha(so["colorAlpha"].toInt(180));
        obj.height   = float(so["height"].toDouble(1.0));
        obj.isRect   = so["isRect"].toBool(true);
        obj.center   = QPointF(so["cx"].toDouble(), so["cz"].toDouble());
        obj.width    = float(so["width"].toDouble(2.0));
        obj.depth    = float(so["depth"].toDouble(1.0));
        obj.rotation = float(so["rotation"].toDouble(0.0));
        obj.polygon        = jsonToPointList(so["polygon"].toArray());
        obj.visibleInVideo = so["visibleInVideo"].toBool(true);
        obj.visibleIn3D    = so["visibleIn3D"].toBool(true);
        obj.isStageOutline = so["isStageOutline"].toBool(false);
        p.stageObjects << obj;
    }

    QJsonObject mvrJson = root["mvr"].toObject();
    p.mvr.showLabels = mvrJson["showLabels"].toBool(false);
    p.mvr.renderMode = stringToMvrRenderMode(mvrJson["renderMode"].toString("shaded"));
    int idx = 0;
    for (const auto& v : mvrJson["imports"].toArray()) {
        MvrImportData importData = jsonToMvrImport(v.toObject());
        const QString mvrKey = QString("mvr/%1.mvr").arg(idx++);
        if (entries.contains(mvrKey))
            importData.mvrData = entries[mvrKey];
        p.mvr.imports.append(importData);
    }

    p.operatingMode = stringToOperatingMode(root["operatingMode"].toString());
    if (root.contains("dmxOutput"))
        p.dmxOutput = jsonToDmxOutputConfig(root["dmxOutput"].toObject());
    if (root.contains("camera2DCalib"))
        p.camera2DCalib = jsonToCamera2DCalib(root["camera2DCalib"].toObject());
    for (const auto& v : root["inputAdapters"].toArray())
        p.inputAdapters.append(jsonToInputAdapterConfig(v.toObject()));

    for (const auto& v : root["fixtureUniverseConfigs"].toArray())
        p.fixtureUniverseConfigs.append(jsonToFixtureUniverseConfig(v.toObject()));

    return p;
}

// ─── Save ─────────────────────────────────────────────────────────────────────

void Project::save(const QString& path, std::function<void(int)> progressCb) const
{
    // ── Build project.json ────────────────────────────────────────────────────
    QJsonObject root;
    root["videoSourceType"]      = videoSourceType;
    root["ndiSource"]            = ndiSource;
    root["webcamDevice"]         = webcamDevice;
    root["decklinkDevice"]       = decklinkDevice;
    root["decklinkConnection"]   = decklinkConnection;
    root["decklinkAllow10Bit"]   = decklinkAllow10Bit;
    root["decklinkDisplayMode"]  = static_cast<int>(decklinkDisplayMode);

    QJsonArray trackerArr;
    for (const auto& t : trackers) {
        QJsonObject to;
        to["id"]    = t.id;
        to["name"]  = t.name;
        to["color"] = t.color.name();
        trackerArr << to;
    }
    root["trackers"] = trackerArr;

    QJsonObject cal;
    cal["imagePoints"] = pointListToJson(calibration.imagePoints);
    cal["stagePoints"] = pointListToJson(calibration.stagePoints);
    QJsonArray hArr;
    for (double v : calibration.homography) hArr << v;
    cal["homography"] = hArr;
    cal["elevatedImagePoints"] = pointListToJson(calibration.elevatedImagePoints);
    cal["markerHeight"] = double(calibration.markerHeight);
    QJsonArray pArr;
    for (double v : calibration.projectionMatrix) pArr << v;
    cal["projectionMatrix"] = pArr;
    root["calibration"] = cal;

    QJsonObject net;
    switch (network.psnMode) {
        case PsnMode::Unicast:   net["mode"] = "unicast";   break;
        case PsnMode::Broadcast: net["mode"] = "broadcast"; break;
        default:                 net["mode"] = "multicast"; break;
    }
    net["multicastIp"]      = network.multicastIp;
    net["unicastIp"]        = network.unicastIp;
    net["broadcastIp"]      = network.broadcastIp;
    net["port"]             = network.port;
    net["psnInterface"]     = network.psnInterface;
    net["sessionInterface"] = network.sessionInterface;
    QJsonObject sacn;
    sacn["enabled"]   = network.sacnInput.enabled;
    sacn["mode"]      = network.sacnInput.mode == SacnMode::Unicast ? "unicast" : "multicast";
    sacn["iface"]     = network.sacnInput.iface;
    sacn["universe"]  = network.sacnInput.universe;
    sacn["address"]   = network.sacnInput.address;
    sacn["minHeight"] = double(network.sacnInput.minHeight);
    sacn["maxHeight"] = double(network.sacnInput.maxHeight);
    net["sacnInput"] = sacn;
    root["network"] = net;

    QJsonObject cv;
    cv["showFloorGrid"]        = calibrationView.showFloorGrid;
    cv["clickPlaneHeight"]     = double(calibrationView.clickPlaneHeight);
    cv["showClickPlane"]       = calibrationView.showClickPlane;
    cv["showCalibRectInVideo"] = calibrationView.showCalibRectInVideo;
    cv["showCalibRectIn3D"]    = calibrationView.showCalibRectIn3D;
    cv["showCameraIn3D"]       = calibrationView.showCameraIn3D;
    cv["cameraFovDeg"]         = double(calibrationView.cameraFovDeg);
    root["calibrationView"] = cv;

    QJsonObject stMap;
    for (auto it = stationTrackers.constBegin(); it != stationTrackers.constEnd(); ++it)
        stMap[it.key()] = intListToJson(it.value());
    root["stationTrackers"] = stMap;

    QJsonArray stageArr;
    for (const auto& obj : stageObjects) {
        QJsonObject so;
        so["id"]       = obj.id;
        so["name"]     = obj.name;
        so["color"]    = obj.color.name();
        so["colorAlpha"]= obj.color.alpha();
        so["height"]   = double(obj.height);
        so["isRect"]   = obj.isRect;
        so["cx"]       = obj.center.x();
        so["cz"]       = obj.center.y();
        so["width"]    = double(obj.width);
        so["depth"]    = double(obj.depth);
        so["rotation"] = double(obj.rotation);
        so["polygon"]        = pointListToJson(obj.polygon);
        so["visibleInVideo"] = obj.visibleInVideo;
        so["visibleIn3D"]    = obj.visibleIn3D;
        so["isStageOutline"] = obj.isStageOutline;
        stageArr << so;
    }
    root["stageObjects"] = stageArr;

    QJsonObject cam3d;
    cam3d["centerX"] = double(stage3dCamera.centerX);
    cam3d["centerY"] = double(stage3dCamera.centerY);
    cam3d["centerZ"] = double(stage3dCamera.centerZ);
    cam3d["yaw"]     = double(stage3dCamera.yaw);
    cam3d["pitch"]   = double(stage3dCamera.pitch);
    cam3d["dist"]    = double(stage3dCamera.dist);
    root["stage3dCamera"] = cam3d;

    QJsonObject mvrJson;
    mvrJson["showLabels"] = mvr.showLabels;
    mvrJson["renderMode"] = mvrRenderModeToString(mvr.renderMode);
    QJsonArray importsArr;
    for (int i = 0; i < mvr.imports.size(); ++i)
        importsArr.append(mvrImportToJson(mvr.imports[i], i));
    mvrJson["imports"] = importsArr;
    root["mvr"] = mvrJson;

    root["operatingMode"] = operatingModeToString(operatingMode);
    root["dmxOutput"]     = dmxOutputConfigToJson(dmxOutput);   // kept for backward compat
    root["camera2DCalib"] = camera2DCalibToJson(camera2DCalib);
    QJsonArray adaptersArr;
    for (const auto& a : inputAdapters) adaptersArr.append(inputAdapterConfigToJson(a));
    root["inputAdapters"] = adaptersArr;                         // kept for backward compat

    QJsonArray univArr;
    for (const auto& c : fixtureUniverseConfigs) univArr.append(fixtureUniverseConfigToJson(c));
    root["fixtureUniverseConfigs"] = univArr;

    const QByteArray jsonBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    // ── Write ZIP archive ─────────────────────────────────────────────────────
    struct archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    const QByteArray utf8Path = path.toUtf8();
    if (archive_write_open_filename(a, utf8Path.constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        throw std::runtime_error("Cannot write showfile: " + path.toStdString());
    }

    if (progressCb) progressCb(-1);  // about to write project.json
    zipWriteEntry(a, QStringLiteral("project.json"), jsonBytes);

    for (int i = 0; i < mvr.imports.size(); ++i) {
        if (progressCb) progressCb(i);  // about to write MVR i
        if (!mvr.imports[i].mvrData.isEmpty())
            zipWriteEntry(a, QString("mvr/%1.mvr").arg(i), mvr.imports[i].mvrData);
    }

    archive_write_close(a);
    archive_write_free(a);
}
