#pragma once
#include <QThread>
#include <QImage>
#include <QStringList>
#include <QMutex>
#include <atomic>

class NdiReceiver : public QThread {
    Q_OBJECT
public:
    explicit NdiReceiver(QObject* parent = nullptr);
    ~NdiReceiver() override;

    void discoverSources();
    void connectToSource(const QString& name);
    void stop();

signals:
    void frameReady(QImage frame);
    void sourcesChanged(QStringList sources);

protected:
    void run() override;

private:
    QMutex           sourceMutex_;
    QString          targetSource_;
    std::atomic_bool running_{false};
    std::atomic_bool reconnect_{false};
};
