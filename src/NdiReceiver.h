#pragma once
#include <QThread>
#include <QImage>
#include <QStringList>
#include <QMutex>
#include <QFuture>
#include <atomic>

class NdiReceiver : public QThread {
    Q_OBJECT
public:
    explicit NdiReceiver(QObject* parent = nullptr);
    ~NdiReceiver() override;

    void discoverSources();
    void connectToSource(const QString& name);
    void disconnectFromSource();
    void stop();

signals:
    void frameReady(QImage frame);
    void sourcesChanged(QStringList sources);
    void sourceEndpointChanged(const QString& sourceName, const QString& urlAddress);

protected:
    void run() override;

private:
    QMutex           sourceMutex_;
    QString          targetSource_;
    std::atomic_bool running_{false};
    std::atomic_bool reconnect_{false};
    QFuture<void>    discoveryFuture_;
};
