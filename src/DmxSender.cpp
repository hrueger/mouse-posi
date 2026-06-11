#include "DmxSender.h"
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QtEndian>
#include <QDebug>
#include <cstring>
#include <e131.h>

static constexpr quint16 kSacnPort   = 5568;
static constexpr quint16 kArtNetPort = 6454;

// ArtNet OpDmx packet (minimal, fixed 512 channel payload)
#pragma pack(push, 1)
struct ArtDmxPacket {
    char     id[8]     = {'A','r','t','-','N','e','t','\0'};
    quint16  opCode    = 0x0050; // LE: OpDmx = 0x5000
    quint8   verHi     = 0;
    quint8   verLo     = 14;
    quint8   sequence  = 0;
    quint8   physical  = 0;
    quint8   subUni    = 0;  // low byte of 15-bit universe
    quint8   net       = 0;  // high 7 bits of 15-bit universe
    quint8   lenHi     = 2;  // length = 512
    quint8   lenLo     = 0;
    quint8   data[512] = {};
};
#pragma pack(pop)

DmxSender::DmxSender(QObject* parent) : QObject(parent) {}

DmxSender::~DmxSender() { teardown(); }

void DmxSender::teardown() {
    for (auto& st : states_) {
        if (st.socket) { st.socket->close(); st.socket->deleteLater(); st.socket = nullptr; }
    }
    states_.clear();
}

void DmxSender::stop() { teardown(); }

void DmxSender::configure(const QList<DmxUniverseConfig>& universes) {
    teardown();
    for (const auto& cfg : universes) {
        UniverseState st;
        st.config = cfg;
        st.socket = new QUdpSocket(this);

        if (!cfg.iface.isEmpty()) {
            const QNetworkInterface ni = QNetworkInterface::interfaceFromName(cfg.iface);
            if (ni.isValid())
                st.socket->setMulticastInterface(ni);
        }

        const bool bound = st.socket->bind(QHostAddress::AnyIPv4, 0,
                                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        if (!bound) {
            qWarning() << "DmxSender: bind failed for universe" << cfg.universe
                       << ":" << st.socket->errorString();
            st.socket->deleteLater();
            st.socket = nullptr;
        }

        states_[cfg.universe] = std::move(st);
    }
}

void DmxSender::sendFrame(quint16 universe, const QByteArray& dmx512) {
    auto it = states_.find(universe);
    if (it == states_.end() || !it->socket) return;

    QByteArray padded = dmx512;
    if (padded.size() < 512) padded.resize(512, '\0');

    if (it->config.protocol == DmxProtocol::SACN)
        sendSacn(*it, padded);
    else
        sendArtNet(*it, padded);

    ++totalSent_;
}

void DmxSender::sendSacn(UniverseState& st, const QByteArray& dmx512) {
    e131_packet_t pkt;
    e131_pkt_init(&pkt, st.config.universe, 512);
    pkt.frame.seq_number = ++st.seqSacn;
    std::memcpy(&pkt.dmp.prop_val[1], dmx512.constData(), 512);

    QHostAddress dest;
    quint16 destPort = kSacnPort;

    switch (st.config.netMode) {
    case DmxNetworkMode::Unicast:
        dest = QHostAddress(st.config.unicastIp);
        break;
    case DmxNetworkMode::Broadcast:
        dest = QHostAddress::Broadcast;
        break;
    default: // Multicast
        dest = QHostAddress(QString("239.255.%1.%2")
                            .arg(st.config.universe >> 8)
                            .arg(st.config.universe & 0xFF));
        break;
    }

    st.socket->writeDatagram(
        reinterpret_cast<const char*>(&pkt),
        static_cast<qint64>(sizeof(pkt)),
        dest, destPort);
}

void DmxSender::sendArtNet(UniverseState& st, const QByteArray& dmx512) {
    ArtDmxPacket pkt;
    const quint16 uni15 = st.config.universe - 1; // ArtNet is 0-based
    pkt.subUni   = quint8(uni15 & 0xFF);
    pkt.net      = quint8((uni15 >> 8) & 0x7F);
    pkt.sequence = ++st.seqArtNet;
    std::memcpy(pkt.data, dmx512.constData(), 512);

    QHostAddress dest;
    switch (st.config.netMode) {
    case DmxNetworkMode::Unicast:
        dest = QHostAddress(st.config.unicastIp);
        break;
    case DmxNetworkMode::Broadcast:
        dest = QHostAddress(QStringLiteral("2.255.255.255"));
        break;
    default: // Multicast — ArtNet uses broadcast on subnet
        dest = QHostAddress(QStringLiteral("2.255.255.255"));
        break;
    }

    st.socket->writeDatagram(
        reinterpret_cast<const char*>(&pkt),
        static_cast<qint64>(sizeof(pkt)),
        dest, kArtNetPort);
}

int DmxSender::totalFramesSent() const { return totalSent_; }
