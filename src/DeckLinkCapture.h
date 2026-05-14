#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
// DeckLink API uses CoreFoundation COM-style interfaces.
#include <DeckLinkAPI.h>
#endif

class DeckLinkCapture : public QObject {
    Q_OBJECT
public:
    explicit DeckLinkCapture(QObject* parent = nullptr);
    ~DeckLinkCapture() override;

    void    setDeviceName(const QString& name);
    QString deviceName() const;

    void start();
    void stop();

    // Returns display names for all available DeckLink devices.
    // If the DeckLink API is unavailable, returns empty and (optionally) sets error.
    static QStringList listDevices(QString* error = nullptr);

signals:
    void frameReady(const QImage& frame);
    void errorChanged(const QString& error);

private:
    QString deviceName_;

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    class InputCallback;

    IDeckLink*      deckLink_ = nullptr;
    IDeckLinkInput* input_ = nullptr;
    InputCallback*  callback_ = nullptr;

    void cleanupDeckLink();
    void handleFormatChanged(IDeckLinkDisplayMode* newDisplayMode,
                             BMDDetectedVideoInputFormatFlags detectedFlags);

#ifdef Q_OS_WIN
    static QString bstrToQString(BSTR bstr);
#else
    static QString cfStringToQString(CFStringRef s);
    static bool    refiidEqual(REFIID a, REFIID b);
#endif
#endif
};
