#pragma once
#include "InputAdapterBase.h"
#include "../Project.h"

class MidiInputAdapter : public InputAdapterBase {
    Q_OBJECT
public:
    explicit MidiInputAdapter(const InputAdapterConfig& config, QObject* parent = nullptr);
    ~MidiInputAdapter() override;

    void start() override;
    void stop()  override;

    void setLearning(bool on);

    // List available MIDI input port names (static utility, usable from UI)
    static QStringList availablePorts();

signals:
    void midiEventReceived(int cc, int channel, int rawValue);
    void learnedCC(int cc, int channel);

private:
    static void rtMidiCallback(double ts, std::vector<unsigned char>* msg, void* userData);
    void handleMessage(const std::vector<unsigned char>& msg);

    InputAdapterConfig config_;
    void*              midiIn_   = nullptr;  // RtMidiIn*, cast in .cpp
    bool               running_  = false;
    bool               learning_ = false;
};
