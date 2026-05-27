#include "SacnReceiver.h"
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QtEndian>
#include <QDebug>
#include <e131.h>
#include <cstring>

static constexpr quint16 kSacnPort = 5568;

SacnReceiver::SacnReceiver(QObject* parent) : QObject(parent) {}

SacnReceiver::~SacnReceiver() { closeSocket(); }

void SacnReceiver::closeSocket() {
    if (!socket_) return;
    if (config_.mode == SacnMode::Multicast && !mcGroup_.isNull()) {
        for (const auto& name : mcIfaces_) {
            const QNetworkInterface ni = QNetworkInterface::interfaceFromName(name);
            if (ni.isValid())
                socket_->leaveMulticastGroup(mcGroup_, ni);
        }
        mcIfaces_.clear();
    }
    socket_->close();
    socket_->deleteLater();
    socket_  = nullptr;
    mcGroup_ = {};
}

void SacnReceiver::stop() { closeSocket(); }

void SacnReceiver::startListening(const SacnInputConfig& cfg) {
    closeSocket();
    config_   = cfg;
    lastSeq_  = 0;
    totalUdp_  = 0;
    totalSacn_ = 0;

    socket_ = new QUdpSocket(this);
    connect(socket_, &QUdpSocket::readyRead, this, &SacnReceiver::onReadyRead);

    // Bind the same way the PSN receiver does — this is what makes it work
    // on macOS for both multicast and same-machine traffic.
    const bool bound = socket_->bind(QHostAddress::AnyIPv4, kSacnPort,
                                     QUdpSocket::ShareAddress
                                     | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        qWarning() << "sACN: bind failed:" << socket_->errorString();
        return;
    }

    if (cfg.mode == SacnMode::Multicast) {
        mcGroup_ = QHostAddress(QString("239.255.%1.%2")
                                .arg(cfg.universe >> 8).arg(cfg.universe & 0xFF));

        auto joinOn = [&](const QNetworkInterface& ni) {
            const bool ok = socket_->joinMulticastGroup(mcGroup_, ni);
            if (ok) mcIfaces_.append(ni.name());
            qDebug() << "sACN: join" << mcGroup_.toString() << "on" << ni.name()
                     << (ok ? "ok" : socket_->errorString());
        };

        if (!cfg.iface.isEmpty()) {
            const QNetworkInterface ni = QNetworkInterface::interfaceFromName(cfg.iface);
            if (ni.isValid()) joinOn(ni);
        } else {
            // On macOS the default multicast route resolves to lo0, so a no-interface
            // join never receives packets from the network. Join on every non-loopback
            // IPv4-capable interface instead.
            for (const auto& ni : QNetworkInterface::allInterfaces()) {
                if (!(ni.flags() & QNetworkInterface::IsUp)) continue;
                if (!(ni.flags() & QNetworkInterface::CanMulticast)) continue;
                if (ni.flags() & QNetworkInterface::IsLoopBack) continue;
                bool hasIPv4 = false;
                for (const auto& entry : ni.addressEntries())
                    if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) { hasIPv4 = true; break; }
                if (!hasIPv4) continue;
                joinOn(ni);
            }
        }

        qDebug() << "sACN: multicast" << mcGroup_.toString()
                 << "joined on" << mcIfaces_.size() << "interface(s)"
                 << "uni=" << cfg.universe << "addr=" << cfg.address;
    } else {
        qDebug() << "sACN: unicast port" << kSacnPort
                 << "uni=" << cfg.universe << "addr=" << cfg.address;
    }
}

void SacnReceiver::onReadyRead() {
    while (socket_ && socket_->hasPendingDatagrams()) {
        const QByteArray data = socket_->receiveDatagram().data();
        ++totalUdp_;

        // 126 bytes = root(38) + framing(77) + DMP header(11): minimum to validate
        if (data.size() < 126) continue;

        e131_packet_t pkt;
        std::memset(&pkt, 0, sizeof(pkt));
        std::memcpy(&pkt, data.constData(),
                    qMin(static_cast<int>(sizeof(pkt)), data.size()));

        if (e131_pkt_validate(&pkt) != E131_ERR_NONE)                          continue;
        if (qFromBigEndian<quint16>(pkt.frame.universe) != config_.universe)    continue;
        if (e131_pkt_discard(&pkt, lastSeq_))                                   continue;
        lastSeq_ = pkt.frame.seq_number;

        if (config_.address < 1 || config_.address > 512)                      continue;

        // prop_val[0] = null start code; prop_val[1..512] = DMX channels
        const quint8 value = pkt.dmp.prop_val[config_.address];
        ++totalSacn_;

        const float h = config_.minHeight
            + (value / 255.0f) * (config_.maxHeight - config_.minHeight);

        const QString src = QString::fromUtf8(
            reinterpret_cast<const char*>(pkt.frame.source_name), 64).trimmed();

        emit heightReceived(h, src);
    }
}
