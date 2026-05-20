#pragma once
#include <QThread>
#include <QMutex>
#include <QMap>
#include <QVector3D>
#include <QHostAddress>
#include <QNetworkInterface>
#include <atomic>

class PsnReceiver : public QThread {
    Q_OBJECT
public:
    explicit PsnReceiver(QObject* parent = nullptr);
    ~PsnReceiver() override;

    void startListening(const QString& multicastIp, quint16 port,
                        bool multicast = true,
                        const QString& interfaceName = {});
    void stop();

    // Thread-safe snapshot of all received tracker positions
    QMap<int, QVector3D> remotePositions();

    quint64 totalBinaryPacketsReceived() const {
        return totalBinaryPacketsReceived_.load(std::memory_order_relaxed);
    }

protected:
    void run() override;

private:
    QMutex               mutex_;
    QMap<int, QVector3D> positions_;
    QString              multicastIp_;
    QString              interfaceName_;
    quint16              port_      = 56565;
    bool                 multicast_ = true;
    std::atomic_bool     running_{false};

    std::atomic<quint64> totalBinaryPacketsReceived_{0};
};
