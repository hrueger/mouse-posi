#include "Project.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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

Project Project::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open file: " + path.toStdString());

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    if (doc.isNull() || !doc.isObject())
        throw std::runtime_error("Invalid showfile (JSON parse error): "
                                 + parseErr.errorString().toStdString());

    QJsonObject root = doc.object();

    Project p;
    p.videoSourceType       = root["videoSourceType"].toString();
    p.ndiSource             = root["ndiSource"].toString();
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

    // Sanitize calibration: discard if point lists are mismatched or homography is wrong size
    if (p.calibration.imagePoints.size() != p.calibration.stagePoints.size()
        || (p.calibration.homography.size() != 9 && !p.calibration.homography.isEmpty())) {
        p.calibration = {};
    }

    QJsonObject net = root["network"].toObject();
    QString modeStr = net["mode"].toString("multicast");
    if (modeStr == "unicast")        p.network.psnMode = PsnMode::Unicast;
    else if (modeStr == "broadcast") p.network.psnMode = PsnMode::Broadcast;
    else                             p.network.psnMode = PsnMode::Multicast;
    p.network.multicastIp     = net["multicastIp"].toString("236.10.10.10");
    p.network.unicastIp       = net["unicastIp"].toString();
    p.network.broadcastIp     = net["broadcastIp"].toString("255.255.255.255");
    p.network.port            = static_cast<quint16>(net["port"].toInt(56565));
    p.network.psnInterface    = net["psnInterface"].toString();
    p.network.sessionInterface= net["sessionInterface"].toString();
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

    return p;
}

void Project::save(const QString& path) const {
    QJsonObject root;
    root["videoSourceType"]      = videoSourceType;
    root["ndiSource"]            = ndiSource;
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

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}
