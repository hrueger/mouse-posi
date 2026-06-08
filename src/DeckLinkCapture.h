#pragma once

#include <QObject>
#include <QImage>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <atomic>
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

    struct DeviceInfo {
        QString displayName;
        QString persistentId;
    };

    struct DisplayModeInfo {
        QString  name;
        uint32_t mode;
    };

    explicit DeckLinkCapture(QObject* parent = nullptr);
    ~DeckLinkCapture() override;

    void    setDeviceId(const QString& persistentId);
    QString deviceId() const;

    void       setConnection(Connection conn);
    Connection connection() const;

    void setAllow10Bit(bool allow);
    bool allow10Bit() const;

    void     setDisplayMode(uint32_t mode);
    uint32_t displayMode() const;

    void start();
    void stop();

    static QList<DeviceInfo>      listDeviceInfos(QString* error = nullptr);
    static QList<Connection>      supportedConnections(const QString& persistentId);
    static QList<DisplayModeInfo> listDisplayModes(const QString& persistentId);
    static QString                connectionName(Connection conn);

signals:
    void frameReady(const QImage& frame);
    void errorChanged(const QString& error);

private:
    QString    deviceId_;
    Connection connection_  = Connection::Unspecified;
    bool       allow10Bit_  = true;
    uint32_t   displayMode_ = 0;
    bool       streaming_   = false;

    // Incremented on every start() so stale Qt-queued lambdas self-discard.
    std::atomic<uint64_t> generation_{0};

    // Last confirmed working (BMDDisplayMode, BMDPixelFormat) per Connection (cast to int).
    // VideoInputFormatChanged populates this; start() uses it to skip the NTSC→format-detect
    // cycle on second+ starts, since the hardware only fires VideoInputFormatChanged once per
    // power-on for a given signal.
    QMap<int, QPair<quint32, quint32>> modeCache_;

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    class InputCallback;
    class CpuVideoFrame;

    // Device-lifetime objects — acquired once in openDevice(), released in closeDevice().
    // IDeckLinkConfiguration must be kept alive for the device lifetime per SDK docs;
    // releasing it immediately after SetInt can cause the hardware change to be discarded.
    IDeckLink*                deckLink_     = nullptr;
    IDeckLinkConfiguration*   config_       = nullptr;
    IDeckLinkVideoConversion* converter_    = nullptr;

    // Stream-lifetime objects — acquired in start(), released in stop().
    IDeckLinkInput*           input_        = nullptr;
    InputCallback*            callback_     = nullptr;
    CpuVideoFrame*            convertFrame_ = nullptr;

    BMDPixelFormat currentPixelFormat_ = bmdFormat10BitYUV;

    void openDevice();
    void closeDevice();

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
