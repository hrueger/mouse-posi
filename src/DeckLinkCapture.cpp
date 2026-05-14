#include "DeckLinkCapture.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <atomic>
#include <cstring>

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE

namespace {
static constexpr BMDPixelFormat kPixelFormat = bmdFormat8BitBGRA;
static constexpr BMDVideoInputFlags kVideoFlags = bmdVideoInputEnableFormatDetection;

static QString hresultToString(HRESULT hr) {
    return QStringLiteral("0x%1").arg(quintptr(hr), 0, 16);
}
}

class DeckLinkCapture::InputCallback final : public IDeckLinkInputCallback {
public:
    explicit InputCallback(DeckLinkCapture* owner) : owner_(owner) {}

    void clearOwner() {
        QMutexLocker lock(&mutex_);
        owner_ = nullptr;
    }

    // IUnknown
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;

        const REFIID iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
        if (DeckLinkCapture::refiidEqual(iid, IID_IDeckLinkInputCallback) ||
            DeckLinkCapture::refiidEqual(iid, iunknown)) {
            *ppv = static_cast<IDeckLinkInputCallback*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG AddRef() override {
        return ++refCount_;
    }

    ULONG Release() override {
        const ULONG n = --refCount_;
        if (n == 0)
            delete this;
        return n;
    }

    // IDeckLinkInputCallback
    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents,
                                    IDeckLinkDisplayMode* newDisplayMode,
                                    BMDDetectedVideoInputFormatFlags detectedFlags) override
    {
        DeckLinkCapture* owner = nullptr;
        {
            QMutexLocker lock(&mutex_);
            owner = owner_;
        }
        if (owner)
            owner->handleFormatChanged(newDisplayMode, detectedFlags);
        return S_OK;
    }

    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                   IDeckLinkAudioInputPacket*) override
    {
        DeckLinkCapture* owner = nullptr;
        {
            QMutexLocker lock(&mutex_);
            owner = owner_;
        }
        if (!owner || !videoFrame)
            return S_OK;

        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
            return S_OK;

        void* bytes = nullptr;
        IDeckLinkVideoBuffer* buffer = nullptr;
        if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void**>(&buffer)) != S_OK || !buffer)
            return S_OK;

        if (buffer->StartAccess(bmdBufferAccessRead) == S_OK) {
            (void)buffer->GetBytes(&bytes);
            (void)buffer->EndAccess(bmdBufferAccessRead);
        }

        buffer->Release();
        buffer = nullptr;

        if (!bytes)
            return S_OK;

        const int width = static_cast<int>(videoFrame->GetWidth());
        const int height = static_cast<int>(videoFrame->GetHeight());
        const int rowBytes = static_cast<int>(videoFrame->GetRowBytes());

        // DeckLink BGRA matches QImage::Format_ARGB32 memory layout on little-endian.
        QImage img(reinterpret_cast<const uchar*>(bytes), width, height, rowBytes, QImage::Format_ARGB32);
        if (img.isNull())
            return S_OK;

        const QImage copy = img.copy();
        QPointer<DeckLinkCapture> safeOwner(owner);
        QMetaObject::invokeMethod(owner, [safeOwner, copy]() {
            if (!safeOwner)
                return;
            emit safeOwner->frameReady(copy);
        }, Qt::QueuedConnection);

        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_ {1};
    QMutex mutex_;
    DeckLinkCapture* owner_ = nullptr;
};

DeckLinkCapture::DeckLinkCapture(QObject* parent)
    : QObject(parent)
{
}

DeckLinkCapture::~DeckLinkCapture() {
    stop();
}

void DeckLinkCapture::setDeviceName(const QString& name) {
    if (deviceName_ == name)
        return;

    deviceName_ = name;

    if (input_) {
        // Device changed while running: restart.
        start();
    }
}

QString DeckLinkCapture::deviceName() const {
    return deviceName_;
}

QStringList DeckLinkCapture::listDevices(QString* error) {
    if (error)
        *error = QString();

    QStringList out;

    IDeckLinkIterator* it = CreateDeckLinkIteratorInstance();
    if (!it) {
        if (error)
            *error = QStringLiteral("DeckLink API not found. Install Blackmagic Desktop Video.");
        return out;
    }

    IDeckLink* deckLink = nullptr;
    while (it->Next(&deckLink) == S_OK && deckLink) {
        CFStringRef cfName = nullptr;
        if (deckLink->GetDisplayName(&cfName) == S_OK && cfName) {
            out.append(cfStringToQString(cfName));
            CFRelease(cfName);
        }
        deckLink->Release();
        deckLink = nullptr;
    }

    it->Release();

    return out;
}

void DeckLinkCapture::start() {
    stop();

    if (deviceName_.isEmpty()) {
        emit errorChanged(QStringLiteral("No DeckLink device selected"));
        return;
    }

    IDeckLinkIterator* it = CreateDeckLinkIteratorInstance();
    if (!it) {
        emit errorChanged(QStringLiteral("DeckLink API not found. Install Blackmagic Desktop Video."));
        return;
    }

    IDeckLink* deckLink = nullptr;
    while (it->Next(&deckLink) == S_OK && deckLink) {
        CFStringRef cfName = nullptr;
        QString name;
        if (deckLink->GetDisplayName(&cfName) == S_OK && cfName) {
            name = cfStringToQString(cfName);
            CFRelease(cfName);
        }

        if (!name.isEmpty() && name == deviceName_) {
            deckLink_ = deckLink;
            break;
        }

        deckLink->Release();
        deckLink = nullptr;
    }

    it->Release();

    if (!deckLink_) {
        emit errorChanged(QStringLiteral("DeckLink device not found: %1").arg(deviceName_));
        return;
    }

    if (deckLink_->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(&input_)) != S_OK || !input_) {
        cleanupDeckLink();
        emit errorChanged(QStringLiteral("Selected device does not support input capture"));
        return;
    }

    callback_ = new InputCallback(this);
    input_->SetCallback(callback_);

    // Use format detection; DeckLink will call VideoInputFormatChanged when signal is detected.
    const HRESULT en = input_->EnableVideoInput(bmdModeHD1080p30, kPixelFormat, kVideoFlags);
    if (en != S_OK) {
        cleanupDeckLink();
        emit errorChanged(QStringLiteral("EnableVideoInput failed (%1)").arg(hresultToString(en)));
        return;
    }

    const HRESULT st = input_->StartStreams();
    if (st != S_OK) {
        cleanupDeckLink();
        emit errorChanged(QStringLiteral("StartStreams failed (%1)").arg(hresultToString(st)));
        return;
    }

    emit errorChanged(QString());
}

void DeckLinkCapture::stop() {
    if (!input_ && !deckLink_)
        return;

    if (input_) {
        input_->StopStreams();
        input_->FlushStreams();
        input_->DisableVideoInput();
        input_->SetCallback(nullptr);
    }

    cleanupDeckLink();
}

void DeckLinkCapture::cleanupDeckLink() {
    if (input_ && callback_) {
        input_->SetCallback(nullptr);
    }

    if (callback_) {
        callback_->clearOwner();
        callback_->Release();
        callback_ = nullptr;
    }

    if (input_) {
        input_->Release();
        input_ = nullptr;
    }

    if (deckLink_) {
        deckLink_->Release();
        deckLink_ = nullptr;
    }
}

void DeckLinkCapture::handleFormatChanged(IDeckLinkDisplayMode* newDisplayMode,
                                         BMDDetectedVideoInputFormatFlags) {
    if (!input_ || !newDisplayMode)
        return;

    // Reconfigure to the detected mode.
    input_->StopStreams();
    input_->FlushStreams();
    input_->DisableVideoInput();

    const BMDDisplayMode mode = newDisplayMode->GetDisplayMode();
    const HRESULT en = input_->EnableVideoInput(mode, kPixelFormat, kVideoFlags);
    if (en != S_OK) {
        QMetaObject::invokeMethod(this, [this, en]() {
            emit errorChanged(QStringLiteral("DeckLink reconfigure failed (%1)").arg(hresultToString(en)));
        }, Qt::QueuedConnection);
        return;
    }

    const HRESULT st = input_->StartStreams();
    if (st != S_OK) {
        QMetaObject::invokeMethod(this, [this, st]() {
            emit errorChanged(QStringLiteral("DeckLink StartStreams failed (%1)").arg(hresultToString(st)));
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        emit errorChanged(QString());
    }, Qt::QueuedConnection);
}

QString DeckLinkCapture::cfStringToQString(CFStringRef s) {
    if (!s) return {};

    const CFIndex length = CFStringGetLength(s);
    if (length <= 0) return {};

    const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buf;
    buf.resize(int(maxSize));
    if (CFStringGetCString(s, buf.data(), maxSize, kCFStringEncodingUTF8))
        return QString::fromUtf8(buf.constData());

    return {};
}

bool DeckLinkCapture::refiidEqual(REFIID a, REFIID b) {
    return std::memcmp(&a, &b, sizeof(REFIID)) == 0;
}

#else

DeckLinkCapture::DeckLinkCapture(QObject* parent) : QObject(parent) {}
DeckLinkCapture::~DeckLinkCapture() { stop(); }

void DeckLinkCapture::setDeviceName(const QString& name) { deviceName_ = name; }
QString DeckLinkCapture::deviceName() const { return deviceName_; }
void DeckLinkCapture::start() { emit errorChanged(QStringLiteral("DeckLink support not available on this platform")); }
void DeckLinkCapture::stop() {}
QStringList DeckLinkCapture::listDevices(QString* error) {
    if (error) *error = QStringLiteral("DeckLink support not available on this platform");
    return {};
}

#endif
