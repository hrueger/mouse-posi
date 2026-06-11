#pragma once
#include <QObject>
#include <QMap>
#include <QByteArray>
#include "Project.h"

class QUdpSocket;

// Sends DMX frames over sACN (E1.31) or ArtNet, one socket per configured universe.
class DmxSender : public QObject {
    Q_OBJECT
public:
    explicit DmxSender(QObject* parent = nullptr);
    ~DmxSender() override;

    void configure(const QList<DmxUniverseConfig>& universes);
    void stop();

    // Send a 512-byte DMX frame on the given universe (must be in the configured list).
    // dmx512 must be exactly 512 bytes; shorter arrays are zero-padded.
    void sendFrame(quint16 universe, const QByteArray& dmx512);

    int totalFramesSent() const;

private:
    struct UniverseState {
        DmxUniverseConfig  config;
        QUdpSocket*        socket   = nullptr;
        quint8             seqSacn  = 0;
        quint8             seqArtNet = 0;
    };

    QMap<quint16, UniverseState> states_;
    int totalSent_ = 0;

    void sendSacn(UniverseState& st, const QByteArray& dmx512);
    void sendArtNet(UniverseState& st, const QByteArray& dmx512);
    void teardown();
};
