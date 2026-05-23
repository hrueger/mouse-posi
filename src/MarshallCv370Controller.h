#pragma once

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class MarshallCv370Controller : public QObject {
    Q_OBJECT
public:
    explicit MarshallCv370Controller(QObject* parent = nullptr);

    static QUrl buildSetIrCutUrl(const QString& host, bool nightMode);

    void setNightMode(const QString& host, bool nightMode);

signals:
    void requestFinished(bool ok, bool nightMode, const QString& message);

private:
    QNetworkAccessManager* network_ = nullptr;
};
