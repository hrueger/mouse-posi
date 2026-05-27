#pragma once

#include "CameraControl.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QLabel;
class QCheckBox;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

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

class MarshallCv370Panel : public CameraSettingsPanel {
    Q_OBJECT
public:
    explicit MarshallCv370Panel(QWidget* parent = nullptr);

    QJsonObject configJson() const override;
    void setConfigJson(const QJsonObject& config) override;
    void setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress) override;

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

void registerMarshallCv370Camera(CameraControlPanel* cameraControl);
