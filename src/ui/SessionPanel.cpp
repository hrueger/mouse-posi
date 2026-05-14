#include "SessionPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QSettings>

SessionPanel::SessionPanel(SessionManager* mgr, QWidget* parent)
    : QWidget(parent), mgr_(mgr)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    statusLabel_ = new QLabel("No active session.");
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    layout->addWidget(statusLabel_);

    stack_ = new QStackedWidget;

    // ── Page 0: Idle — host or join ──────────────────────────────────────
    auto* idlePage = new QWidget;
    auto* idleLayout = new QVBoxLayout(idlePage);
    idleLayout->setContentsMargins(0, 0, 0, 0);
    idleLayout->setSpacing(4);

    auto* hostFL = new QFormLayout;
    hostFL->setContentsMargins(0, 0, 0, 0);
    hostFL->setSpacing(4);
    sessionNameEdit_ = new QLineEdit("My Session");
    sessionNameEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    peerNameEdit_    = new QLineEdit;
    peerNameEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QSettings s("mouse-posi", "mouse-posi");
    peerNameEdit_->setText(s.value("peerName", "Station 1").toString());
    hostFL->addRow("Session:", sessionNameEdit_);
    hostFL->addRow("My name:", peerNameEdit_);
    idleLayout->addLayout(hostFL);
    hostBtn_ = new QPushButton("Start Hosting");
    idleLayout->addWidget(hostBtn_);

    idleLayout->addSpacing(6);
    idleLayout->addWidget(new QLabel("Sessions:"));
    sessionsView_ = new QListWidget;
    sessionsView_->setMinimumWidth(0);
    sessionsView_->setMaximumHeight(100);
    joinBtn_ = new QPushButton("Join Selected");
    joinBtn_->setEnabled(false);
    idleLayout->addWidget(sessionsView_);
    idleLayout->addWidget(joinBtn_);

    idleLayout->addStretch();
    stack_->addWidget(idlePage);  // index 0

    // ── Page 1: Hosting ───────────────────────────────────────────────────
    auto* hostPage = new QWidget;
    auto* hostPageLayout = new QVBoxLayout(hostPage);
    hostPageLayout->setContentsMargins(0, 0, 0, 0);
    peersView_ = new QListWidget;
    peersView_->setMinimumWidth(0);
    peersView_->setMaximumHeight(120);
    stopBtn_ = new QPushButton("Stop Hosting");
    hostPageLayout->addWidget(new QLabel("Connected peers:"));
    hostPageLayout->addWidget(peersView_);
    hostPageLayout->addWidget(stopBtn_);
    hostPageLayout->addStretch();
    stack_->addWidget(hostPage);  // index 1

    // ── Page 2: Joined as user ────────────────────────────────────────────
    auto* joinedPage = new QWidget;
    auto* joinedLayout = new QVBoxLayout(joinedPage);
    joinedLayout->setContentsMargins(0, 0, 0, 0);
    leaveBtn_ = new QPushButton("Leave Session");
    joinedLayout->addWidget(leaveBtn_);
    joinedLayout->addStretch();
    stack_->addWidget(joinedPage);  // index 2

    layout->addWidget(stack_);

    // ── Connections ───────────────────────────────────────────────────────
    connect(hostBtn_, &QPushButton::clicked, this, [this]() {
        QString name = sessionNameEdit_->text().trimmed();
        QString peer = peerNameEdit_->text().trimmed();
        if (name.isEmpty()) name = "My Session";
        if (peer.isEmpty()) peer = "Station 1";
        QSettings s("mouse-posi", "mouse-posi");
        s.setValue("peerName", peer);
        mgr_->startHosting(name, peer);
    });
    connect(stopBtn_, &QPushButton::clicked, mgr_, &SessionManager::stopHosting);
    connect(leaveBtn_, &QPushButton::clicked, mgr_, &SessionManager::leaveSession);

    connect(joinBtn_, &QPushButton::clicked, this, [this]() {
        auto* item = sessionsView_->currentItem();
        if (!item) return;
        auto disc = item->data(Qt::UserRole).value<DiscoveredSession>();
        QString peer = peerNameEdit_->text().trimmed();
        if (peer.isEmpty()) peer = "Station 1";
        mgr_->joinSession(disc.host, disc.port, peer);
    });
    connect(sessionsView_, &QListWidget::itemSelectionChanged, this, [this]() {
        joinBtn_->setEnabled(sessionsView_->currentItem() != nullptr);
    });

    connect(mgr_, &SessionManager::stateChanged,          this, &SessionPanel::onStateChanged);
    connect(mgr_, &SessionManager::browsedSessionsChanged, this, &SessionPanel::onBrowsedSessionsChanged);
    connect(mgr_, &SessionManager::peerJoined,            this, &SessionPanel::onPeerJoined);
    connect(mgr_, &SessionManager::peerLeft,              this, &SessionPanel::onPeerLeft);

    mgr_->startBrowsing();
}

void SessionPanel::onStateChanged(SessionManager::State state) {
    switch (state) {
        case SessionManager::State::Idle:
            stack_->setCurrentIndex(0);
            statusLabel_->setText("No active session.");
            break;
        case SessionManager::State::Hosting:
            stack_->setCurrentIndex(1);
            statusLabel_->setText(QString("Hosting: %1").arg(sessionNameEdit_->text()));
            break;
        case SessionManager::State::Joining:
            statusLabel_->setText("Connecting…");
            break;
        case SessionManager::State::Joined:
            if (mgr_->localRole() == SessionRole::Admin)
                stack_->setCurrentIndex(1);
            else
                stack_->setCurrentIndex(2);
            statusLabel_->setText(QString("Joined as %1")
                .arg(mgr_->localRole() == SessionRole::Admin ? "Admin" : "User"));
            break;
    }
}

void SessionPanel::onBrowsedSessionsChanged(QList<DiscoveredSession> sessions) {
    sessionsView_->clear();
    for (const auto& s : sessions) {
        auto* item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, QVariant::fromValue(s));
        sessionsView_->addItem(item);
    }
    joinBtn_->setEnabled(false);
}

void SessionPanel::onPeerJoined(SessionPeer peer) {
    peersView_->addItem(QString("[%1] %2")
        .arg(peer.role == SessionRole::Admin ? "Admin" : "User")
        .arg(peer.displayName));
}

void SessionPanel::onPeerLeft(QString name) {
    auto items = peersView_->findItems(name, Qt::MatchContains);
    for (auto* item : items) delete item;
}
