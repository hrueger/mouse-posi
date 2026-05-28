#pragma once
#include <QWidget>
#include <QStringList>
#include "../SessionManager.h"

class QLabel;
class QVBoxLayout;
class QComboBox;
class QListWidget;
class QLineEdit;
class QPushButton;

class WelcomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeScreen(SessionManager* mgr, QWidget* parent = nullptr);
    void refresh(const QStringList& recentFiles);

signals:
    void newRequested();
    void openRequested(const QString& path); // empty = show open dialog
    void joinRequested(const QString& peerName, const QString& iface, DiscoveredSession session);

private:
    void populateInterfaces();
    void updateJoinButton();

    void refreshSessions();

    SessionManager* mgr_;
    QListWidget*    recentView_    = nullptr;
    QStringList     recentPaths_;
    QComboBox*      ifaceCombo_    = nullptr;
    QListWidget*    sessionsView_  = nullptr;
    QLineEdit*      peerNameEdit_  = nullptr;
    QPushButton*    joinBtn_       = nullptr;
};
