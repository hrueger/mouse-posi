#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include "Project.h"
#include "DnsSdBridge.h"

enum class SessionRole { Admin, User };

struct SessionPeer {
    QString     displayName;
    QString     address;
    quint16     tcpPort   = 0;
    SessionRole role      = SessionRole::User;
    bool        isMaster  = false;
    QList<int>  assignedTrackerIds;
};

struct DiscoveredSession {
    QString name;
    QString host;
    quint16 port = 0;
};
Q_DECLARE_METATYPE(DiscoveredSession)

class PeerConnection;

class SessionManager : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Hosting, Joining, Joined };

    explicit SessionManager(QObject* parent = nullptr);
    ~SessionManager() override;

    void startHosting(const QString& sessionName, const QString& peerName,
                      const QString& iface = {});
    void stopHosting();
    void joinSession(const QString& host, quint16 port, const QString& peerName);
    void leaveSession();

    // Admin operations
    void setTrackerAccess(const QString& peerName, const QList<int>& ids);
    void promoteToAdmin(const QString& peerName);
    void setUnassignedAlpha(int alpha);
    void broadcastProjectState(const Project& p);

    State       state()       const { return state_; }
    SessionRole localRole()   const { return localRole_; }
    bool        isMaster()    const { return isMaster_; }
    QList<SessionPeer>       peers()             const;
    QList<DiscoveredSession> discoveredSessions() const;
    QList<int>  localAssignedTrackers() const { return localAssignedTrackers_; }
    int         unassignedAlpha()       const { return unassignedAlpha_; }

    void startBrowsing();
    void stopBrowsing();

signals:
    void stateChanged(SessionManager::State);
    void peerJoined(SessionPeer);
    void peerLeft(QString name);
    void peerRoleChanged(SessionPeer);
    void projectStateReceived(Project p, int unassignedAlpha);
    void browsedSessionsChanged(QList<DiscoveredSession>);
    void trackerAccessReceived(QList<int> assignedIds);
    void unassignedAlphaChanged(int alpha);
    void errorOccurred(QString message);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onServerReadyRead();
    void onServerDisconnected();

private:
    struct ConnectedPeer {
        SessionPeer  info;
        QTcpSocket*  socket = nullptr;
        QByteArray   readBuf;
    };

    void sendMessage(QTcpSocket* socket, const QJsonObject& msg);
    void broadcastMessage(const QJsonObject& msg, QTcpSocket* exclude = nullptr);
    void processMessage(QTcpSocket* socket, const QJsonObject& msg);
    void processClientMessage(const QJsonObject& msg);
    void broadcastPeerList();
    void handleMasterLeft();
    Project projectFromJson(const QJsonObject& obj) const;
    QJsonObject projectToJson(const Project& p) const;

    State       state_       = State::Idle;
    SessionRole localRole_   = SessionRole::Admin;
    bool        isMaster_    = true;
    QString     localName_;
    QString     sessionName_;
    QList<int>  localAssignedTrackers_;
    int         unassignedAlpha_ = 80;
    Project     sharedProject_;

    QTcpServer*           server_    = nullptr;
    QTcpSocket*           clientSock_= nullptr;
    QByteArray            clientBuf_;
    QList<ConnectedPeer>  connectedPeers_;

    DnsSdBridge*          dns_ = nullptr;
    QList<DiscoveredSession> discovered_;
};
