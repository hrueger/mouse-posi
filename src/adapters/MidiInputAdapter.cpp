#include "MidiInputAdapter.h"
#include <RtMidi.h>
#include <QDebug>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static RtMidiIn* asMidi(void* p) { return static_cast<RtMidiIn*>(p); }

// ── static utility ───────────────────────────────────────────────────────────

QStringList MidiInputAdapter::availablePorts() {
    QStringList out;
    try {
        RtMidiIn tmp;
        unsigned int n = tmp.getPortCount();
        out.reserve(int(n));
        for (unsigned int i = 0; i < n; ++i)
            out << QString::fromStdString(tmp.getPortName(i));
    } catch (const RtMidiError& e) {
        qWarning() << "MidiInputAdapter::availablePorts:" << e.getMessage().c_str();
    }
    return out;
}

// ── lifecycle ─────────────────────────────────────────────────────────────────

MidiInputAdapter::MidiInputAdapter(const InputAdapterConfig& config, QObject* parent)
    : InputAdapterBase(parent), config_(config)
{
    try {
        midiIn_ = new RtMidiIn();
    } catch (const RtMidiError& e) {
        qWarning() << "MidiInputAdapter: init error:" << e.getMessage().c_str();
    }
}

MidiInputAdapter::~MidiInputAdapter() {
    stop();
    delete asMidi(midiIn_);
    midiIn_ = nullptr;
}

void MidiInputAdapter::start() {
    if (!midiIn_ || running_) return;

    // iface field is repurposed as MIDI port name for MIDI adapters
    const QString targetPort = config_.iface;

    try {
        RtMidiIn* in = asMidi(midiIn_);
        unsigned int portCount = in->getPortCount();
        if (portCount == 0) {
            qWarning() << "MidiInputAdapter: no MIDI ports available";
            return;
        }
        int portIndex = -1;
        for (unsigned int i = 0; i < portCount; ++i) {
            if (QString::fromStdString(in->getPortName(i)) == targetPort) {
                portIndex = int(i);
                break;
            }
        }
        if (portIndex < 0) portIndex = 0;  // fallback to first port

        in->openPort(unsigned(portIndex));
        in->ignoreTypes(true, true, true);  // ignore sysex, timing, active sensing
        in->setCallback(&MidiInputAdapter::rtMidiCallback, this);
        running_ = true;
        qDebug() << "MidiInputAdapter: opened port" << portIndex
                 << QString::fromStdString(in->getPortName(unsigned(portIndex)));
    } catch (const RtMidiError& e) {
        qWarning() << "MidiInputAdapter::start:" << e.getMessage().c_str();
    }
}

void MidiInputAdapter::stop() {
    if (!midiIn_ || !running_) return;
    try {
        RtMidiIn* in = asMidi(midiIn_);
        in->cancelCallback();
        in->closePort();
    } catch (...) {}
    running_ = false;
}

// ── MIDI callback (called from RtMidi's internal thread) ─────────────────────

void MidiInputAdapter::rtMidiCallback(double /*ts*/,
                                       std::vector<unsigned char>* msg,
                                       void* userData) {
    auto* self = static_cast<MidiInputAdapter*>(userData);
    if (!msg || msg->empty()) return;
    // Marshal to the Qt thread
    QMetaObject::invokeMethod(self,
        [self, bytes = *msg]() { self->handleMessage(bytes); },
        Qt::QueuedConnection);
}

void MidiInputAdapter::handleMessage(const std::vector<unsigned char>& msg) {
    if (msg.size() < 3) return;

    const unsigned char status = msg[0] & 0xF0u;
    if (status != 0xB0u) return;  // only CC messages

    const int midiChannel = (msg[0] & 0x0Fu) + 1;  // 1-based
    const int cc          = int(msg[1]);
    const float raw       = float(msg[2]) / 127.f;  // 0–1

    for (const auto& m : config_.mappings) {
        if (m.midiCC < 0 || m.midiCC != cc) continue;
        if (m.midiChannel > 0 && m.midiChannel != midiChannel) continue;

        const float value = m.minValue + raw * (m.maxValue - m.minValue);

        if (m.target == QLatin1String("clickPlaneHeight"))
            emit clickPlaneHeightChanged(value);
        else
            emit operatorValueChanged(m.target, raw);
    }
}
