#include "Project.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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
        return defaultProject();
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    Project p;
    p.name      = root["name"].toString("New Project");
    p.ndiSource = root["ndiSource"].toString();

    p.trackers.clear();
    for (const auto& tv : root["trackers"].toArray()) {
        QJsonObject to = tv.toObject();
        TrackerConfig t;
        t.id    = to["id"].toInt(1);
        t.name  = to["name"].toString();
        t.color = QColor(to["color"].toString("#ff5050"));
        p.trackers << t;
    }
    if (p.trackers.isEmpty())
        p.trackers = defaultProject().trackers;

    QJsonObject cal = root["calibration"].toObject();
    p.calibration.imagePoints = jsonToPointList(cal["imagePoints"].toArray());
    p.calibration.stagePoints = jsonToPointList(cal["stagePoints"].toArray());
    for (const auto& v : cal["homography"].toArray())
        p.calibration.homography << v.toDouble();

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

    return p;
}

void Project::save(const QString& path) const {
    QJsonObject root;
    root["name"]      = name;
    root["ndiSource"] = ndiSource;

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
    root["network"] = net;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}
