#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <cstdint>

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
#include <DeckLinkAPI.h>
#endif

class DeckLinkCapture : public QObject {
    Q_OBJECT
public:
    enum class Connection {
        Unspecified = 0,
        SDI,
        HDMI,
        OpticalSDI,
        Component,
        Composite,
        SVideo,
    };

    // Device info returned by listDeviceInfos().
    struct DeviceInfo {
        QString displayName;
        QString persistentId;  // "<id>_<model>" hash; fallback = displayName
    };

    // Display mode entry returned by listDisplayModes().
    // mode == 0 (bmdModeUnknown) means "Auto" (format-detection).
    struct DisplayModeInfo {
        QString  name;
        uint32_t mode;  // BMDDisplayMode cast to uint32_t
    };

    explicit DeckLinkCapture(QObject* parent = nullptr);
    ~DeckLinkCapture() override;

    // Primary device identifier is the persistent-ID hash, not the display name.
    void    setDeviceId(const QString& persistentId);
    QString deviceId() const;

    void       setConnection(Connection conn);
    Connection connection() const;

    // Allow10Bit: true = prefer 10-bit YUV capture (default); false = 8-bit YUV.
    void setAllow10Bit(bool allow);
    bool allow10Bit() const;

    // Display mode: 0 = auto (format detection); other = specific BMDDisplayMode value.
    void     setDisplayMode(uint32_t mode);
    uint32_t displayMode() const;

    void start();
    void stop();

    // Returns all connected DeckLink devices.
    static QList<DeviceInfo>      listDeviceInfos(QString* error = nullptr);
    // Returns supported input connections for the device identified by persistentId.
    static QList<Connection>      supportedConnections(const QString& persistentId);
    // Returns supported input display modes; always includes "Auto" entry if format detection supported.
    static QList<DisplayModeInfo> listDisplayModes(const QString& persistentId);

    static QString connectionName(Connection conn);

signals:
    void frameReady(const QImage& frame);
    void errorChanged(const QString& error);

private:
    QString    deviceId_;
    Connection connection_  = Connection::Unspecified;
    bool       allow10Bit_  = true;
    uint32_t   displayMode_ = 0;

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    class InputCallback;
    class CpuVideoFrame;

    IDeckLink*                deckLink_     = nullptr;
    IDeckLinkInput*           input_        = nullptr;
    IDeckLinkVideoConversion* converter_    = nullptr;
    InputCallback*            callback_     = nullptr;
    CpuVideoFrame*            convertFrame_ = nullptr;

    BMDPixelFormat currentPixelFormat_ = bmdFormat10BitYUV;

    void cleanupDeckLink();
    void restartWithFormat(BMDDisplayMode mode, BMDPixelFormat pixelFormat);

    static QString    persistentIdFromDevice(IDeckLink* dev);
    static IDeckLink* findDeviceById(const QString& persistentId);

#ifdef Q_OS_WIN
    static QString bstrToQString(BSTR bstr);
#else
    static QString cfStringToQString(CFStringRef s);
    static bool    refiidEqual(REFIID a, REFIID b);
#endif
#endif
};
