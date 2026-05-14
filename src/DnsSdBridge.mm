#import "DnsSdBridge.h"
#import <Foundation/Foundation.h>
#import <dns_sd.h>

// ── Objective-C implementation class ─────────────────────────────────────────

@interface DnsSdImpl : NSObject <NSNetServiceDelegate, NSNetServiceBrowserDelegate>
@property (nonatomic, assign) DnsSdBridge* bridge;
@property (nonatomic, strong) NSNetService* service;
@property (nonatomic, strong) NSNetServiceBrowser* browser;
@property (nonatomic, strong) NSMutableArray<NSNetService*>* resolving;
@end

@implementation DnsSdImpl

- (void)netServiceDidPublish:(NSNetService*)sender {
    Q_UNUSED(sender)
}

- (void)netService:(NSNetService*)sender didNotPublish:(NSDictionary*)error {
    Q_UNUSED(sender)
    NSNumber* code = error[NSNetServicesErrorCode];
    NSString* desc = code ? [NSString stringWithFormat:@"DNS-SD error %@", code] : @"Unknown DNS-SD error";
    emit self.bridge->advertiseError(QString::fromNSString(desc));
}

- (instancetype)init {
    if ((self = [super init]))
        self.resolving = [NSMutableArray array];
    return self;
}

- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser*)browser {
    Q_UNUSED(browser)
}

- (void)netServiceBrowserDidStopSearch:(NSNetServiceBrowser*)browser {
    Q_UNUSED(browser)
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)browser
         didNotSearch:(NSDictionary*)errorDict
{
    Q_UNUSED(browser) Q_UNUSED(errorDict)
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)browser
           didFindService:(NSNetService*)service
               moreComing:(BOOL)moreComing
{
    Q_UNUSED(browser) Q_UNUSED(moreComing)
    service.delegate = self;
    [service scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [self.resolving addObject:service];
    [service resolveWithTimeout:5.0];
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)browser
         didRemoveService:(NSNetService*)service
               moreComing:(BOOL)moreComing
{
    Q_UNUSED(browser) Q_UNUSED(moreComing)
    emit self.bridge->serviceLost(QString::fromNSString(service.name));
}

- (void)netServiceDidResolveAddress:(NSNetService*)service {
    [self.resolving removeObject:service];
    QString name = QString::fromNSString(service.name);
    QString host = QString::fromNSString(service.hostName);
    quint16 port = static_cast<quint16>(service.port);
    emit self.bridge->serviceFound(name, host, port);
}

- (void)netService:(NSNetService*)service didNotResolve:(NSDictionary*)error {
    Q_UNUSED(error)
    [self.resolving removeObject:service];
}

@end

// ── C++ wrapper ───────────────────────────────────────────────────────────────

DnsSdBridge::DnsSdBridge(QObject* parent) : QObject(parent) {
    DnsSdImpl* impl = [[DnsSdImpl alloc] init];
    impl.bridge = this;
    impl_ = (__bridge_retained void*)impl;
}

DnsSdBridge::~DnsSdBridge() {
    stopAdvertising();
    stopBrowsing();
    DnsSdImpl* impl = (__bridge_transfer DnsSdImpl*)impl_;
    Q_UNUSED(impl)
}

void DnsSdBridge::advertise(const QString& sessionName, quint16 port) {
    DnsSdImpl* impl = (__bridge DnsSdImpl*)impl_;
    stopAdvertising();
    NSString* name = sessionName.toNSString();
    impl.service = [[NSNetService alloc] initWithDomain:@""
                                                   type:@"_mouseposi._tcp."
                                                   name:name
                                                   port:(int)port];
    impl.service.delegate = impl;
    [impl.service publish];
}

void DnsSdBridge::stopAdvertising() {
    DnsSdImpl* impl = (__bridge DnsSdImpl*)impl_;
    [impl.service stop];
    impl.service = nil;
}

void DnsSdBridge::browse() {
    DnsSdImpl* impl = (__bridge DnsSdImpl*)impl_;
    stopBrowsing();
    impl.browser = [[NSNetServiceBrowser alloc] init];
    impl.browser.delegate = impl;
    [impl.browser scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [impl.browser searchForServicesOfType:@"_mouseposi._tcp." inDomain:@""];
}

void DnsSdBridge::stopBrowsing() {
    DnsSdImpl* impl = (__bridge DnsSdImpl*)impl_;
    [impl.browser stop];
    impl.browser = nil;
}
