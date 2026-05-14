#pragma once
#include <QObject>
#include <QString>

// macOS NSNetService/NSNetServiceBrowser wrapper.
// Implementation in DnsSdBridge.mm (Objective-C++).
class DnsSdBridge : public QObject {
    Q_OBJECT
public:
    explicit DnsSdBridge(QObject* parent = nullptr);
    ~DnsSdBridge() override;

    void advertise(const QString& sessionName, quint16 port);
    void stopAdvertising();

    void browse();
    void stopBrowsing();

signals:
    void serviceFound(QString name, QString host, quint16 port);
    void serviceLost(QString name);
    void advertiseError(QString message);

private:
    void* impl_ = nullptr; // opaque Obj-C object
};
