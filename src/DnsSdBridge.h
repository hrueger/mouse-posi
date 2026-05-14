#pragma once
#include <QObject>
#include <QString>

// DNS-SD / mDNS service advertisement and discovery.
// macOS: implemented in DnsSdBridge.mm (NSNetService / Objective-C++).
// Windows: implemented in DnsSdBridge_win.cpp (dns_sd C API via Bonjour SDK).
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
    void* impl_ = nullptr; // opaque platform handle
};
