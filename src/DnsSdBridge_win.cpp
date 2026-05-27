#include "DnsSdBridge.h"
#include <QTimer>

#if defined(DNSSD_AVAILABLE) && DNSSD_AVAILABLE

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#endif
#include <dns_sd.h>
#include <QSocketNotifier>
#include <vector>

// ── impl struct ───────────────────────────────────────────────────────────────

struct DnsSdWinImpl {
    DnsSdBridge* q = nullptr;

    DNSServiceRef advertiseRef = nullptr;
    QSocketNotifier* advertiseNotifier = nullptr;

    DNSServiceRef browseRef = nullptr;
    QSocketNotifier* browseNotifier = nullptr;

    struct ResolveEntry {
        QString name;
        DNSServiceRef ref = nullptr;
        QSocketNotifier* notifier = nullptr;
    };
    std::vector<ResolveEntry> resolving;

    static void DNSSD_API onAdvertiseReply(
        DNSServiceRef, DNSServiceFlags, DNSServiceErrorType errorCode,
        const char*, const char*, const char*, void* ctx)
    {
        auto* d = static_cast<DnsSdWinImpl*>(ctx);
        if (errorCode != kDNSServiceErr_NoError)
            emit d->q->advertiseError(
                QStringLiteral("DNS-SD register error %1").arg(errorCode));
    }

    static void DNSSD_API onBrowseReply(
        DNSServiceRef, DNSServiceFlags flags, uint32_t ifaceIndex,
        DNSServiceErrorType errorCode, const char* serviceName,
        const char* regtype, const char* domain, void* ctx)
    {
        auto* d = static_cast<DnsSdWinImpl*>(ctx);
        if (errorCode != kDNSServiceErr_NoError)
            return;

        QString name = QString::fromUtf8(serviceName);

        if (flags & kDNSServiceFlagsAdd) {
            DNSServiceRef resolveRef = nullptr;
            if (DNSServiceResolve(&resolveRef, 0, ifaceIndex,
                                  serviceName, regtype, domain,
                                  onResolveReply, ctx) != kDNSServiceErr_NoError)
                return;

            qintptr fd = static_cast<qintptr>(DNSServiceRefSockFD(resolveRef));
            auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read);
            QObject::connect(notifier, &QSocketNotifier::activated,
                [d, resolveRef](QSocketDescriptor, QSocketNotifier::Type) {
                    DNSServiceProcessResult(resolveRef);
                });
            d->resolving.push_back({name, resolveRef, notifier});
        } else {
            emit d->q->serviceLost(name);
        }
    }

    static void DNSSD_API onResolveReply(
        DNSServiceRef sdRef, DNSServiceFlags, uint32_t,
        DNSServiceErrorType errorCode, const char*,
        const char* hosttarget, uint16_t port,
        uint16_t, const unsigned char*, void* ctx)
    {
        auto* d = static_cast<DnsSdWinImpl*>(ctx);

        for (auto it = d->resolving.begin(); it != d->resolving.end(); ++it) {
            if (it->ref != sdRef)
                continue;

            if (errorCode == kDNSServiceErr_NoError)
                emit d->q->serviceFound(it->name,
                                        QString::fromUtf8(hosttarget),
                                        ntohs(port));

            // DNSServiceRefDeallocate must not be called from within its own callback;
            // disable the notifier and defer cleanup to the next event-loop tick.
            it->notifier->setEnabled(false);
            QSocketNotifier* notifier = it->notifier;
            DNSServiceRef ref = it->ref;
            d->resolving.erase(it);
            QTimer::singleShot(0, [ref, notifier]() {
                delete notifier;
                DNSServiceRefDeallocate(ref);
            });
            return;
        }
    }
};

// ── DnsSdBridge ───────────────────────────────────────────────────────────────

DnsSdBridge::DnsSdBridge(QObject* parent) : QObject(parent) {
    auto* impl = new DnsSdWinImpl;
    impl->q = this;
    impl_ = impl;
}

DnsSdBridge::~DnsSdBridge() {
    stopAdvertising();
    stopBrowsing();
    delete static_cast<DnsSdWinImpl*>(impl_);
}

void DnsSdBridge::advertise(const QString& sessionName, quint16 port) {
    auto* impl = static_cast<DnsSdWinImpl*>(impl_);
    stopAdvertising();

    QByteArray nameUtf8 = sessionName.toUtf8();
    DNSServiceErrorType err = DNSServiceRegister(
        &impl->advertiseRef,
        0, 0,
        nameUtf8.constData(),
        "_onpoint._tcp.",
        nullptr, nullptr,
        htons(port),
        0, nullptr,
        DnsSdWinImpl::onAdvertiseReply,
        impl);

    if (err != kDNSServiceErr_NoError) {
        impl->advertiseRef = nullptr;
        emit advertiseError(
            QStringLiteral("DNS-SD register failed: error %1").arg(err));
        return;
    }

    qintptr fd = static_cast<qintptr>(DNSServiceRefSockFD(impl->advertiseRef));
    impl->advertiseNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(impl->advertiseNotifier, &QSocketNotifier::activated,
        [impl](QSocketDescriptor, QSocketNotifier::Type) {
            DNSServiceProcessResult(impl->advertiseRef);
        });
}

void DnsSdBridge::stopAdvertising() {
    auto* impl = static_cast<DnsSdWinImpl*>(impl_);
    delete impl->advertiseNotifier;
    impl->advertiseNotifier = nullptr;
    if (impl->advertiseRef) {
        DNSServiceRefDeallocate(impl->advertiseRef);
        impl->advertiseRef = nullptr;
    }
}

void DnsSdBridge::browse() {
    auto* impl = static_cast<DnsSdWinImpl*>(impl_);
    stopBrowsing();

    DNSServiceErrorType err = DNSServiceBrowse(
        &impl->browseRef,
        0, 0,
        "_onpoint._tcp.",
        nullptr,
        DnsSdWinImpl::onBrowseReply,
        impl);

    if (err != kDNSServiceErr_NoError) {
        impl->browseRef = nullptr;
        return;
    }

    qintptr fd = static_cast<qintptr>(DNSServiceRefSockFD(impl->browseRef));
    impl->browseNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(impl->browseNotifier, &QSocketNotifier::activated,
        [impl](QSocketDescriptor, QSocketNotifier::Type) {
            DNSServiceProcessResult(impl->browseRef);
        });
}

void DnsSdBridge::stopBrowsing() {
    auto* impl = static_cast<DnsSdWinImpl*>(impl_);
    delete impl->browseNotifier;
    impl->browseNotifier = nullptr;
    if (impl->browseRef) {
        DNSServiceRefDeallocate(impl->browseRef);
        impl->browseRef = nullptr;
    }
    for (auto& entry : impl->resolving) {
        delete entry.notifier;
        DNSServiceRefDeallocate(entry.ref);
    }
    impl->resolving.clear();
}

#else // Bonjour SDK not found — stub implementation

DnsSdBridge::DnsSdBridge(QObject* parent) : QObject(parent) {}
DnsSdBridge::~DnsSdBridge() {}

void DnsSdBridge::advertise(const QString&, quint16) {
    QTimer::singleShot(0, this, [this]() {
        emit advertiseError(
            QStringLiteral("Session discovery unavailable: install a DNS-SD compatibility library "
                           "(Bonjour on Windows or Avahi libdns_sd on Linux)"));
    });
}
void DnsSdBridge::stopAdvertising() {}
void DnsSdBridge::browse() {}
void DnsSdBridge::stopBrowsing() {}

#endif // DNSSD_AVAILABLE
