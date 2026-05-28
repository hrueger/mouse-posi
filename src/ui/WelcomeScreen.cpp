#include "WelcomeScreen.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QFileInfo>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QSettings>
#include <QNetworkInterface>
#include <QAbstractSocket>

static QFrame* makeVSep()
{
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    return sep;
}

static QLabel* makeColTitle(const QString& text)
{
    auto* lbl = new QLabel(text);
    QFont f = lbl->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    lbl->setFont(f);
    return lbl;
}

WelcomeScreen::WelcomeScreen(SessionManager* mgr, QWidget* parent)
    : QWidget(parent), mgr_(mgr)
{
    // ── Outer centering layout ─────────────────────────────────────────────
    auto* outerV = new QVBoxLayout(this);
    outerV->setContentsMargins(0, 0, 0, 0);

    auto* appTitle = new QLabel("OnPoint");
    {
        QFont f = appTitle->font();
        f.setPointSize(28);
        f.setBold(true);
        appTitle->setFont(f);
        appTitle->setAlignment(Qt::AlignCenter);
    }

    auto* centerH = new QHBoxLayout;
    outerV->addStretch(1);
    outerV->addWidget(appTitle);
    outerV->addSpacing(16);
    outerV->addLayout(centerH);
    outerV->addStretch(2);

    centerH->addStretch();

    // ── Card ──────────────────────────────────────────────────────────────
    auto* card = new QFrame;
    card->setFrameShape(QFrame::Box);
    card->setFrameShadow(QFrame::Plain);
    card->setLineWidth(1);

    auto* cardH = new QHBoxLayout(card);
    cardH->setContentsMargins(0, 0, 0, 0);
    cardH->setSpacing(0);

    // Helper: equal-width column
    constexpr int kColMin = 220;
    auto makeCol = [kColMin](QWidget** container) -> QVBoxLayout* {
        *container = new QWidget;
        (*container)->setMinimumWidth(kColMin);
        auto* lay = new QVBoxLayout(*container);
        lay->setContentsMargins(24, 24, 24, 24);
        lay->setSpacing(8);
        return lay;
    };

    // ── Column 1: New / Open ──────────────────────────────────────────────
    QWidget* col1;
    auto* lay1 = makeCol(&col1);

    lay1->addWidget(makeColTitle("New"));
    lay1->addSpacing(4);

    auto* newBtn = new QPushButton("New Showfile");
    newBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay1->addWidget(newBtn);
    connect(newBtn, &QPushButton::clicked, this, &WelcomeScreen::newRequested);

    auto* openBtn = new QPushButton("Open Showfile...");
    openBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay1->addWidget(openBtn);
    connect(openBtn, &QPushButton::clicked, this, [this]() { emit openRequested({}); });

    lay1->addStretch();

    // ── Column 2: Recent ─────────────────────────────────────────────────
    QWidget* col2;
    auto* lay2 = makeCol(&col2);

    lay2->addWidget(makeColTitle("Recent"));
    lay2->addSpacing(4);

    recentView_ = new QListWidget;
    recentView_->setFrameShape(QFrame::NoFrame);
    recentView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recentView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay2->addWidget(recentView_);

    connect(recentView_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit openRequested(item->data(Qt::UserRole).toString());
    });

    // ── Column 3: Join Session ────────────────────────────────────────────
    QWidget* col3;
    auto* lay3 = makeCol(&col3);

    lay3->addWidget(makeColTitle("Join Session"));
    lay3->addSpacing(4);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    peerNameEdit_ = new QLineEdit;
    peerNameEdit_->setPlaceholderText("Station 1");
    {
        QSettings s("onpoint", "onpoint");
        peerNameEdit_->setText(s.value("peerName", "").toString());
    }
    form->addRow("My name:", peerNameEdit_);

    ifaceCombo_ = new QComboBox;
    ifaceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    populateInterfaces();
    form->addRow("Interface:", ifaceCombo_);

    lay3->addLayout(form);
    lay3->addSpacing(8);

    // Sessions label + refresh button on the same row
    auto* sessHeaderRow = new QHBoxLayout;
    auto* sessLabel = new QLabel("Available sessions:");
    sessLabel->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    auto* refreshBtn = new QPushButton;
    refreshBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshBtn->setFixedWidth(28);
    refreshBtn->setToolTip("Refresh sessions");
    sessHeaderRow->addWidget(sessLabel);
    sessHeaderRow->addStretch();
    sessHeaderRow->addWidget(refreshBtn);
    lay3->addLayout(sessHeaderRow);

    sessionsView_ = new QListWidget;
    sessionsView_->setMinimumHeight(80);
    sessionsView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay3->addWidget(sessionsView_);

    joinBtn_ = new QPushButton("Join");
    joinBtn_->setEnabled(false);
    joinBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay3->addWidget(joinBtn_);

    // ── Assemble card ─────────────────────────────────────────────────────
    cardH->addWidget(col1, 1);
    cardH->addWidget(makeVSep());
    cardH->addWidget(col2, 1);
    cardH->addWidget(makeVSep());
    cardH->addWidget(col3, 1);

    centerH->addWidget(card);
    centerH->addStretch();

    // ── Connections ───────────────────────────────────────────────────────
    connect(sessionsView_, &QListWidget::itemSelectionChanged,
            this, &WelcomeScreen::updateJoinButton);

    connect(refreshBtn, &QPushButton::clicked, this, &WelcomeScreen::refreshSessions);

    connect(joinBtn_, &QPushButton::clicked, this, [this]() {
        auto* item = sessionsView_->currentItem();
        if (!item) return;
        auto disc = item->data(Qt::UserRole).value<DiscoveredSession>();
        QString peer = peerNameEdit_->text().trimmed();
        if (peer.isEmpty()) peer = peerNameEdit_->placeholderText();
        QSettings s("onpoint", "onpoint");
        s.setValue("peerName", peer);
        emit joinRequested(peer, ifaceCombo_->currentData().toString(), disc);
    });

    connect(mgr_, &SessionManager::browsedSessionsChanged,
            this, [this](QList<DiscoveredSession> sessions) {
        sessionsView_->clear();
        for (const auto& s : sessions) {
            auto* item = new QListWidgetItem(s.name);
            item->setData(Qt::UserRole, QVariant::fromValue(s));
            sessionsView_->addItem(item);
        }
        updateJoinButton();
    });

    // Populate with any sessions already discovered
    for (const auto& s : mgr_->discoveredSessions()) {
        auto* item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, QVariant::fromValue(s));
        sessionsView_->addItem(item);
    }
    updateJoinButton();
}

void WelcomeScreen::populateInterfaces()
{
    ifaceCombo_->clear();
    ifaceCombo_->addItem("(Default)", QString());
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
        QString ipv4;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4 = entry.ip().toString(); break;
            }
        }
        if (ipv4.isEmpty()) continue;
        ifaceCombo_->addItem(iface.humanReadableName() + " — " + ipv4, iface.name());
    }
}

void WelcomeScreen::refreshSessions()
{
    sessionsView_->clear();
    updateJoinButton();
    mgr_->startBrowsing();
}

void WelcomeScreen::updateJoinButton()
{
    joinBtn_->setEnabled(sessionsView_->currentItem() != nullptr);
}

void WelcomeScreen::refresh(const QStringList& recentFiles)
{
    recentPaths_ = recentFiles;
    recentView_->clear();
    if (recentFiles.isEmpty()) {
        auto* item = new QListWidgetItem("No recent files.");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        recentView_->addItem(item);
        return;
    }
    for (const QString& path : recentFiles) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        recentView_->addItem(item);
    }
}
