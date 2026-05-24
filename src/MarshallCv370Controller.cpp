#include "MarshallCv370Controller.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

MarshallCv370Controller::MarshallCv370Controller(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this))
{
}

QString MarshallCv370Controller::hostFromNdiUrlAddress(const QString& ndiUrlAddress) {
    QString normalized = ndiUrlAddress.trimmed();
    if (normalized.isEmpty())
        return {};

    if (!normalized.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !normalized.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        normalized.prepend(QStringLiteral("http://"));
    }

    const QUrl url(normalized);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    return url.host();
}

static QUrl buildMarshallBaseUrl(const QString& hostOrUrl) {
    QString host = MarshallCv370Controller::hostFromNdiUrlAddress(hostOrUrl);
    if (host.isEmpty())
        return {};

    QUrl base;
    base.setScheme(QStringLiteral("http"));
    base.setHost(host);
    base.setPath(QStringLiteral("/cgi-bin/web.fcgi"));
    return base;
}

QUrl MarshallCv370Controller::buildDetectUrl(const QString& hostOrNdiUrlAddress) {
    QUrl url = buildMarshallBaseUrl(hostOrNdiUrlAddress);
    if (!url.isValid() || url.host().isEmpty())
        return {};

    QJsonObject payload;
    payload[QStringLiteral("image")] = QJsonArray{QStringLiteral("ircut")};
    const QString compactJson = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    url.setQuery(QStringLiteral("func=get%1").arg(compactJson));
    return url;
}

QUrl MarshallCv370Controller::buildSetIrCutUrl(const QString& host, bool nightMode) {
    QUrl base = buildMarshallBaseUrl(host);
    if (!base.isValid() || base.host().isEmpty())
        return {};

    QJsonObject image;
    image[QStringLiteral("ircut")] = nightMode ? 0 : 1;

    QJsonObject payload;
    payload[QStringLiteral("image")] = image;

    const QString compactJson = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    base.setQuery(QStringLiteral("func=set%1").arg(compactJson));
    return base;
}

bool MarshallCv370Controller::responseLooksLikeCv370(const QByteArray& body) {
    const QString text = QString::fromUtf8(body).toLower();
    return text.contains(QStringLiteral("ircut")) ||
           (text.contains(QStringLiteral("image")) && text.contains(QStringLiteral("result")));
}

void MarshallCv370Controller::detectHost(const QString& hostOrNdiUrlAddress) {
    const QUrl url = buildDetectUrl(hostOrNdiUrlAddress);
    if (!url.isValid() || url.host().isEmpty()) {
        emit detectionFinished(false, {}, QStringLiteral("No camera IP found for the selected NDI source."));
        return;
    }

    QNetworkRequest request(url);
    auto* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, host = url.host()]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool transportOk = reply->error() == QNetworkReply::NoError;
        const bool httpOk = status == 0 || (status >= 200 && status < 300);
        const QByteArray body = reply->readAll();
        const bool detected = transportOk && httpOk && responseLooksLikeCv370(body);

        QString message;
        if (detected) {
            message = QStringLiteral("Detected Marshall CV-370 at %1 from the NDI stream IP.").arg(host);
        } else if (!transportOk) {
            message = QStringLiteral("Selected NDI stream IP is %1; CV-370 probe failed: %2")
                .arg(host, reply->errorString());
        } else if (!httpOk) {
            message = QStringLiteral("Selected NDI stream IP is %1; CV-370 probe returned HTTP %2.")
                .arg(host).arg(status);
        } else {
            message = QStringLiteral("Selected NDI stream IP is %1, but it did not answer like a CV-370.").arg(host);
        }

        reply->deleteLater();
        emit detectionFinished(detected, host, message);
    });
}

void MarshallCv370Controller::setNightMode(const QString& host, bool nightMode) {
    const QUrl url = buildSetIrCutUrl(host, nightMode);
    if (!url.isValid() || url.host().isEmpty()) {
        emit requestFinished(false, nightMode, QStringLiteral("Enter a valid CV-370 host or IP address."));
        return;
    }

    QNetworkRequest request(url);
    auto* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nightMode]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool transportOk = reply->error() == QNetworkReply::NoError;
        const bool httpOk = status == 0 || (status >= 200 && status < 300);
        const bool ok = transportOk && httpOk;
        QString message;
        if (ok) {
            message = nightMode ? QStringLiteral("CV-370 switched to night mode.")
                                : QStringLiteral("CV-370 switched to daylight mode.");
        } else if (!transportOk) {
            message = reply->errorString();
        } else {
            message = QStringLiteral("CV-370 returned HTTP %1.").arg(status);
        }
        reply->deleteLater();
        emit requestFinished(ok, nightMode, message);
    });
}

MarshallCv370Panel::MarshallCv370Panel(QWidget* parent) : CameraSettingsPanel(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);

    cameraCheck_ = new QCheckBox(QStringLiteral("Marshall CV-370 camera"));
    cameraCheck_->setChecked(true);
    cameraCheck_->setVisible(false);
    cameraCheck_->setToolTip(QStringLiteral(
        "Automatically probes the selected NDI stream IP for Marshall CV-370 controls.\n"
        "Use the generic Camera control checkbox above to enable or disable camera control."));
    layout->addWidget(cameraCheck_);

    auto* hostRow = new QHBoxLayout;
    hostLabel_ = new QLabel(QStringLiteral("CV-370 host:"));
    hostEdit_ = new QLineEdit;
    hostEdit_->setPlaceholderText(QStringLiteral("IP or hostname"));
    hostEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    hostRow->addWidget(hostLabel_);
    hostRow->addWidget(hostEdit_, 1);
    layout->addLayout(hostRow);

    toggleBtn_ = new QPushButton(QStringLiteral("Switch to Night Mode"));
    toggleBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(toggleBtn_);

    statusLabel_ = new QLabel;
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("color: palette(placeholderText); font-size: 11px;"));
    layout->addWidget(statusLabel_);

    controller_ = new MarshallCv370Controller(this);
    updateControls();

    connect(cameraCheck_, &QCheckBox::toggled, this, [this](bool) {
        if (!setting_) emitConfigChanged();
        updateControls();
    });
    connect(hostEdit_, &QLineEdit::textEdited, this, [this](const QString&) {
        userEditedHost_ = true;
    });
    connect(hostEdit_, &QLineEdit::textChanged, this, [this](const QString&) {
        if (!setting_) emitConfigChanged();
        updateControls();
    });
    connect(toggleBtn_, &QPushButton::clicked, this, [this]() {
        const bool nextNightMode = !nightMode_;
        toggleBtn_->setEnabled(false);
        statusLabel_->setText(nextNightMode ? QStringLiteral("Switching CV-370 to night mode…")
                                            : QStringLiteral("Switching CV-370 to daylight mode…"));
        controller_->setNightMode(hostEdit_->text(), nextNightMode);
    });
    connect(controller_, &MarshallCv370Controller::detectionFinished,
            this, [this](bool detected, const QString& host, const QString& message) {
        statusLabel_->setText(message);
        if (detected) {
            setting_ = true;
            cameraCheck_->setChecked(true);
            if (!userEditedHost_ || hostEdit_->text().trimmed().isEmpty() || hostEdit_->text() == lastDetectedHost_)
                hostEdit_->setText(host);
            lastDetectedHost_ = host;
            setting_ = false;
            emitConfigChanged();
        }
        updateControls();
    });
    connect(controller_, &MarshallCv370Controller::requestFinished,
            this, [this](bool ok, bool nightMode, const QString& message) {
        if (ok) {
            nightMode_ = nightMode;
            emitConfigChanged();
        }
        statusLabel_->setText(message);
        updateControls();
    });
}

QJsonObject MarshallCv370Panel::configJson() const {
    QJsonObject config;
    config[QStringLiteral("host")] = hostEdit_->text();
    config[QStringLiteral("nightMode")] = nightMode_;
    return config;
}

void MarshallCv370Panel::setConfigJson(const QJsonObject& config) {
    setting_ = true;
    hostEdit_->setText(config[QStringLiteral("host")].toString());
    nightMode_ = config[QStringLiteral("nightMode")].toBool(false);
    userEditedHost_ = !hostEdit_->text().trimmed().isEmpty();
    setting_ = false;
    statusLabel_->setText(nightMode_ ? QStringLiteral("Current mode: night")
                                     : QStringLiteral("Current mode: daylight"));
    updateControls();
}

void MarshallCv370Panel::setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress) {
    if (sourceName == currentNdiSource_ && ndiUrlAddress.isEmpty())
        return;
    currentNdiSource_ = sourceName;

    const QString host = MarshallCv370Controller::hostFromNdiUrlAddress(ndiUrlAddress);
    if (host.isEmpty()) {
        if (!sourceName.isEmpty())
            statusLabel_->setText(QStringLiteral("Selected NDI stream does not expose an IP address."));
        return;
    }

    if (!userEditedHost_ || hostEdit_->text().trimmed().isEmpty()) {
        setting_ = true;
        hostEdit_->setText(host);
        setting_ = false;
    }

    statusLabel_->setText(QStringLiteral("Checking selected NDI stream IP %1 for CV-370 controls…").arg(host));
    controller_->detectHost(host);
    updateControls();
}

void MarshallCv370Panel::emitConfigChanged() {
    emit configChanged();
}

void MarshallCv370Panel::updateControls() {
    const bool hasHost = !hostEdit_->text().trimmed().isEmpty();
    hostLabel_->setVisible(true);
    hostEdit_->setVisible(true);
    toggleBtn_->setVisible(true);
    statusLabel_->setVisible(!statusLabel_->text().isEmpty());
    toggleBtn_->setEnabled(hasHost);
    toggleBtn_->setText(nightMode_ ? QStringLiteral("Switch to Daylight Mode")
                                   : QStringLiteral("Switch to Night Mode"));
}

void registerMarshallCv370Camera(CameraControlPanel* cameraControl) {
    cameraControl->registerCamera(
        QStringLiteral("cv370"),
        QStringLiteral("Marshall CV-370"),
        [](QWidget* parent) { return new MarshallCv370Panel(parent); });
}
