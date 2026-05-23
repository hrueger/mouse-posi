#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QWidget>

class Project;
class QLabel;
class QCheckBox;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;

struct MarshallCv370Config {
    bool    enabled = false;
    QString host;
    bool    nightMode = false;

    static MarshallCv370Config fromProject(const Project& project);
    void writeToProject(Project& project) const;
};

class MarshallCv370Controller : public QObject {
    Q_OBJECT
public:
    explicit MarshallCv370Controller(QObject* parent = nullptr);

    static QString hostFromNdiUrlAddress(const QString& ndiUrlAddress);
    static QUrl buildDetectUrl(const QString& hostOrNdiUrlAddress);
    static QUrl buildSetIrCutUrl(const QString& host, bool nightMode);
    static bool responseLooksLikeCv370(const QByteArray& body);

    void detectHost(const QString& hostOrNdiUrlAddress);
    void setNightMode(const QString& host, bool nightMode);

signals:
    void detectionFinished(bool detected, const QString& host, const QString& message);
    void requestFinished(bool ok, bool nightMode, const QString& message);

private:
    QNetworkAccessManager* network_ = nullptr;
};

class MarshallCv370Panel : public QWidget {
    Q_OBJECT
public:
    explicit MarshallCv370Panel(QWidget* parent = nullptr);

    MarshallCv370Config config() const;
    void setConfig(const MarshallCv370Config& config);
    void setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress);

signals:
    void configChanged(const MarshallCv370Config& config);

private:
    void emitConfigChanged();
    void updateControls();

    QCheckBox*   cameraCheck_ = nullptr;
    QLabel*      hostLabel_ = nullptr;
    QLineEdit*   hostEdit_ = nullptr;
    QPushButton* toggleBtn_ = nullptr;
    QLabel*      statusLabel_ = nullptr;

    MarshallCv370Controller* controller_ = nullptr;
    QString      currentNdiSource_;
    QString      lastDetectedHost_;
    bool         nightMode_ = false;
    bool         setting_ = false;
    bool         userEditedHost_ = false;
};
