#include <QApplication>
#ifdef Q_OS_WIN
#  include <objbase.h>
#endif
#include <QStyleHints>
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
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("mouse-posi");
    app.setOrganizationName("mouse-posi");

    auto* style = new oclero::qlementine::QlementineStyle;
    QApplication::setStyle(style);

    auto* themeManager = new oclero::qlementine::ThemeManager(style, &app);
    themeManager->loadDirectory(":/themes");

    const auto applySystemTheme = [themeManager]() {
        const bool isDark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        themeManager->setCurrentTheme(isDark ? "Dark" : "Light");
    };
    applySystemTheme();
    QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
                     themeManager, [applySystemTheme](Qt::ColorScheme) { applySystemTheme(); });

    auto* ndi = new NdiReceiver;

    MainWindow w(ndi);
    w.show();
    w.raise();
    w.activateWindow();
    return app.exec();
}
