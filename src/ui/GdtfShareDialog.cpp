#include "GdtfShareDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QStackedWidget>
#include <QSplitter>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QSettings>
#include <QCheckBox>
#include <QTimer>
#include "../GdtfLibrary.h"

static const char* kLoginUrl    = "https://gdtf-share.com/apis/public/login.php";
static const char* kListUrl     = "https://gdtf-share.com/apis/public/getList.php";
static const char* kDownloadUrl = "https://gdtf-share.com/apis/public/downloadFile.php";

// ─── Parse the modes array (format varies) ───────────────────────────────────
static QString parseModes(const QJsonValue& modesVal)
{
    QStringList parts;

    auto tryMode = [&](const QJsonValue& v) {
        if (v.isObject()) {
            const auto o = v.toObject();
            if (o.contains("name") && o.contains("dmxfootprint")) {
                parts << QString("%1 (%2ch)").arg(o["name"].toString()).arg(o["dmxfootprint"].toInt());
                return;
            }
            for (const auto& key : o.keys()) {
                for (const auto& el : o[key].toArray()) {
                    if (el.isObject()) {
                        const auto eo = el.toObject();
                        parts << QString("%1 (%2ch)")
                                 .arg(eo["name"].toString())
                                 .arg(eo["dmxfootprint"].toInt());
                    }
                }
            }
        }
    };

    if (modesVal.isArray())
        for (const auto& m : modesVal.toArray()) tryMode(m);
    else
        tryMode(modesVal);

    return parts.join(", ");
}

// ─── Constructor ─────────────────────────────────────────────────────────────
GdtfShareDialog::GdtfShareDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("GDTF-Share Browser");
    resize(1000, 620);
    setMinimumSize(700, 460);

    nam_ = new QNetworkAccessManager(this);
    nam_->setCookieJar(new QNetworkCookieJar(nam_));

    // ── Login page ────────────────────────────────────────────────────────────
    auto* loginPage  = new QWidget;
    auto* loginOuter = new QVBoxLayout(loginPage);
    loginOuter->addStretch();

    auto* loginBox = new QWidget;
    loginBox->setMaximumWidth(360);
    auto* loginLay = new QFormLayout(loginBox);
    loginLay->setContentsMargins(24, 24, 24, 24);
    loginLay->setSpacing(10);
    loginLay->addRow(new QLabel(
        "<b>GDTF-Share Login</b><br>"
        "<small>Requires a free account at "
        "<a href='https://gdtf-share.com'>gdtf-share.com</a></small>"));

    userEdit_ = new QLineEdit;
    userEdit_->setPlaceholderText("Username");
    QSettings s;
    userEdit_->setText(s.value("gdtfshare/user").toString());

    passEdit_ = new QLineEdit;
    passEdit_->setPlaceholderText("Password");
    passEdit_->setEchoMode(QLineEdit::Password);
    const bool savedPass = !s.value("gdtfshare/pass").toString().isEmpty();
    if (savedPass)
        passEdit_->setText(s.value("gdtfshare/pass").toString());

    savePassCheck_ = new QCheckBox("Remember password");
    savePassCheck_->setChecked(savedPass);

    btnLogin_    = new QPushButton("Log In");
    btnLogin_->setDefault(true);
    loginStatus_ = new QLabel;
    loginStatus_->setWordWrap(true);

    loginLay->addRow("Username:", userEdit_);
    loginLay->addRow("Password:", passEdit_);
    loginLay->addRow("",          savePassCheck_);
    loginLay->addRow("",          btnLogin_);
    loginLay->addRow("",          loginStatus_);

    auto* loginRow = new QHBoxLayout;
    loginRow->addStretch();
    loginRow->addWidget(loginBox);
    loginRow->addStretch();
    loginOuter->addLayout(loginRow);
    loginOuter->addStretch();

    // ── Library page ──────────────────────────────────────────────────────────
    auto* libPage = new QWidget;
    auto* libLay  = new QVBoxLayout(libPage);
    libLay->setContentsMargins(6, 6, 6, 6);
    libLay->setSpacing(4);

    // Toolbar
    auto* toolbar = new QHBoxLayout;
    btnRefresh_ = new QPushButton("Refresh List");
    btnRefresh_->setFixedWidth(100);
    btnDownload_ = new QPushButton("Download && Import");
    btnDownload_->setEnabled(false);
    toolbar->addWidget(btnRefresh_);
    toolbar->addStretch();
    toolbar->addWidget(btnDownload_);
    libLay->addLayout(toolbar);

    // ── Left: manufacturer list ───────────────────────────────────────────────
    auto* leftPanel = new QWidget;
    auto* leftLay   = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(2);

    mfrFilter_ = new QLineEdit;
    mfrFilter_->setPlaceholderText("Filter manufacturers…");
    mfrFilter_->setClearButtonEnabled(true);

    mfrList_ = new QListWidget;
    mfrList_->setMinimumWidth(200);

    leftLay->addWidget(mfrFilter_);
    leftLay->addWidget(mfrList_, 1);

    // ── Right: fixture table ──────────────────────────────────────────────────
    auto* rightPanel = new QWidget;
    auto* rightLay   = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(2);

    fixtureFilter_ = new QLineEdit;
    fixtureFilter_->setPlaceholderText("Filter fixtures…");
    fixtureFilter_->setClearButtonEnabled(true);

    fixtureStatus_ = new QLabel;
    fixtureStatus_->setStyleSheet("color: grey; font-size: 11px;");

    fixtureTable_ = new QTableWidget(0, 4);
    fixtureTable_->setHorizontalHeaderLabels({"Fixture", "Revision", "Modes", "Rating"});
    fixtureTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fixtureTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    fixtureTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    fixtureTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    fixtureTable_->setColumnWidth(1, 130);
    fixtureTable_->setColumnWidth(3, 55);
    fixtureTable_->verticalHeader()->hide();
    fixtureTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fixtureTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fixtureTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    fixtureTable_->setAlternatingRowColors(true);
    fixtureTable_->setSortingEnabled(true);

    rightLay->addWidget(fixtureFilter_);
    rightLay->addWidget(fixtureStatus_);
    rightLay->addWidget(fixtureTable_, 1);

    // ── Splitter ──────────────────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 780});
    libLay->addWidget(splitter, 1);

    // ── Footer: logged-in user + logout ───────────────────────────────────────
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(2, 2, 2, 2);
    loginUserLabel_ = new QLabel;
    loginUserLabel_->setStyleSheet("color: grey; font-size: 11px;");
    btnLogout_ = new QPushButton("Log Out");
    btnLogout_->setFixedHeight(20);
    btnLogout_->setStyleSheet("font-size: 11px;");
    footer->addWidget(loginUserLabel_);
    footer->addStretch();
    footer->addWidget(btnLogout_);
    libLay->addLayout(footer);

    // ── Stack ─────────────────────────────────────────────────────────────────
    stack_ = new QStackedWidget;
    stack_->addWidget(loginPage);  // 0 = login
    stack_->addWidget(libPage);    // 1 = library

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->addWidget(stack_);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(btnLogin_,   &QPushButton::clicked,  this, &GdtfShareDialog::onLogin);
    connect(userEdit_,   &QLineEdit::returnPressed, this, &GdtfShareDialog::onLogin);
    connect(passEdit_,   &QLineEdit::returnPressed, this, &GdtfShareDialog::onLogin);
    connect(btnRefresh_, &QPushButton::clicked,  this, &GdtfShareDialog::onFetchList);
    connect(btnDownload_,&QPushButton::clicked,  this, &GdtfShareDialog::onDownload);
    connect(mfrList_,    &QListWidget::currentItemChanged,
            this, &GdtfShareDialog::onManufacturerSelected);
    connect(mfrFilter_,  &QLineEdit::textChanged, this, [this](const QString& text) {
        populateManufacturers();
        // re-select current manufacturer if still visible
        if (!currentMfr_.isEmpty()) {
            for (int r = 0; r < mfrList_->count(); ++r) {
                if (mfrList_->item(r)->text().startsWith(currentMfr_)) {
                    mfrList_->setCurrentRow(r);
                    break;
                }
            }
        }
        Q_UNUSED(text)
    });
    connect(fixtureFilter_, &QLineEdit::textChanged,
            this, &GdtfShareDialog::onFixtureFilterChanged);
    connect(fixtureTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        btnDownload_->setEnabled(fixtureTable_->currentRow() >= 0);
    });
    connect(btnLogout_, &QPushButton::clicked, this, [this]() {
        loggedIn_ = listLoaded_ = false;
        allEntries_.clear();
        byManufacturer_.clear();
        currentMfr_.clear();
        nam_->setCookieJar(new QNetworkCookieJar(nam_));
        stack_->setCurrentIndex(0);
        loginStatus_->clear();
    });

    // Auto-login if saved credentials are available
    if (!userEdit_->text().isEmpty() && !passEdit_->text().isEmpty())
        QTimer::singleShot(0, this, &GdtfShareDialog::onLogin);
}

// ─── Login ────────────────────────────────────────────────────────────────────
void GdtfShareDialog::onLogin()
{
    const QString user = userEdit_->text().trimmed();
    const QString pass = passEdit_->text();
    if (user.isEmpty() || pass.isEmpty()) {
        loginStatus_->setText("<span style='color:red'>Please enter username and password.</span>");
        return;
    }

    btnLogin_->setEnabled(false);
    loginStatus_->setText("Logging in…");

    QNetworkRequest req{QUrl(kLoginUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QJsonObject body;
    body["user"]     = user;
    body["password"] = pass;

    activeReply_ = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(activeReply_, &QNetworkReply::finished, this, &GdtfShareDialog::onLoginFinished);
}

void GdtfShareDialog::onLoginFinished()
{
    btnLogin_->setEnabled(true);
    if (!activeReply_) return;
    const int      code = activeReply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = activeReply_->readAll();
    activeReply_->deleteLater();
    activeReply_ = nullptr;

    const auto doc = QJsonDocument::fromJson(data);
    const bool ok  = doc.isObject() && doc.object().value("result").toBool() && code == 200;

    if (!ok) {
        const QString msg = doc.isObject()
            ? doc.object().value("error").toString()
            : QString("HTTP %1").arg(code);
        loginStatus_->setText("<span style='color:red'>Login failed: " + msg + "</span>");
        return;
    }

    QSettings s;
    s.setValue("gdtfshare/user", userEdit_->text().trimmed());
    if (savePassCheck_->isChecked())
        s.setValue("gdtfshare/pass", passEdit_->text());
    else
        s.remove("gdtfshare/pass");

    loginUserLabel_->setText("Logged in as: " + userEdit_->text().trimmed());
    loggedIn_ = true;
    stack_->setCurrentIndex(1);
    if (!listLoaded_) onFetchList();
}

// ─── Fixture list ─────────────────────────────────────────────────────────────
void GdtfShareDialog::onFetchList()
{
    fixtureStatus_->setText("Loading fixture list…");
    btnRefresh_->setEnabled(false);
    mfrList_->clear();
    fixtureTable_->setRowCount(0);
    allEntries_.clear();
    byManufacturer_.clear();
    currentMfr_.clear();

    QNetworkRequest req{QUrl(kListUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    activeReply_ = nam_->get(req);
    connect(activeReply_, &QNetworkReply::finished, this, &GdtfShareDialog::onListFinished);
}

void GdtfShareDialog::onListFinished()
{
    btnRefresh_->setEnabled(true);
    if (!activeReply_) return;
    const int      code = activeReply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = activeReply_->readAll();
    activeReply_->deleteLater();
    activeReply_ = nullptr;

    if (code == 401) {
        fixtureStatus_->setText("Session expired — please log in again.");
        loggedIn_ = listLoaded_ = false;
        stack_->setCurrentIndex(0);
        return;
    }

    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject() || !doc.object().value("result").toBool()) {
        const QString msg = doc.isObject()
            ? doc.object().value("error").toString() : "Parse error";
        fixtureStatus_->setText("Error: " + msg);
        return;
    }

    const QJsonArray list = doc.object().value("list").toArray();
    allEntries_.reserve(list.size());

    for (const auto& val : list) {
        if (!val.isObject()) continue;
        const auto obj = val.toObject();

        ShareEntry e;
        e.rid          = obj.value("rid").toInt();
        e.fixture      = obj.value("fixture").toString();
        e.manufacturer = obj.value("manufacturer").toString();
        e.revision     = obj.value("revision").toString();
        e.rating       = float(obj.value("rating").toDouble());
        e.modes        = parseModes(obj.value("modes"));

        if (e.rid <= 0 || e.fixture.isEmpty()) continue;
        if (e.manufacturer.isEmpty()) e.manufacturer = "(Unknown)";

        const int idx = allEntries_.size();
        allEntries_.append(e);
        byManufacturer_[e.manufacturer].append(idx);
    }

    listLoaded_ = true;
    populateManufacturers();
    fixtureStatus_->setText(
        QString("Select a manufacturer — %1 fixtures from %2 manufacturers loaded")
            .arg(allEntries_.size()).arg(byManufacturer_.size()));
}

// ─── Manufacturer list ────────────────────────────────────────────────────────
void GdtfShareDialog::populateManufacturers()
{
    const QString filter = mfrFilter_->text().trimmed().toLower();

    mfrList_->blockSignals(true);
    mfrList_->clear();

    for (auto it = byManufacturer_.constBegin(); it != byManufacturer_.constEnd(); ++it) {
        if (!filter.isEmpty() && !it.key().toLower().contains(filter))
            continue;
        const QString label = QString("%1  (%2)").arg(it.key()).arg(it.value().size());
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, it.key());   // store raw manufacturer name
        mfrList_->addItem(item);
    }

    mfrList_->blockSignals(false);
}

void GdtfShareDialog::onManufacturerSelected()
{
    auto* item = mfrList_->currentItem();
    if (!item) {
        currentMfr_.clear();
        fixtureTable_->setRowCount(0);
        fixtureStatus_->setText({});
        return;
    }
    currentMfr_ = item->data(Qt::UserRole).toString();
    fixtureFilter_->clear();
    populateFixtures(currentMfr_);
}

// ─── Fixture table ────────────────────────────────────────────────────────────
void GdtfShareDialog::populateFixtures(const QString& manufacturer)
{
    const auto& indices = byManufacturer_.value(manufacturer);
    fixtureTable_->setSortingEnabled(false);
    fixtureTable_->setRowCount(0);
    btnDownload_->setEnabled(false);

    const QString termLower = fixtureFilter_->text().trimmed().toLower();

    for (int idx : indices) {
        const auto& e = allEntries_[idx];
        if (!termLower.isEmpty() && !e.fixture.toLower().contains(termLower))
            continue;

        const int row = fixtureTable_->rowCount();
        fixtureTable_->insertRow(row);

        auto makeItem = [](const QString& t, Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft) {
            auto* it = new QTableWidgetItem(t);
            it->setTextAlignment(align);
            return it;
        };
        // store rid in the first column item for retrieval
        auto* nameItem = makeItem(e.fixture);
        nameItem->setData(Qt::UserRole, idx);   // index into allEntries_
        fixtureTable_->setItem(row, 0, nameItem);
        fixtureTable_->setItem(row, 1, makeItem(e.revision));
        fixtureTable_->setItem(row, 2, makeItem(e.modes));
        auto* ratingItem = makeItem(
            e.rating > 0 ? QString::number(double(e.rating), 'f', 1) : "—",
            Qt::AlignCenter);
        fixtureTable_->setItem(row, 3, ratingItem);
        fixtureTable_->setRowHeight(row, 24);
    }

    fixtureTable_->setSortingEnabled(true);
    fixtureTable_->sortByColumn(0, Qt::AscendingOrder);

    const int shown = fixtureTable_->rowCount();
    if (termLower.isEmpty())
        fixtureStatus_->setText(QString("%1 — %2 fixtures").arg(manufacturer).arg(shown));
    else
        fixtureStatus_->setText(QString("%1 — %2 of %3 fixtures match \"%4\"")
            .arg(manufacturer).arg(shown).arg(indices.size()).arg(fixtureFilter_->text().trimmed()));
}

void GdtfShareDialog::onFixtureFilterChanged()
{
    if (!currentMfr_.isEmpty())
        populateFixtures(currentMfr_);
}

// ─── Download ─────────────────────────────────────────────────────────────────
void GdtfShareDialog::onDownload()
{
    const int row = fixtureTable_->currentRow();
    if (row < 0) return;
    auto* nameItem = fixtureTable_->item(row, 0);
    if (!nameItem) return;
    const int idx = nameItem->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= allEntries_.size()) return;
    const auto e = allEntries_[idx];   // copy — table may be rebuilt

    btnDownload_->setEnabled(false);
    btnDownload_->setText("Downloading…");

    const QUrl url(QString("%1?rid=%2").arg(kDownloadUrl).arg(e.rid));
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, e]() {
        reply->deleteLater();
        btnDownload_->setEnabled(true);
        btnDownload_->setText("Download && Import");

        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const QString msg = doc.isObject()
                ? doc.object().value("error").toString() : reply->errorString();
            QMessageBox::warning(this, "Download Failed", msg);
            return;
        }

        const QByteArray gdtfData = reply->readAll();
        if (gdtfData.isEmpty()) {
            QMessageBox::warning(this, "Download Failed", "Downloaded file is empty.");
            return;
        }

        const QString filename = e.manufacturer + "@" + e.fixture + ".gdtf";
        QString err;
        if (!GdtfLibrary::importFromData(gdtfData, filename, &err)) {
            QMessageBox::warning(this, "Import Failed",
                err.isEmpty() ? "Could not import GDTF to library." : err);
            return;
        }

        accept();
    });
}
