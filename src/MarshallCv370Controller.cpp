#include "MarshallCv370Controller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

MarshallCv370Controller::MarshallCv370Controller(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this))
{
}

QUrl MarshallCv370Controller::buildSetIrCutUrl(const QString& host, bool nightMode) {
    QString normalized = host.trimmed();
    if (normalized.isEmpty())
        return {};

    if (!normalized.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !normalized.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        normalized.prepend(QStringLiteral("http://"));
    }

    QUrl base(normalized);
    if (!base.isValid() || base.host().isEmpty())
        return {};

    base.setPath(QStringLiteral("/cgi-bin/web.fcgi"));

    QJsonObject image;
    image[QStringLiteral("ircut")] = nightMode ? 0 : 1;

    QJsonObject payload;
    payload[QStringLiteral("image")] = image;

    const QString compactJson = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    base.setQuery(QStringLiteral("func=set%1").arg(compactJson));
    return base;
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
