#pragma once
#include <QDialog>
#include <QList>
#include <QMap>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTableWidget;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QStackedWidget;
class QSplitter;

class GdtfShareDialog : public QDialog {
    Q_OBJECT
public:
    explicit GdtfShareDialog(QWidget* parent = nullptr);

private slots:
    void onLogin();
    void onLoginFinished();
    void onFetchList();
    void onListFinished();
    void onDownload();
    void onManufacturerSelected();
    void onFixtureFilterChanged();

private:
    struct ShareEntry {
        int     rid       = 0;
        QString fixture;
        QString manufacturer;
        QString revision;
        float   rating    = 0.f;
        QString modes;
    };

    void showLogin();
    void showLibrary();
    void populateManufacturers();
    void populateFixtures(const QString& manufacturer);

    // Login page
    QLineEdit*   userEdit_;
    QLineEdit*   passEdit_;
    QCheckBox*   savePassCheck_;
    QPushButton* btnLogin_;
    QLabel*      loginStatus_;

    // Library page
    QListWidget*  mfrList_;
    QLineEdit*    mfrFilter_;
    QLineEdit*    fixtureFilter_;
    QLabel*       fixtureStatus_;
    QLabel*       loginUserLabel_;
    QTableWidget* fixtureTable_;
    QPushButton*  btnRefresh_;
    QPushButton*  btnDownload_;
    QPushButton*  btnLogout_;

    QStackedWidget* stack_;

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;

    QList<ShareEntry>           allEntries_;
    QMap<QString, QList<int>>   byManufacturer_;  // manufacturer → indices into allEntries_
    QString                     currentMfr_;

    bool loggedIn_   = false;
    bool listLoaded_ = false;
};
