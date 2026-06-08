#include "DeckLinkCapture.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <atomic>
#include <cstring>
#include <vector>

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE

namespace {

static constexpr BMDPixelFormat kOutputFormat = bmdFormat8BitBGRA;

static QString hresultToString(HRESULT hr) {
    return QStringLiteral("0x%1").arg(quintptr(hr), 0, 16);
}

static BMDVideoConnection toBMDConnection(DeckLinkCapture::Connection conn) {
    switch (conn) {
    case DeckLinkCapture::Connection::SDI:        return bmdVideoConnectionSDI;
    case DeckLinkCapture::Connection::HDMI:       return bmdVideoConnectionHDMI;
    case DeckLinkCapture::Connection::OpticalSDI: return bmdVideoConnectionOpticalSDI;
    case DeckLinkCapture::Connection::Component:  return bmdVideoConnectionComponent;
    case DeckLinkCapture::Connection::Composite:  return bmdVideoConnectionComposite;
    case DeckLinkCapture::Connection::SVideo:     return bmdVideoConnectionSVideo;
    default:                                       return bmdVideoConnectionUnspecified;
    }
}

static DeckLinkCapture::Connection fromBMDConnection(BMDVideoConnection bmd) {
    switch (bmd) {
    case bmdVideoConnectionSDI:        return DeckLinkCapture::Connection::SDI;
    case bmdVideoConnectionHDMI:       return DeckLinkCapture::Connection::HDMI;
    case bmdVideoConnectionOpticalSDI: return DeckLinkCapture::Connection::OpticalSDI;
    case bmdVideoConnectionComponent:  return DeckLinkCapture::Connection::Component;
    case bmdVideoConnectionComposite:  return DeckLinkCapture::Connection::Composite;
    case bmdVideoConnectionSVideo:     return DeckLinkCapture::Connection::SVideo;
    default:                            return DeckLinkCapture::Connection::Unspecified;
    }
}

static IDeckLinkIterator* createIterator() {
#ifdef Q_OS_WIN
    IDeckLinkIterator* it = nullptr;
    CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
                     IID_IDeckLinkIterator, reinterpret_cast<void**>(&it));
    return it;
#else
    return CreateDeckLinkIteratorInstance();
#endif
}

} // namespace

// ── CpuVideoFrame ─────────────────────────────────────────────────────────────

class DeckLinkCapture::CpuVideoFrame
    : public IDeckLinkMutableVideoFrame
    , public IDeckLinkVideoBuffer
{
public:
    CpuVideoFrame(long w, long h)
        : width_(w), height_(h), rowBytes_(w * 4)
    {
        buffer_.resize(static_cast<size_t>(rowBytes_ * h));
    }

    void* rawBytes() { return buffer_.data(); }

    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
#ifdef Q_OS_WIN
        if (IsEqualIID(iid, IID_IDeckLinkVideoFrame) ||
            IsEqualIID(iid, IID_IDeckLinkMutableVideoFrame) ||
            IsEqualIID(iid, IID_IUnknown)) {
            *ppv = static_cast<IDeckLinkMutableVideoFrame*>(this);
        } else if (IsEqualIID(iid, IID_IDeckLinkVideoBuffer)) {
            *ppv = static_cast<IDeckLinkVideoBuffer*>(this);
        } else {
            return E_NOINTERFACE;
        }
#else
        const REFIID iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
        if (DeckLinkCapture::refiidEqual(iid, IID_IDeckLinkVideoFrame) ||
            DeckLinkCapture::refiidEqual(iid, IID_IDeckLinkMutableVideoFrame) ||
            DeckLinkCapture::refiidEqual(iid, iunknown)) {
            *ppv = static_cast<IDeckLinkMutableVideoFrame*>(this);
        } else if (DeckLinkCapture::refiidEqual(iid, IID_IDeckLinkVideoBuffer)) {
            *ppv = static_cast<IDeckLinkVideoBuffer*>(this);
        } else {
            return E_NOINTERFACE;
        }
#endif
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override { return ++refCount_; }
    ULONG Release() override {
        const ULONG n = --refCount_;
        if (n == 0) delete this;
        return n;
    }

    long           GetWidth()       override { return width_; }
    long           GetHeight()      override { return height_; }
    long           GetRowBytes()    override { return rowBytes_; }
    BMDPixelFormat GetPixelFormat() override { return kOutputFormat; }
    BMDFrameFlags  GetFlags()       override { return bmdFrameFlagDefault; }
    HRESULT GetTimecode(BMDTimecodeFormat, IDeckLinkTimecode**) override { return S_FALSE; }
    HRESULT GetAncillaryData(IDeckLinkVideoFrameAncillary**) override    { return S_FALSE; }

    HRESULT SetFlags(BMDFrameFlags) override                                      { return S_OK; }
    HRESULT SetTimecode(BMDTimecodeFormat, IDeckLinkTimecode*) override           { return S_FALSE; }
    HRESULT SetTimecodeFromComponents(BMDTimecodeFormat, uint8_t, uint8_t,
                                      uint8_t, uint8_t, BMDTimecodeFlags) override{ return S_FALSE; }
    HRESULT SetAncillaryData(IDeckLinkVideoFrameAncillary*) override              { return S_FALSE; }
    HRESULT SetTimecodeUserBits(BMDTimecodeFormat, BMDTimecodeUserBits) override  { return S_FALSE; }
    HRESULT SetInterfaceProvider(REFIID, IUnknown*) override                      { return S_OK; }

    HRESULT GetBytes(void** buf) override {
        if (!buf) return E_INVALIDARG;
        *buf = buffer_.data();
        return S_OK;
    }
    HRESULT GetSize(uint64_t* size) override {
        if (!size) return E_INVALIDARG;
        *size = static_cast<uint64_t>(buffer_.size());
        return S_OK;
    }
    HRESULT StartAccess(BMDBufferAccessFlags) override { return S_OK; }
    HRESULT EndAccess(BMDBufferAccessFlags) override   { return S_OK; }

private:
    std::atomic<ULONG> refCount_{1};
    long               width_, height_, rowBytes_;
    std::vector<uint8_t> buffer_;
};

// ── InputCallback ─────────────────────────────────────────────────────────────

class DeckLinkCapture::InputCallback final : public IDeckLinkInputCallback {
public:
    explicit InputCallback(DeckLinkCapture* owner) : owner_(owner) {}

    void clearOwner() {
        QMutexLocker lock(&mutex_);
        owner_ = nullptr;
    }

    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
#ifdef Q_OS_WIN
        if (IsEqualIID(iid, IID_IDeckLinkInputCallback) ||
            IsEqualIID(iid, IID_IUnknown)) {
#else
        const REFIID iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
        if (DeckLinkCapture::refiidEqual(iid, IID_IDeckLinkInputCallback) ||
            DeckLinkCapture::refiidEqual(iid, iunknown)) {
#endif
            *ppv = static_cast<IDeckLinkInputCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refCount_; }
    ULONG Release() override {
        const ULONG n = --refCount_;
        if (n == 0) delete this;
        return n;
    }

    // Called on the SDK capture thread when the input format changes.
    // Following OBS's approach: reconfigure directly here without dispatching
    // to the main thread — PauseStreams/EnableVideoInput/FlushStreams/StartStreams
    // are safe and intended to be called from this callback.
    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events,
                                    IDeckLinkDisplayMode* newMode,
                                    BMDDetectedVideoInputFormatFlags detectedFlags) override
    {
        if (!newMode) return S_OK;

        DeckLinkCapture* owner = nullptr;
        {
            QMutexLocker lock(&mutex_);
            owner = owner_;
        }
        if (!owner || !owner->input_) return S_OK;

        BMDPixelFormat newPixelFormat = owner->currentPixelFormat_;
        bool formatChanged = false;

        if (events & bmdVideoInputColorspaceChanged) {
            const bool highBit = (detectedFlags & bmdDetectedVideoInput10BitDepth) ||
                                 (detectedFlags & bmdDetectedVideoInput12BitDepth);
            BMDPixelFormat candidate = newPixelFormat;
            if (detectedFlags & bmdDetectedVideoInputRGB444) {
                candidate = (highBit && owner->allow10Bit_) ? bmdFormat10BitRGBXLE : bmdFormat8BitBGRA;
            } else if (detectedFlags & bmdDetectedVideoInputYCbCr422) {
                candidate = (highBit && owner->allow10Bit_) ? bmdFormat10BitYUV : bmdFormat8BitYUV;
            }
            if (candidate != newPixelFormat) {
                newPixelFormat = candidate;
                formatChanged = true;
            }
        }

        const bool modeChanged = (events & bmdVideoInputDisplayModeChanged) != 0;
        if (!formatChanged && !modeChanged) return S_OK;

        const BMDDisplayMode detectedMode = newMode->GetDisplayMode();
        owner->currentPixelFormat_ = newPixelFormat;

        owner->input_->PauseStreams();
        owner->input_->EnableVideoInput(detectedMode, newPixelFormat, bmdVideoInputEnableFormatDetection);
        owner->input_->FlushStreams();
        owner->input_->StartStreams();

        // Cache this working (mode, pixelFormat) so start() can use it directly next time,
        // bypassing the NTSC→format-detect cycle that only fires once per device power-on.
        const int connKey = static_cast<int>(owner->connection_);
        const uint64_t gen = owner->generation_.load();
        QPointer<DeckLinkCapture> safeOwner(owner);
        QMetaObject::invokeMethod(owner, [safeOwner, gen, connKey, detectedMode, newPixelFormat]() {
            if (safeOwner && safeOwner->generation_.load() == gen)
                safeOwner->modeCache_[connKey] = {static_cast<quint32>(detectedMode),
                                                   static_cast<quint32>(newPixelFormat)};
        }, Qt::QueuedConnection);

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
        if (!owner || !videoFrame) return S_OK;

        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
            if (!noSourceReported_.exchange(true)) {
                const uint64_t gen = owner->generation_.load();
                QPointer<DeckLinkCapture> safeOwner(owner);
                QMetaObject::invokeMethod(owner, [safeOwner, gen]() {
                    if (safeOwner && safeOwner->generation_.load() == gen)
                        emit safeOwner->errorChanged(QStringLiteral("No input signal detected"));
                }, Qt::QueuedConnection);
            }
            return S_OK;
        }
        noSourceReported_.store(false);

        IDeckLinkVideoConversion* conv = owner->converter_;
        if (!conv) return S_OK;

        const long w = videoFrame->GetWidth();
        const long h = videoFrame->GetHeight();
        if (w <= 0 || h <= 0) return S_OK;

        if (!owner->convertFrame_ ||
            owner->convertFrame_->GetWidth() != w ||
            owner->convertFrame_->GetHeight() != h) {
            delete owner->convertFrame_;
            owner->convertFrame_ = new CpuVideoFrame(w, h);
        }

        void* bytes    = nullptr;
        long  rowBytes = 0;

        if (videoFrame->GetPixelFormat() != kOutputFormat) {
            if (conv->ConvertFrame(videoFrame, owner->convertFrame_) != S_OK)
                return S_OK;
            bytes    = owner->convertFrame_->rawBytes();
            rowBytes = owner->convertFrame_->GetRowBytes();
        } else {
            IDeckLinkVideoBuffer* buf = nullptr;
            if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer,
                                           reinterpret_cast<void**>(&buf)) == S_OK && buf) {
                buf->GetBytes(&bytes);
                buf->Release();
            }
            rowBytes = videoFrame->GetRowBytes();
        }

        if (!bytes) return S_OK;

        QImage img(reinterpret_cast<const uchar*>(bytes),
                   static_cast<int>(w), static_cast<int>(h),
                   rowBytes, QImage::Format_ARGB32);
        if (img.isNull()) return S_OK;
        QImage copy = img.copy();

        const uint64_t gen = owner->generation_.load();
        QPointer<DeckLinkCapture> safeOwner(owner);
        QMetaObject::invokeMethod(owner, [safeOwner, gen, copy]() {
            if (!safeOwner || safeOwner->generation_.load() != gen) return;
            emit safeOwner->errorChanged(QString());
            emit safeOwner->frameReady(copy);
        }, Qt::QueuedConnection);

        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
    std::atomic<bool>  noSourceReported_{false};
    QMutex             mutex_;
    DeckLinkCapture*   owner_ = nullptr;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

QString DeckLinkCapture::persistentIdFromDevice(IDeckLink* dev) {
    IDeckLinkProfileAttributes* attrs = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkProfileAttributes,
                            reinterpret_cast<void**>(&attrs)) != S_OK || !attrs)
        return {};

    int64_t pid = 0;
    const bool hasPid = (attrs->GetInt(BMDDeckLinkPersistentID, &pid) == S_OK) ||
                        (attrs->GetInt(BMDDeckLinkTopologicalID, &pid) == S_OK);
    attrs->Release();

    if (!hasPid) return {};

    QString modelName;
#ifdef Q_OS_WIN
    BSTR bstrModel = nullptr;
    if (dev->GetModelName(&bstrModel) == S_OK)
        modelName = bstrToQString(bstrModel);
#else
    CFStringRef cfModel = nullptr;
    if (dev->GetModelName(&cfModel) == S_OK && cfModel) {
        modelName = cfStringToQString(cfModel);
        CFRelease(cfModel);
    }
#endif

    return QString::number(pid) + QLatin1Char('_') + modelName;
}

IDeckLink* DeckLinkCapture::findDeviceById(const QString& persistentId) {
    IDeckLinkIterator* it = createIterator();
    if (!it) return nullptr;

    IDeckLink* found = nullptr;
    IDeckLink* dev = nullptr;
    while (it->Next(&dev) == S_OK && dev) {
        if (persistentIdFromDevice(dev) == persistentId) {
            found = dev;
            break;
        }
        dev->Release();
        dev = nullptr;
    }
    it->Release();

    return found;
}

// ── Device lifetime ───────────────────────────────────────────────────────────

void DeckLinkCapture::openDevice() {
    if (deviceId_.isEmpty()) return;

    deckLink_ = findDeviceById(deviceId_);
    if (!deckLink_) return;

    // Acquire and hold the configuration interface for the device lifetime.
    // Per SDK docs, IDeckLinkConfiguration must outlive the IDeckLink object it was
    // obtained from — releasing it early can cause SetInt changes to be discarded.
    deckLink_->QueryInterface(IID_IDeckLinkConfiguration,
                              reinterpret_cast<void**>(&config_));

#ifdef Q_OS_WIN
    IDeckLinkVideoConversion* conv = nullptr;
    CoCreateInstance(CLSID_CDeckLinkVideoConversion, nullptr, CLSCTX_ALL,
                     IID_IDeckLinkVideoConversion, reinterpret_cast<void**>(&conv));
    converter_ = conv;
#else
    converter_ = CreateVideoConversionInstance();
#endif

    if (!converter_) {
        if (config_) { config_->Release(); config_ = nullptr; }
        deckLink_->Release();
        deckLink_ = nullptr;
    }
}

void DeckLinkCapture::closeDevice() {
    if (converter_) { converter_->Release(); converter_ = nullptr; }
    if (config_)    { config_->Release();    config_    = nullptr; }
    if (deckLink_)  { deckLink_->Release();  deckLink_  = nullptr; }
}

// ── Public API ────────────────────────────────────────────────────────────────

DeckLinkCapture::DeckLinkCapture(QObject* parent) : QObject(parent) {}

DeckLinkCapture::~DeckLinkCapture() {
    stop();
    closeDevice();
    delete convertFrame_;
    convertFrame_ = nullptr;
}

void DeckLinkCapture::setDeviceId(const QString& id) {
    if (deviceId_ == id) return;
    const bool wasStreaming = streaming_;
    stop();
    closeDevice();
    deviceId_ = id;
    modeCache_.clear();
    openDevice();
    if (wasStreaming) start();
}
QString DeckLinkCapture::deviceId() const { return deviceId_; }

void DeckLinkCapture::setConnection(Connection conn) {
    if (connection_ == conn) return;
    connection_ = conn;
    if (streaming_) start();
}
DeckLinkCapture::Connection DeckLinkCapture::connection() const { return connection_; }

void DeckLinkCapture::setAllow10Bit(bool allow) {
    if (allow10Bit_ == allow) return;
    allow10Bit_ = allow;
    if (streaming_) start();
}
bool DeckLinkCapture::allow10Bit() const { return allow10Bit_; }

void DeckLinkCapture::setDisplayMode(uint32_t mode) {
    if (displayMode_ == mode) return;
    displayMode_ = mode;
    if (streaming_) start();
}
uint32_t DeckLinkCapture::displayMode() const { return displayMode_; }

QString DeckLinkCapture::connectionName(Connection conn) {
    switch (conn) {
    case Connection::SDI:        return QStringLiteral("SDI");
    case Connection::HDMI:       return QStringLiteral("HDMI");
    case Connection::OpticalSDI: return QStringLiteral("Optical SDI");
    case Connection::Component:  return QStringLiteral("Component");
    case Connection::Composite:  return QStringLiteral("Composite");
    case Connection::SVideo:     return QStringLiteral("S-Video");
    default:                      return QStringLiteral("Auto");
    }
}

QList<DeckLinkCapture::DeviceInfo> DeckLinkCapture::listDeviceInfos(QString* error) {
    if (error) *error = QString();
    QList<DeviceInfo> out;

    IDeckLinkIterator* it = createIterator();
    if (!it) {
        if (error)
            *error = QStringLiteral("DeckLink API not found. Install Blackmagic Desktop Video.");
        return out;
    }

    IDeckLink* dev = nullptr;
    while (it->Next(&dev) == S_OK && dev) {
        DeviceInfo info;
#ifdef Q_OS_WIN
        BSTR bstrName = nullptr;
        if (dev->GetDisplayName(&bstrName) == S_OK)
            info.displayName = bstrToQString(bstrName);
#else
        CFStringRef cfName = nullptr;
        if (dev->GetDisplayName(&cfName) == S_OK && cfName) {
            info.displayName = cfStringToQString(cfName);
            CFRelease(cfName);
        }
#endif
        info.persistentId = persistentIdFromDevice(dev);
        if (info.persistentId.isEmpty())
            info.persistentId = info.displayName;

        if (!info.displayName.isEmpty())
            out.append(info);

        dev->Release();
        dev = nullptr;
    }
    it->Release();
    return out;
}

QList<DeckLinkCapture::Connection> DeckLinkCapture::supportedConnections(const QString& persistentId) {
    QList<Connection> out;
    IDeckLink* dev = findDeviceById(persistentId);
    if (!dev) return out;

    IDeckLinkProfileAttributes* attrs = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkProfileAttributes,
                            reinterpret_cast<void**>(&attrs)) == S_OK && attrs) {
        int64_t bits = 0;
        if (attrs->GetInt(BMDDeckLinkVideoInputConnections, &bits) == S_OK) {
            const BMDVideoConnection all[] = {
                bmdVideoConnectionSDI, bmdVideoConnectionHDMI,
                bmdVideoConnectionOpticalSDI, bmdVideoConnectionComponent,
                bmdVideoConnectionComposite, bmdVideoConnectionSVideo
            };
            for (BMDVideoConnection c : all) {
                if (bits & c) out.append(fromBMDConnection(c));
            }
        }
        attrs->Release();
    }
    dev->Release();
    return out;
}

QList<DeckLinkCapture::DisplayModeInfo> DeckLinkCapture::listDisplayModes(const QString& persistentId) {
    QList<DisplayModeInfo> out;
    IDeckLink* dev = findDeviceById(persistentId);
    if (!dev) return out;

    bool supportsFormatDetection = false;
    {
        IDeckLinkProfileAttributes* attrs = nullptr;
        if (dev->QueryInterface(IID_IDeckLinkProfileAttributes,
                                reinterpret_cast<void**>(&attrs)) == S_OK && attrs) {
#ifdef Q_OS_WIN
            BOOL sup = FALSE;
            if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &sup) == S_OK)
                supportsFormatDetection = (sup != FALSE);
#else
            bool sup = false;
            if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &sup) == S_OK)
                supportsFormatDetection = sup;
#endif
            attrs->Release();
        }
    }

    IDeckLinkInput* input = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(&input)) == S_OK && input) {
        if (supportsFormatDetection)
            out.append({QStringLiteral("Auto"), 0u});

        IDeckLinkDisplayModeIterator* modeIt = nullptr;
        if (input->GetDisplayModeIterator(&modeIt) == S_OK && modeIt) {
            IDeckLinkDisplayMode* dm = nullptr;
            while (modeIt->Next(&dm) == S_OK && dm) {
                DisplayModeInfo info;
                info.mode = static_cast<uint32_t>(dm->GetDisplayMode());
#ifdef Q_OS_WIN
                BSTR bstrName = nullptr;
                if (dm->GetName(&bstrName) == S_OK)
                    info.name = bstrToQString(bstrName);
#else
                CFStringRef cfName = nullptr;
                if (dm->GetName(&cfName) == S_OK && cfName) {
                    info.name = cfStringToQString(cfName);
                    CFRelease(cfName);
                }
#endif
                if (!info.name.isEmpty())
                    out.append(info);
                dm->Release();
                dm = nullptr;
            }
            modeIt->Release();
        }
        input->Release();
    }
    dev->Release();
    return out;
}

// ── start() / stop() ─────────────────────────────────────────────────────────

void DeckLinkCapture::start() {
    stop();

    const uint64_t gen = ++generation_;

    if (!deckLink_ || !converter_) {
        emit errorChanged(QStringLiteral("No DeckLink device selected"));
        return;
    }

    // Re-acquire input interface (following OBS: QueryInterface on each start).
    if (deckLink_->QueryInterface(IID_IDeckLinkInput,
                                  reinterpret_cast<void**>(&input_)) != S_OK || !input_) {
        input_ = nullptr;
        emit errorChanged(QStringLiteral("Selected device does not support input capture"));
        return;
    }

    // Switch physical input connector using the persistent config interface.
    const BMDVideoConnection bmdConn = toBMDConnection(connection_);
    if (bmdConn != bmdVideoConnectionUnspecified && config_) {
        const HRESULT cr = config_->SetInt(bmdDeckLinkConfigVideoInputConnection,
                                           static_cast<int64_t>(bmdConn));
        if (cr != S_OK) {
            input_->Release(); input_ = nullptr;
            emit errorChanged(
                QStringLiteral("Cannot set input connection (%1)").arg(hresultToString(cr)));
            return;
        }
    }

    currentPixelFormat_ = allow10Bit_ ? bmdFormat10BitYUV : bmdFormat8BitYUV;

    BMDDisplayMode     startMode  = static_cast<BMDDisplayMode>(displayMode_);
    BMDVideoInputFlags startFlags = bmdVideoInputFlagDefault;

    if (displayMode_ == 0) {
        bool supportsFormatDetection = false;
        {
            IDeckLinkProfileAttributes* attrs = nullptr;
            if (deckLink_->QueryInterface(IID_IDeckLinkProfileAttributes,
                                          reinterpret_cast<void**>(&attrs)) == S_OK && attrs) {
#ifdef Q_OS_WIN
                BOOL sup = FALSE;
                if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &sup) == S_OK)
                    supportsFormatDetection = (sup != FALSE);
#else
                bool sup = false;
                if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &sup) == S_OK)
                    supportsFormatDetection = sup;
#endif
                attrs->Release();
            }
        }
        // If we've seen this connection before, start with the known good mode so the
        // hardware delivers frames immediately — VideoInputFormatChanged only fires once
        // per device power-on, so NTSC→detect only works on the very first start.
        // Keep format detection enabled so genuine format changes are still handled.
        const int connKey = static_cast<int>(connection_);
        if (modeCache_.contains(connKey)) {
            const auto& cached = modeCache_[connKey];
            startMode           = static_cast<BMDDisplayMode>(cached.first);
            currentPixelFormat_ = static_cast<BMDPixelFormat>(cached.second);
        } else {
            startMode = bmdModeNTSC;
        }
        startFlags = supportsFormatDetection ? bmdVideoInputEnableFormatDetection
                                             : bmdVideoInputFlagDefault;
    }

    const HRESULT en = input_->EnableVideoInput(startMode, currentPixelFormat_, startFlags);
    if (en != S_OK) {
        input_->Release(); input_ = nullptr;
        emit errorChanged(
            QStringLiteral("EnableVideoInput failed (%1)").arg(hresultToString(en)));
        return;
    }

    callback_ = new InputCallback(this);
    input_->SetCallback(callback_);

    const HRESULT st = input_->StartStreams();
    if (st != S_OK) {
        input_->SetCallback(nullptr);
        callback_->Release(); callback_ = nullptr;
        input_->DisableVideoInput();
        input_->Release(); input_ = nullptr;
        emit errorChanged(
            QStringLiteral("StartStreams failed (%1)").arg(hresultToString(st)));
        return;
    }

    streaming_ = true;
    emit errorChanged(QStringLiteral("Waiting for signal…"));
}

void DeckLinkCapture::stop() {
    if (!streaming_) return;
    streaming_ = false;

    if (input_) {
        input_->StopStreams();
        input_->DisableVideoInput();
    }
    if (callback_) {
        if (input_) input_->SetCallback(nullptr);
        callback_->clearOwner();
        callback_->Release();
        callback_ = nullptr;
    }
    if (input_) {
        input_->Release();
        input_ = nullptr;
    }

    delete convertFrame_;
    convertFrame_ = nullptr;
}

#ifdef Q_OS_WIN

QString DeckLinkCapture::bstrToQString(BSTR bstr) {
    if (!bstr) return {};
    QString s = QString::fromWCharArray(bstr);
    SysFreeString(bstr);
    return s;
}

#else

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

#endif

#else  // !DECKLINK_AVAILABLE

DeckLinkCapture::DeckLinkCapture(QObject* parent) : QObject(parent) {}
DeckLinkCapture::~DeckLinkCapture() { stop(); }

void DeckLinkCapture::setDeviceId(const QString& id) { deviceId_ = id; }
QString DeckLinkCapture::deviceId() const { return deviceId_; }
void DeckLinkCapture::setConnection(Connection conn) { connection_ = conn; }
DeckLinkCapture::Connection DeckLinkCapture::connection() const { return connection_; }
void DeckLinkCapture::setAllow10Bit(bool allow) { allow10Bit_ = allow; }
bool DeckLinkCapture::allow10Bit() const { return allow10Bit_; }
void DeckLinkCapture::setDisplayMode(uint32_t mode) { displayMode_ = mode; }
uint32_t DeckLinkCapture::displayMode() const { return displayMode_; }

QString DeckLinkCapture::connectionName(Connection) { return QStringLiteral("Auto"); }

QList<DeckLinkCapture::DeviceInfo> DeckLinkCapture::listDeviceInfos(QString* error) {
    if (error) *error = QStringLiteral("DeckLink support not available on this platform");
    return {};
}
QList<DeckLinkCapture::Connection> DeckLinkCapture::supportedConnections(const QString&) { return {}; }
QList<DeckLinkCapture::DisplayModeInfo> DeckLinkCapture::listDisplayModes(const QString&) { return {}; }

void DeckLinkCapture::start() {
    emit errorChanged(QStringLiteral("DeckLink support not available on this platform"));
}
void DeckLinkCapture::stop() {}

#endif
