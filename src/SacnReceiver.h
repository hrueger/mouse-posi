#pragma once
#include <QObject>
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <atomic>
#include "Project.h"

class QUdpSocket;

class SacnReceiver : public QObject {
    Q_OBJECT
public:
    explicit SacnReceiver(QObject* parent = nullptr);
    ~SacnReceiver() override;

    void startListening(const SacnInputConfig& cfg);
    void stop();

    quint64 totalUdpReceived()  const { return totalUdp_;  }
    quint64 totalSacnReceived() const { return totalSacn_; }

signals:
    void heightReceived(float height, const QString& sourceName);

private slots:
    void onReadyRead();

private:
    void closeSocket();

    QUdpSocket*          socket_   = nullptr;
    QHostAddress         mcGroup_;
    QStringList          mcIfaces_;
    SacnInputConfig      config_;
    quint8               lastSeq_  = 0;
    std::atomic<quint64> totalUdp_ {0};
    std::atomic<quint64> totalSacn_{0};
};
