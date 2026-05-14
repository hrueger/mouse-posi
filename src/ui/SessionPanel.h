#pragma once
#include <QWidget>
#include "../SessionManager.h"

class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QStackedWidget;

class SessionPanel : public QWidget {
    Q_OBJECT
public:
    explicit SessionPanel(SessionManager* mgr, QWidget* parent = nullptr);

private slots:
    void onStateChanged(SessionManager::State state);
    void onBrowsedSessionsChanged(QList<DiscoveredSession> sessions);
    void onPeerJoined(SessionPeer peer);
    void onPeerLeft(QString name);

private:
    void buildHostView();
    void buildJoinView();

    SessionManager* mgr_;

    // Host controls
    QLineEdit*    sessionNameEdit_;
    QLineEdit*    peerNameEdit_;
    QPushButton*  hostBtn_;
    QPushButton*  stopBtn_;
    QListWidget*  peersView_;

    // Join controls
    QListWidget*  sessionsView_;
    QPushButton*  joinBtn_;
    QPushButton*  leaveBtn_;

    QLabel*       statusLabel_;
    QStackedWidget* stack_;  // 0=idle, 1=hosting, 2=joined-user
};
