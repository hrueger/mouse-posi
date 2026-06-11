#pragma once
#include "InputAdapterBase.h"
#include "../Project.h"

class DmxReceiver;

// Input adapter that reads DMX channels from sACN or ArtNet and maps them
// to click-plane height and operator control values (dimmer, zoom, iris, focus).
class SacnArtNetInputAdapter : public InputAdapterBase {
    Q_OBJECT
public:
    explicit SacnArtNetInputAdapter(const InputAdapterConfig& config, QObject* parent = nullptr);
    ~SacnArtNetInputAdapter() override;

    void start() override;
    void stop()  override;

private:
    InputAdapterConfig config_;
    DmxReceiver*       receiver_ = nullptr;

    void onDmxFrame(quint16 universe, const QByteArray& frame);
};
