#pragma once
#include <QObject>
#include <QString>

// Abstract base for operator input adapters (sACN/ArtNet, MIDI, etc.).
// Adapters emit signals that drive click-plane height and FS operator controls.
class InputAdapterBase : public QObject {
    Q_OBJECT
public:
    explicit InputAdapterBase(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~InputAdapterBase() = default;

    virtual void start()  = 0;
    virtual void stop()   = 0;

signals:
    // Emitted when click-plane height changes (metres)
    void clickPlaneHeightChanged(float metres);

    // Emitted for operator controls: target is "dimmer", "zoom", "iris", "focus"
    // value is 0.0–1.0 normalised
    void operatorValueChanged(QString target, float value);
};
