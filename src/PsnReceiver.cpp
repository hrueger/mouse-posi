#include "PsnReceiver.h"
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QtEndian>
#include <cstring>

namespace {

struct ChunkHeader {
    quint16 id = 0;
    quint16 len = 0;
    bool hasSubchunks = false;
};

bool readChunkHeader(const QByteArray& data, int offset, ChunkHeader& out) {
    if (offset < 0 || offset + 4 > data.size())
        return false;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    const quint16 id = qFromLittleEndian<quint16>(p);
    const quint16 lenField = qFromLittleEndian<quint16>(p + 2);
    out.id = id;
    out.hasSubchunks = (lenField & 0x8000) != 0;
    out.len = (lenField & 0x7fff);
    if (offset + 4 + out.len > data.size())
        return false;
    return true;
}

float readF32LE(const char* p) {
    const quint32 u = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
    float f = 0.0f;
    static_assert(sizeof(float) == sizeof(quint32), "float must be 32-bit");
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

} // namespace

// PSN v2 decoding: chunk-based packets.
// We only decode PSN_DATA_PACKET (0x6755) and extract per-tracker position chunks (0x0000).

PsnReceiver::PsnReceiver(QObject* parent) : QThread(parent) {}

PsnReceiver::~PsnReceiver() {
    stop();
    wait();
}

void PsnReceiver::startListening(const QString& multicastIp, quint16 port, bool multicast) {
    multicastIp_ = multicastIp;
    port_        = port;
    multicast_   = multicast;
    running_     = true;
    if (!isRunning()) start();
}

void PsnReceiver::stop() {
    running_ = false;
}

QMap<int, QVector3D> PsnReceiver::remotePositions() {
    QMutexLocker lock(&mutex_);
    return positions_;
}

void PsnReceiver::run() {
    QUdpSocket socket;
    socket.bind(QHostAddress::AnyIPv4, port_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    if (multicast_) {
        socket.joinMulticastGroup(QHostAddress(multicastIp_));
    }

    while (running_) {
        if (!socket.waitForReadyRead(50)) continue;

        while (socket.hasPendingDatagrams()) {
            QNetworkDatagram dg = socket.receiveDatagram();
            QByteArray data = dg.data();

            ChunkHeader root;
            if (!readChunkHeader(data, 0, root))
                continue;
            if (root.id != 0x6755 || !root.hasSubchunks)
                continue;

            totalBinaryPacketsReceived_.fetch_add(1, std::memory_order_relaxed);

            const int rootStart = 4;
            const int rootEnd = rootStart + root.len;

            // Find tracker list chunk (0x0001).
            int off = rootStart;
            while (off < rootEnd) {
                ChunkHeader ch;
                if (!readChunkHeader(data, off, ch))
                    break;

                if (ch.id == 0x0001 && ch.hasSubchunks) {
                    // Tracker list contains tracker chunks where chunk id == tracker id.
                    const int listStart = off + 4;
                    const int listEnd = listStart + ch.len;

                    QMutexLocker lock(&mutex_);
                    int toff = listStart;
                    while (toff < listEnd) {
                        ChunkHeader trk;
                        if (!readChunkHeader(data, toff, trk))
                            break;

                        const int trackerId = trk.id;
                        if (trk.hasSubchunks) {
                            const int fieldStart = toff + 4;
                            const int fieldEnd = fieldStart + trk.len;
                            int foff = fieldStart;
                            while (foff < fieldEnd) {
                                ChunkHeader field;
                                if (!readChunkHeader(data, foff, field))
                                    break;
                                if (field.id == 0x0000 && !field.hasSubchunks && field.len >= 12) {
                                    const char* fp = data.constData() + foff + 4;
                                    const float x = readF32LE(fp + 0);
                                    const float y = readF32LE(fp + 4);
                                    const float z = readF32LE(fp + 8);
                                    positions_[trackerId] = QVector3D(x, y, z);
                                }
                                foff += 4 + field.len;
                            }
                        }

                        toff += 4 + trk.len;
                    }
                }

                off += 4 + ch.len;
            }
        }
    }

    if (multicast_)
        socket.leaveMulticastGroup(QHostAddress(multicastIp_));
}
