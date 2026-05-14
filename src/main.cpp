#include <QApplication>
#include "MainWindow.h"
#include "NdiReceiver.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("mouse-posi");
    app.setOrganizationName("mouse-posi");

    auto* ndi = new NdiReceiver;

    MainWindow w(ndi);
    w.show();
    w.raise();
    w.activateWindow();
    return app.exec();
}
