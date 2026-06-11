#pragma once
#include <QObject>
#include <QMap>
#include <QMutex>
#include <QByteArray>
#include "Project.h"

class QUdpSocket;

// Receives DMX frames from sACN (E1.31) or ArtNet, one socket per configured universe.
// Emits dmxFrameReceived() on the main thread whenever a valid frame arrives.
class DmxReceiver : public QObject {
    Q_OBJECT
public:
    explicit DmxReceiver(QObject* parent = nullptr);
    ~DmxReceiver() override;

    void configure(const QList<DmxUniverseConfig>& universes);
    void stop();

    // Thread-safe snapshot of the latest DMX frame for the given universe.
    // Returns a 512-byte array, or empty if never received.
    QByteArray latestFrame(quint16 universe) const;

    int totalFramesReceived() const;

signals:
    void dmxFrameReceived(quint16 universe, QByteArray frame);

private:
    struct UniverseState {
        DmxUniverseConfig config;
        QUdpSocket*       socket   = nullptr;
        quint8            lastSeq  = 0;
        QByteArray        frame;   // latest 512-byte DMX data
        QString           mcGroup;
    };

    QMap<quint16, UniverseState> states_;
    mutable QMutex               frameMutex_;
    int                          totalReceived_ = 0;

    void teardown();
    void onReadyRead(quint16 universe);
    bool parseSacn(const QByteArray& data, quint16 universe, UniverseState& st);
    bool parseArtNet(const QByteArray& data, quint16 universe, UniverseState& st);
};
