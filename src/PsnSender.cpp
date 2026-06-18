#include "PsnSender.h"
#include <QByteArray>
#include <QVariant>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {

constexpr int kMaxUdpPacketSize = 1500;

void appendU8(QByteArray& out, quint8 v) {
    out.append(char(v));
}

void appendU16LE(QByteArray& out, quint16 v) {
    const quint16 le = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&le), sizeof(le));
}

void appendU32LE(QByteArray& out, quint32 v) {
    const quint32 le = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&le), sizeof(le));
}

void appendU64LE(QByteArray& out, quint64 v) {
    const quint64 le = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&le), sizeof(le));
}

void appendF32LE(QByteArray& out, float v) {
    quint32 u = 0;
    static_assert(sizeof(u) == sizeof(v), "float must be 32-bit");
    std::memcpy(&u, &v, sizeof(u));
    appendU32LE(out, u);
}

QByteArray makeChunk(quint16 id, const QByteArray& payload, bool hasSubchunks) {
    // PSN v2: chunk header is 4 bytes: u16 id, u16 length with MSB = has_subchunks
    // Length is payload size (not including the 4-byte header) and limited to 0x7fff.
    const int len = payload.size();
    if (len < 0 || len > 0x7fff)
        return {};

    QByteArray out;
    out.reserve(4 + len);
    appendU16LE(out, id);
    quint16 lenField = static_cast<quint16>(len);
    if (hasSubchunks)
        lenField |= 0x8000;
    appendU16LE(out, lenField);
    out.append(payload);
    return out;
}

QByteArray makePacketHeaderChunk(quint64 timestampUs, quint8 verHigh, quint8 verLow,
                                quint8 frameId, quint8 framePacketCount) {
    QByteArray payload;
    payload.reserve(12);
    appendU64LE(payload, timestampUs);
    appendU8(payload, verHigh);
    appendU8(payload, verLow);
    appendU8(payload, frameId);
    appendU8(payload, framePacketCount);
    return makeChunk(0x0000, payload, false);
}

QByteArray makeDataTrackerPosChunk(float x, float y, float z) {
    QByteArray payload;
    payload.reserve(12);
    appendF32LE(payload, x);
    appendF32LE(payload, y);
    appendF32LE(payload, z);
    return makeChunk(0x0000, payload, false);
}

QByteArray makeDataTrackerSpeedChunk(float x, float y, float z) {
    QByteArray payload;
    payload.reserve(12);
    appendF32LE(payload, x);
    appendF32LE(payload, y);
    appendF32LE(payload, z);
    return makeChunk(0x0001, payload, false);
}

QByteArray makeDataTrackerStatusChunk(float validity) {
    QByteArray payload;
    payload.reserve(4);
    appendF32LE(payload, validity);
    return makeChunk(0x0003, payload, false);
}

QByteArray concatChunks(const QList<QByteArray>& chunks) {
    int total = 0;
    for (const auto& c : chunks)
        total += c.size();
    QByteArray out;
    out.reserve(total);
    for (const auto& c : chunks)
        out.append(c);
    return out;
}

QByteArray makeInfoSystemNameChunk(const QString& name) {
    const QByteArray bytes = name.toUtf8();
    return makeChunk(0x0001, bytes, false);
}

QByteArray makeInfoTrackerNameChunk(const QString& name) {
    const QByteArray bytes = name.toUtf8();
    return makeChunk(0x0000, bytes, false);
}

} // namespace

PsnSender::PsnSender(QObject* parent) : QObject(parent) {
    timer_.start();
    // System name is shown by receivers (e.g. consoles) as the sender identity.
    const QString host = QHostInfo::localHostName();
    systemName_ = host.isEmpty() ? QStringLiteral("onpoint")
                                 : QStringLiteral("onpoint-%1").arg(host);
}

void PsnSender::configure(const NetworkConfig& cfg) {
    psnMode_    = cfg.psnMode;
    port_       = cfg.port;
    psnOffsetX_ = cfg.psnOffsetX;
    psnOffsetY_ = cfg.psnOffsetY;
    psnOffsetZ_ = cfg.psnOffsetZ;
    psnRotDeg_  = cfg.psnRotDeg;

    socket_.close();

    // Determine bind address: use the interface's IPv4 address so outgoing packets
    // leave on the correct NIC regardless of the OS routing table.
    QHostAddress bindAddr = QHostAddress::AnyIPv4;
    QNetworkInterface iface;
    if (!cfg.psnInterface.isEmpty()) {
        iface = QNetworkInterface::interfaceFromName(cfg.psnInterface);
        if (iface.isValid()) {
            for (const auto& entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    bindAddr = entry.ip();
                    break;
                }
            }
        }
    }

    socket_.bind(bindAddr, 0);

    // setMulticastInterface must be called after bind so the underlying socket
    // option takes effect.
    if (iface.isValid() && psnMode_ == PsnMode::Multicast)
        socket_.setMulticastInterface(iface);

    switch (psnMode_) {
        case PsnMode::Multicast:
            targetAddr_ = QHostAddress(cfg.multicastIp);
            socket_.setSocketOption(QAbstractSocket::MulticastTtlOption, QVariant(5));
            break;
        case PsnMode::Unicast:
            targetAddr_ = QHostAddress(cfg.unicastIp);
            break;
        case PsnMode::Broadcast:
            targetAddr_ = QHostAddress(cfg.broadcastIp.isEmpty()
                                       ? QStringLiteral("255.255.255.255") : cfg.broadcastIp);
            socket_.setSocketOption(QAbstractSocket::MulticastTtlOption, QVariant(1));
            break;
    }
}

void PsnSender::sendPositions(const QMap<int, QPair<float,float>>& positions,
                               const QList<TrackerConfig>& trackers,
                               const QMap<int, float>& heights) {
    sendDataPacketsV2(positions, heights);

    ++ticksSinceInfo_;
    if (ticksSinceInfo_ >= 60) {
        ticksSinceInfo_ = 0;
        sendInfoPacketsV2(trackers);
    }
}

void PsnSender::sendDataPacketsV2(const QMap<int, QPair<float,float>>& positions,
                                   const QMap<int, float>& heights) {
    const quint64 nowUs = static_cast<quint64>(timer_.nsecsElapsed() / 1000);
    const qint64  nowMs = timer_.elapsed();

    // Build per-tracker chunks.
    QList<QByteArray> trackerChunks;
    trackerChunks.reserve(positions.size());
    const float cosA = std::cos(-psnRotDeg_ * static_cast<float>(M_PI) / 180.f);
    const float sinA = std::sin(-psnRotDeg_ * static_cast<float>(M_PI) / 180.f);

    for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
        const int   id = it.key();
        const float dx = it.value().first  - psnOffsetX_;
        const float dz = it.value().second - psnOffsetZ_;
        const float x  = dx * cosA - dz * sinA;
        const float z  = dx * sinA + dz * cosA;

        float vx = 0.0f, vz = 0.0f;
        if (lastPos_.contains(id) && lastTimeMs_.contains(id)) {
            const double dt = (nowMs - lastTimeMs_[id]) / 1000.0;
            if (dt > 0.001) {
                vx = (x - lastPos_[id].first)  / static_cast<float>(dt);
                vz = (z - lastPos_[id].second) / static_cast<float>(dt);
            }
        }
        lastPos_[id]    = {x, z};
        lastTimeMs_[id] = nowMs;

        const float      yOut        = heights.value(id, 0.0f) - psnOffsetY_;
        const QByteArray posChunk    = makeDataTrackerPosChunk(x, -z, yOut);
        const QByteArray speedChunk  = makeDataTrackerSpeedChunk(vx, -vz, 0.0f);
        const QByteArray statusChunk = makeDataTrackerStatusChunk(1.0f);

        const QByteArray fieldPayload = concatChunks({posChunk, speedChunk, statusChunk});
        const QByteArray trackerChunk = makeChunk(static_cast<quint16>(id), fieldPayload, true);
        if (!trackerChunk.isEmpty())
            trackerChunks.append(trackerChunk);
    }

    // Split into multiple UDP packets if necessary.
    QList<QList<QByteArray>> trackerLists;
    QList<QByteArray> current;
    int currentSum = 0;
    auto flush = [&]() {
        trackerLists.append(current);
        current.clear();
        currentSum = 0;
    };

    for (const auto& trk : trackerChunks) {
        const int nextSum = currentSum + trk.size();
        const int packetSize = 24 + nextSum; // root hdr(4) + packetHeaderChunk(16) + trackerList hdr(4) + tracker chunks
        if (!current.isEmpty() && packetSize > kMaxUdpPacketSize)
            flush();
        current.append(trk);
        currentSum += trk.size();
    }
    flush();

    const quint8 framePacketCount = static_cast<quint8>(std::min<int>(255, trackerLists.size()));
    const QByteArray headerChunk = makePacketHeaderChunk(nowUs, 2, 3, dataFrameId_, framePacketCount);

    for (const auto& list : trackerLists) {
        const QByteArray trackerListPayload = concatChunks(list);
        const QByteArray trackerListChunk = makeChunk(0x0001, trackerListPayload, true);
        const QByteArray rootPayload = headerChunk + trackerListChunk;
        const QByteArray dataPacket = makeChunk(0x6755, rootPayload, true);

        if (!dataPacket.isEmpty() && socket_.writeDatagram(dataPacket, targetAddr_, port_) >= 0)
            totalPacketsSent_.fetch_add(1, std::memory_order_relaxed);
    }

    dataFrameId_ = static_cast<quint8>(dataFrameId_ + 1);
}

void PsnSender::sendInfoPacketsV2(const QList<TrackerConfig>& trackers) {
    auto sanitizeName = [](QString name) {
        // Spec discourages control chars; keep names simple.
        name.replace('\n', ' ');
        name.replace('\r', ' ');
        return name;
    };

    const quint64 nowUs = static_cast<quint64>(timer_.nsecsElapsed() / 1000);

    const QByteArray systemNameChunk = makeInfoSystemNameChunk(systemName_);

    QList<QByteArray> trackerChunks;
    trackerChunks.reserve(trackers.size());
    for (const auto& t : trackers) {
        const QByteArray trackerNameChunk = makeInfoTrackerNameChunk(sanitizeName(t.name));
        const QByteArray trackerChunk = makeChunk(static_cast<quint16>(t.id), trackerNameChunk, true);
        if (!trackerChunk.isEmpty())
            trackerChunks.append(trackerChunk);
    }

    // Split if necessary (system name chunk is included in every info packet).
    QList<QList<QByteArray>> trackerLists;
    QList<QByteArray> current;
    int currentSum = 0;
    auto flush = [&]() {
        trackerLists.append(current);
        current.clear();
        currentSum = 0;
    };

    const int sysLen = systemNameChunk.size();
    for (const auto& trk : trackerChunks) {
        const int nextSum = currentSum + trk.size();
        const int packetSize = 28 + sysLen + nextSum; // root(4) + header(16) + systemNameChunk + trackerList hdr(4) + trackers
        if (!current.isEmpty() && packetSize > kMaxUdpPacketSize)
            flush();
        current.append(trk);
        currentSum += trk.size();
    }
    flush();

    const quint8 framePacketCount = static_cast<quint8>(std::min<int>(255, trackerLists.size()));
    const QByteArray headerChunk = makePacketHeaderChunk(nowUs, 2, 3, infoFrameId_, framePacketCount);

    for (const auto& list : trackerLists) {
        const QByteArray trackerListPayload = concatChunks(list);
        const QByteArray trackerListChunk = makeChunk(0x0002, trackerListPayload, true);
        const QByteArray rootPayload = headerChunk + systemNameChunk + trackerListChunk;
        const QByteArray infoPacket = makeChunk(0x6756, rootPayload, true);

        if (!infoPacket.isEmpty() && socket_.writeDatagram(infoPacket, targetAddr_, port_) >= 0)
            totalPacketsSent_.fetch_add(1, std::memory_order_relaxed);
    }

    infoFrameId_ = static_cast<quint8>(infoFrameId_ + 1);
}
