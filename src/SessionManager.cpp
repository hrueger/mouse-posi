#include "SessionManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDataStream>
#include <QHostAddress>
#include <QNetworkInterface>

// ── Framing: [4-byte big-endian length][JSON] ────────────────────────────────

static void writeFrame(QTcpSocket* s, const QByteArray& data) {
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << static_cast<quint32>(data.size());
    frame += data;
    s->write(frame);
}

// Returns true when a complete frame has been consumed and written to *out*.
static bool readFrame(QByteArray& buf, QByteArray& out) {
    if (buf.size() < 4) return false;
    quint32 len = (static_cast<quint8>(buf[0]) << 24) |
                  (static_cast<quint8>(buf[1]) << 16) |
                  (static_cast<quint8>(buf[2]) << 8)  |
                   static_cast<quint8>(buf[3]);
    if (static_cast<quint32>(buf.size()) < 4 + len) return false;
    out = buf.mid(4, len);
    buf.remove(0, 4 + len);
    return true;
}

// ── Constructor / destructor ──────────────────────────────────────────────────

SessionManager::SessionManager(QObject* parent) : QObject(parent) {
    dns_ = new DnsSdBridge(this);

    connect(dns_, &DnsSdBridge::serviceFound,
            this, [this](QString name, QString host, quint16 port) {
        for (auto& d : discovered_) {
            if (d.name == name) { d.host = host; d.port = port;
                emit browsedSessionsChanged(discovered_); return; }
        }
        discovered_.push_back({name, host, port});
        emit browsedSessionsChanged(discovered_);
    });
    connect(dns_, &DnsSdBridge::serviceLost,
            this, [this](QString name) {
        discovered_.erase(
            std::remove_if(discovered_.begin(), discovered_.end(),
                           [&](const DiscoveredSession& d){ return d.name == name; }),
            discovered_.end());
        emit browsedSessionsChanged(discovered_);
    });
    connect(dns_, &DnsSdBridge::advertiseError,
            this, [this](QString msg) { emit errorOccurred("DNS-SD: " + msg); });
}

SessionManager::~SessionManager() {
    blockSignals(true);
    stopHosting();
    leaveSession();
    stopBrowsing();
}

// ── Browse ────────────────────────────────────────────────────────────────────

void SessionManager::startBrowsing() { dns_->browse(); }
void SessionManager::stopBrowsing()  { dns_->stopBrowsing(); discovered_.clear(); }

QList<DiscoveredSession> SessionManager::discoveredSessions() const { return discovered_; }

// ── Host ──────────────────────────────────────────────────────────────────────

void SessionManager::startHosting(const QString& sessionName,
                                   const QString& peerName,
                                   const QString& iface)
{
    stopHosting();

    localName_    = peerName;
    sessionName_  = sessionName;
    localRole_    = SessionRole::Admin;
    isMaster_     = true;

    server_ = new QTcpServer(this);

    QHostAddress bindAddr = QHostAddress::AnyIPv4;
    if (!iface.isEmpty()) {
        for (const auto& ni : QNetworkInterface::allInterfaces()) {
            if (ni.name() == iface) {
                for (const auto& ae : ni.addressEntries()) {
                    if (ae.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                        bindAddr = ae.ip(); break;
                    }
                }
            }
        }
    }

    if (!server_->listen(bindAddr)) {
        emit errorOccurred("Could not start TCP server: " + server_->errorString());
        server_->deleteLater(); server_ = nullptr; return;
    }

    connect(server_, &QTcpServer::newConnection, this, &SessionManager::onNewConnection);

    dns_->advertise(sessionName, server_->serverPort());

    state_ = State::Hosting;
    emit stateChanged(state_);
}

void SessionManager::stopHosting() {
    if (state_ != State::Hosting) return;
    dns_->stopAdvertising();

    QJsonObject msg; msg["type"] = "leave";
    broadcastMessage(msg);

    for (auto& cp : connectedPeers_) {
        cp.socket->disconnectFromHost();
        cp.socket->deleteLater();
    }
    connectedPeers_.clear();

    if (server_) { server_->close(); server_->deleteLater(); server_ = nullptr; }

    state_ = State::Idle;
    emit stateChanged(state_);
}

// ── Join ──────────────────────────────────────────────────────────────────────

void SessionManager::joinSession(const QString& host, quint16 port,
                                  const QString& peerName)
{
    leaveSession();
    localName_ = peerName;
    state_     = State::Joining;
    emit stateChanged(state_);

    clientSock_ = new QTcpSocket(this);
    connect(clientSock_, &QTcpSocket::readyRead,   this, &SessionManager::onServerReadyRead);
    connect(clientSock_, &QTcpSocket::disconnected,this, &SessionManager::onServerDisconnected);
    connect(clientSock_, &QTcpSocket::connected,   this, [this]() {
        QJsonObject hello; hello["type"] = "hello"; hello["peerName"] = localName_;
        sendMessage(clientSock_, hello);
    });
    connect(clientSock_, &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred("Connection failed: " + clientSock_->errorString());
        state_ = State::Idle;
        emit stateChanged(state_);
    });

    clientSock_->connectToHost(host, port);
}

void SessionManager::leaveSession() {
    if (state_ == State::Idle) return;
    if (clientSock_) {
        QJsonObject msg; msg["type"] = "leave";
        sendMessage(clientSock_, msg);
        clientSock_->disconnectFromHost();
        clientSock_->deleteLater(); clientSock_ = nullptr;
    }
    clientBuf_.clear();
    state_ = State::Idle;
    emit stateChanged(state_);
}

// ── Admin ops ─────────────────────────────────────────────────────────────────

void SessionManager::setTrackerAccess(const QString& peerName, const QList<int>& ids) {
    for (auto& cp : connectedPeers_) {
        if (cp.info.displayName == peerName) {
            cp.info.assignedTrackerIds = ids;
            QJsonArray arr;
            for (int id : ids) arr << id;
            QJsonObject msg; msg["type"] = "tracker_access"; msg["trackerIds"] = arr;
            sendMessage(cp.socket, msg);
            broadcastPeerList();
            break;
        }
    }
}

void SessionManager::promoteToAdmin(const QString& peerName) {
    for (auto& cp : connectedPeers_) {
        if (cp.info.displayName == peerName) {
            cp.info.role = SessionRole::Admin;
            QJsonObject msg; msg["type"] = "role_change"; msg["newRole"] = "admin";
            sendMessage(cp.socket, msg);
            broadcastPeerList();
            emit peerRoleChanged(cp.info);
            break;
        }
    }
}

void SessionManager::setUnassignedAlpha(int alpha) {
    unassignedAlpha_ = alpha;
    QJsonObject msg; msg["type"] = "unassigned_alpha"; msg["alpha"] = alpha;
    broadcastMessage(msg);
    emit unassignedAlphaChanged(alpha);
}

void SessionManager::broadcastProjectState(const Project& p) {
    sharedProject_ = p;
    QJsonObject msg; msg["type"] = "project_update";
    msg["project"] = projectToJson(p);
    msg["unassignedAlpha"] = unassignedAlpha_;
    broadcastMessage(msg);
}

QList<SessionPeer> SessionManager::peers() const {
    QList<SessionPeer> result;
    for (const auto& cp : connectedPeers_) result << cp.info;
    return result;
}

// ── Server side: handle incoming connections ──────────────────────────────────

void SessionManager::onNewConnection() {
    while (server_ && server_->hasPendingConnections()) {
        auto* sock = server_->nextPendingConnection();
        ConnectedPeer cp;
        cp.socket = sock;
        cp.info.address = sock->peerAddress().toString();
        cp.info.tcpPort = sock->peerPort();
        cp.info.role    = SessionRole::User;
        connectedPeers_ << cp;

        connect(sock, &QTcpSocket::readyRead,   this, &SessionManager::onClientReadyRead);
        connect(sock, &QTcpSocket::disconnected,this, &SessionManager::onClientDisconnected);
    }
}

void SessionManager::onClientReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    ConnectedPeer* cp = nullptr;
    for (auto& p : connectedPeers_) if (p.socket == sock) { cp = &p; break; }
    if (!cp) return;

    cp->readBuf += sock->readAll();
    QByteArray frame;
    while (readFrame(cp->readBuf, frame)) {
        auto doc = QJsonDocument::fromJson(frame);
        if (doc.isObject()) processMessage(sock, doc.object());
    }
}

void SessionManager::onClientDisconnected() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    QString leftName;
    bool wasMaster = false;
    connectedPeers_.erase(
        std::remove_if(connectedPeers_.begin(), connectedPeers_.end(),
                       [&](const ConnectedPeer& cp) {
                           if (cp.socket == sock) {
                               leftName  = cp.info.displayName;
                               wasMaster = cp.info.isMaster;
                               sock->deleteLater();
                               return true;
                           }
                           return false;
                       }),
        connectedPeers_.end());

    if (!leftName.isEmpty()) {
        emit peerLeft(leftName);
        broadcastPeerList();
    }
}

// ── Server side: process message from a client ────────────────────────────────

void SessionManager::processMessage(QTcpSocket* sock, const QJsonObject& msg) {
    QString type = msg["type"].toString();

    ConnectedPeer* cp = nullptr;
    for (auto& p : connectedPeers_) if (p.socket == sock) { cp = &p; break; }
    if (!cp) return;

    if (type == "hello") {
        cp->info.displayName = msg["peerName"].toString();

        // Send welcome with current project state
        QJsonObject welcome;
        welcome["type"]           = "welcome";
        welcome["role"]           = "user";
        welcome["sessionName"]    = sessionName_;
        welcome["project"]        = projectToJson(sharedProject_);
        welcome["unassignedAlpha"]= unassignedAlpha_;
        sendMessage(sock, welcome);

        emit peerJoined(cp->info);
        broadcastPeerList();

    } else if (type == "leave") {
        // Will be cleaned up on disconnect signal

    } else if (type == "promote") {
        // Only admins may send this
        if (cp->info.role == SessionRole::Admin)
            promoteToAdmin(msg["targetPeer"].toString());

    } else if (type == "set_tracker_access") {
        if (cp->info.role == SessionRole::Admin) {
            QList<int> ids;
            for (auto v : msg["trackerIds"].toArray()) ids << v.toInt();
            setTrackerAccess(msg["targetPeer"].toString(), ids);
        }
    }
}

// ── Client side: handle messages from server ─────────────────────────────────

void SessionManager::onServerReadyRead() {
    if (!clientSock_) return;
    clientBuf_ += clientSock_->readAll();
    QByteArray frame;
    while (readFrame(clientBuf_, frame)) {
        auto doc = QJsonDocument::fromJson(frame);
        if (doc.isObject()) processClientMessage(doc.object());
    }
}

void SessionManager::processClientMessage(const QJsonObject& msg) {
    QString type = msg["type"].toString();

    if (type == "welcome") {
        localRole_   = msg["role"].toString() == "admin"
                       ? SessionRole::Admin : SessionRole::User;
        sessionName_ = msg["sessionName"].toString();
        state_       = State::Joined;
        emit stateChanged(state_);

        Project p = projectFromJson(msg["project"].toObject());
        int alpha = msg["unassignedAlpha"].toInt(80);
        emit projectStateReceived(p, alpha);

    } else if (type == "project_update") {
        Project p = projectFromJson(msg["project"].toObject());
        int alpha = msg["unassignedAlpha"].toInt(80);
        emit projectStateReceived(p, alpha);

    } else if (type == "tracker_access") {
        QList<int> ids;
        for (auto v : msg["trackerIds"].toArray()) ids << v.toInt();
        localAssignedTrackers_ = ids;
        emit trackerAccessReceived(ids);

    } else if (type == "role_change") {
        localRole_ = msg["newRole"].toString() == "admin"
                     ? SessionRole::Admin : SessionRole::User;
        SessionPeer self; self.displayName = localName_; self.role = localRole_;
        emit peerRoleChanged(self);
        emit stateChanged(state_);  // triggers UI update for role change

    } else if (type == "unassigned_alpha") {
        unassignedAlpha_ = msg["alpha"].toInt(80);
        emit unassignedAlphaChanged(unassignedAlpha_);

    } else if (type == "peer_list") {
        // Update local view of who's in the session
        connectedPeers_.clear();
        for (auto v : msg["peers"].toArray()) {
            auto o = v.toObject();
            ConnectedPeer cp;
            cp.info.displayName = o["name"].toString();
            cp.info.role = o["role"].toString() == "admin"
                           ? SessionRole::Admin : SessionRole::User;
            for (auto id : o["trackerIds"].toArray())
                cp.info.assignedTrackerIds << id.toInt();
            connectedPeers_ << cp;
        }

    } else if (type == "leave") {
        // Server gone
        onServerDisconnected();
    }
}

void SessionManager::onServerDisconnected() {
    clientBuf_.clear();
    if (clientSock_) { clientSock_->deleteLater(); clientSock_ = nullptr; }
    state_ = State::Idle;
    emit stateChanged(state_);
    emit errorOccurred("Disconnected from session host.");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void SessionManager::sendMessage(QTcpSocket* socket, const QJsonObject& msg) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    writeFrame(socket, QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void SessionManager::broadcastMessage(const QJsonObject& msg, QTcpSocket* exclude) {
    for (const auto& cp : connectedPeers_) {
        if (cp.socket != exclude) sendMessage(cp.socket, msg);
    }
}

void SessionManager::broadcastPeerList() {
    QJsonArray arr;
    for (const auto& cp : connectedPeers_) {
        QJsonObject o;
        o["name"] = cp.info.displayName;
        o["role"] = cp.info.role == SessionRole::Admin ? "admin" : "user";
        QJsonArray ids;
        for (int id : cp.info.assignedTrackerIds) ids << id;
        o["trackerIds"] = ids;
        arr << o;
    }
    QJsonObject msg; msg["type"] = "peer_list"; msg["peers"] = arr;
    broadcastMessage(msg);
}

QJsonObject SessionManager::projectToJson(const Project& p) const {
    QJsonObject obj;
    obj["ndiSource"] = p.ndiSource;
    QJsonArray trackers;
    for (const auto& t : p.trackers) {
        QJsonObject to;
        to["id"] = t.id; to["name"] = t.name; to["color"] = t.color.name();
        trackers << to;
    }
    obj["trackers"] = trackers;
    QJsonArray hArr;
    for (double v : p.calibration.homography) hArr << v;
    obj["homography"] = hArr;
    QJsonArray imgPts, stagePts;
    for (auto& pt : p.calibration.imagePoints) {
        QJsonArray a; a << pt.x() << pt.y(); imgPts << a;
    }
    for (auto& pt : p.calibration.stagePoints) {
        QJsonArray a; a << pt.x() << pt.y(); stagePts << a;
    }
    obj["imagePoints"] = imgPts;
    obj["stagePoints"] = stagePts;
    QJsonObject net;
    net["psnMode"]          = static_cast<int>(p.network.psnMode);
    net["multicastIp"]      = p.network.multicastIp;
    net["unicastIp"]        = p.network.unicastIp;
    net["broadcastIp"]      = p.network.broadcastIp;
    net["port"]             = p.network.port;
    net["psnInterface"]     = p.network.psnInterface;
    net["sessionInterface"] = p.network.sessionInterface;
    obj["network"] = net;
    return obj;
}

Project SessionManager::projectFromJson(const QJsonObject& obj) const {
    Project p;
    p.ndiSource = obj["ndiSource"].toString();
    for (auto v : obj["trackers"].toArray()) {
        auto to = v.toObject();
        TrackerConfig t;
        t.id = to["id"].toInt(); t.name = to["name"].toString();
        t.color = QColor(to["color"].toString());
        p.trackers << t;
    }
    for (auto v : obj["homography"].toArray())
        p.calibration.homography << v.toDouble();
    for (auto v : obj["imagePoints"].toArray()) {
        auto a = v.toArray();
        if (a.size() >= 2) p.calibration.imagePoints << QPointF(a[0].toDouble(), a[1].toDouble());
    }
    for (auto v : obj["stagePoints"].toArray()) {
        auto a = v.toArray();
        if (a.size() >= 2) p.calibration.stagePoints << QPointF(a[0].toDouble(), a[1].toDouble());
    }
    if (obj.contains("network")) {
        auto net = obj["network"].toObject();
        p.network.psnMode         = static_cast<PsnMode>(net["psnMode"].toInt());
        p.network.multicastIp     = net["multicastIp"].toString(p.network.multicastIp);
        p.network.unicastIp       = net["unicastIp"].toString();
        p.network.broadcastIp     = net["broadcastIp"].toString(p.network.broadcastIp);
        p.network.port            = static_cast<quint16>(net["port"].toInt(p.network.port));
        p.network.psnInterface    = net["psnInterface"].toString();
        p.network.sessionInterface= net["sessionInterface"].toString();
    }
    return p;
}
