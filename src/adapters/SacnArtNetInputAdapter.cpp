#include "SacnArtNetInputAdapter.h"
#include "../DmxReceiver.h"
#include <QDebug>

SacnArtNetInputAdapter::SacnArtNetInputAdapter(const InputAdapterConfig& config, QObject* parent)
    : InputAdapterBase(parent), config_(config)
{
}

SacnArtNetInputAdapter::~SacnArtNetInputAdapter() { stop(); }

void SacnArtNetInputAdapter::start() {
    if (receiver_) return;

    // Collect the unique universes required by the mappings
    QSet<quint16> universeSet;
    for (const auto& m : config_.mappings)
        universeSet.insert(m.universe);

    QList<DmxUniverseConfig> universes;
    for (quint16 uni : universeSet) {
        DmxUniverseConfig uc;
        uc.universe  = uni;
        uc.protocol  = config_.protocol;
        uc.netMode   = config_.netMode;
        uc.iface     = config_.iface;
        uc.unicastIp = config_.unicastIp;
        universes.append(uc);
    }

    receiver_ = new DmxReceiver(this);
    receiver_->configure(universes);
    connect(receiver_, &DmxReceiver::dmxFrameReceived,
            this, &SacnArtNetInputAdapter::onDmxFrame);
}

void SacnArtNetInputAdapter::stop() {
    if (!receiver_) return;
    receiver_->stop();
    receiver_->deleteLater();
    receiver_ = nullptr;
}

void SacnArtNetInputAdapter::onDmxFrame(quint16 universe, const QByteArray& frame) {
    for (const auto& m : config_.mappings) {
        if (m.universe != universe) continue;
        if (m.channel < 1 || m.channel > 512) continue;

        const quint8 raw = quint8(frame[m.channel - 1]);
        const float normalised = raw / 255.0f;
        const float physical = m.minValue + normalised * (m.maxValue - m.minValue);

        if (m.target == QStringLiteral("clickPlaneHeight"))
            emit clickPlaneHeightChanged(physical);
        else
            emit operatorValueChanged(m.target, normalised);
    }
}
