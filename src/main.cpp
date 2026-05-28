#include <QApplication>
#include <QSurfaceFormat>
#ifdef Q_OS_WIN
#  include <objbase.h>
#endif
#include <QIcon>
#ifdef HAVE_QT_DARWIN_CAMERA_PERMISSION_PLUGIN
#  include <QtPlugin>
#endif
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/style/ThemeManager.hpp>
#include "MainWindow.h"
#include "NdiReceiver.h"

#ifdef HAVE_QT_DARWIN_CAMERA_PERMISSION_PLUGIN
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin)
#endif

int main(int argc, char* argv[]) {
    // Must be set before QApplication to ensure all internal GL contexts
    // (including shared ones) use the same version — required on macOS.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("onpoint");
    app.setOrganizationName("onpoint");
#ifndef Q_OS_MACOS
    app.setWindowIcon(QIcon(":/assets/logo.png"));
#endif

    auto* style = new oclero::qlementine::QlementineStyle;
    QApplication::setStyle(style);

    auto* themeManager = new oclero::qlementine::ThemeManager(style, &app);
    themeManager->loadDirectory(":/themes");

    auto* ndi = new NdiReceiver;

    MainWindow w(ndi, themeManager);
    w.show();
    w.raise();
    w.activateWindow();
    return app.exec();
}
