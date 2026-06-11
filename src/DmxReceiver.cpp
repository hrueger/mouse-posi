#include "DmxReceiver.h"
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QMutexLocker>
#include <QDebug>
#include <QtEndian>
#include <cstring>
#include <e131.h>

static constexpr quint16 kSacnPort   = 5568;
static constexpr quint16 kArtNetPort = 6454;

// ArtNet OpDmx opcode (little-endian in packet)
static constexpr quint16 kArtOpDmx = 0x0050;

DmxReceiver::DmxReceiver(QObject* parent) : QObject(parent) {}

DmxReceiver::~DmxReceiver() { teardown(); }

void DmxReceiver::teardown() {
    for (auto& st : states_) {
        if (!st.socket) continue;
        if (!st.mcGroup.isEmpty()) {
            const QHostAddress grp(st.mcGroup);
            if (!grp.isNull()) {
                if (!st.config.iface.isEmpty()) {
                    const QNetworkInterface ni = QNetworkInterface::interfaceFromName(st.config.iface);
                    if (ni.isValid()) st.socket->leaveMulticastGroup(grp, ni);
                }
            }
        }
        st.socket->close();
        st.socket->deleteLater();
        st.socket = nullptr;
    }
    states_.clear();
}

void DmxReceiver::stop() { teardown(); }

void DmxReceiver::configure(const QList<DmxUniverseConfig>& universes) {
    teardown();

    for (const auto& cfg : universes) {
        UniverseState st;
        st.config = cfg;
        st.frame  = QByteArray(512, '\0');

        const quint16 port = (cfg.protocol == DmxProtocol::ArtNet) ? kArtNetPort : kSacnPort;
        st.socket = new QUdpSocket(this);

        const bool bound = st.socket->bind(QHostAddress::AnyIPv4, port,
                                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        if (!bound) {
            qWarning() << "DmxReceiver: bind failed universe" << cfg.universe
                       << ":" << st.socket->errorString();
            st.socket->deleteLater();
            st.socket = nullptr;
            continue;
        }

        if (cfg.protocol == DmxProtocol::SACN && cfg.netMode == DmxNetworkMode::Multicast) {
            st.mcGroup = QString("239.255.%1.%2")
                         .arg(cfg.universe >> 8).arg(cfg.universe & 0xFF);

            auto joinOn = [&](const QNetworkInterface& ni) {
                if (st.socket->joinMulticastGroup(QHostAddress(st.mcGroup), ni))
                    qDebug() << "DmxReceiver sACN: joined" << st.mcGroup << "on" << ni.name();
            };

            if (!cfg.iface.isEmpty()) {
                joinOn(QNetworkInterface::interfaceFromName(cfg.iface));
            } else {
                for (const auto& ni : QNetworkInterface::allInterfaces()) {
                    if (!(ni.flags() & QNetworkInterface::IsUp))        continue;
                    if (!(ni.flags() & QNetworkInterface::CanMulticast)) continue;
                    if (ni.flags() & QNetworkInterface::IsLoopBack)      continue;
                    bool hasV4 = false;
                    for (const auto& e : ni.addressEntries())
                        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) { hasV4 = true; break; }
                    if (hasV4) joinOn(ni);
                }
            }
        }

        const quint16 uni = cfg.universe;
        connect(st.socket, &QUdpSocket::readyRead, this, [this, uni]() { onReadyRead(uni); });

        states_[cfg.universe] = std::move(st);
    }
}

QByteArray DmxReceiver::latestFrame(quint16 universe) const {
    QMutexLocker lk(&frameMutex_);
    const auto it = states_.constFind(universe);
    return (it != states_.constEnd()) ? it->frame : QByteArray();
}

int DmxReceiver::totalFramesReceived() const { return totalReceived_; }

void DmxReceiver::onReadyRead(quint16 universe) {
    auto it = states_.find(universe);
    if (it == states_.end() || !it->socket) return;

    while (it->socket->hasPendingDatagrams()) {
        const QByteArray data = it->socket->receiveDatagram().data();

        bool ok = false;
        if (it->config.protocol == DmxProtocol::SACN)
            ok = parseSacn(data, universe, *it);
        else
            ok = parseArtNet(data, universe, *it);

        if (ok) {
            ++totalReceived_;
            QByteArray frame;
            { QMutexLocker lk(&frameMutex_); frame = it->frame; }
            emit dmxFrameReceived(universe, frame);
        }
    }
}

bool DmxReceiver::parseSacn(const QByteArray& data, quint16 universe, UniverseState& st) {
    if (data.size() < 126) return false;

    e131_packet_t pkt;
    std::memset(&pkt, 0, sizeof(pkt));
    std::memcpy(&pkt, data.constData(), qMin(int(sizeof(pkt)), data.size()));

    if (e131_pkt_validate(&pkt) != E131_ERR_NONE) return false;
    if (qFromBigEndian<quint16>(pkt.frame.universe) != universe) return false;
    if (e131_pkt_discard(&pkt, st.lastSeq)) return false;
    st.lastSeq = pkt.frame.seq_number;

    // prop_val[0] = null start code; prop_val[1..512] = channels
    QMutexLocker lk(&frameMutex_);
    std::memcpy(st.frame.data(), &pkt.dmp.prop_val[1], 512);
    return true;
}

bool DmxReceiver::parseArtNet(const QByteArray& data, quint16 /*universe*/, UniverseState& st) {
    // Minimum ArtDmx packet: 18 bytes header + data
    if (data.size() < 18) return false;
    if (std::memcmp(data.constData(), "Art-Net\0", 8) != 0) return false;

    const quint16 opCode = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData() + 8));
    if (opCode != kArtOpDmx) return false;

    // subUni at offset 14, net at offset 15
    const quint8 subUni = quint8(data[14]);
    const quint8 net    = quint8(data[15]);
    const quint16 pktUni = quint16((quint16(net) << 8) | subUni) + 1; // convert back to 1-based
    if (pktUni != st.config.universe) return false;

    // Length at offset 16-17 (big-endian)
    const int len = qMin(int(qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData() + 16))), 512);

    if (data.size() < 18 + len) return false;

    QMutexLocker lk(&frameMutex_);
    std::memcpy(st.frame.data(), data.constData() + 18, len);
    return true;
}
