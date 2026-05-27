#pragma once

#include "Project.h"

#include <QMap>
#include <QString>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QStackedWidget;

class CameraSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit CameraSettingsPanel(QWidget* parent = nullptr) : QWidget(parent) {}
    ~CameraSettingsPanel() override = default;

    virtual QJsonObject configJson() const = 0;
    virtual void setConfigJson(const QJsonObject& config) = 0;
    virtual void setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress) {
        Q_UNUSED(sourceName)
        Q_UNUSED(ndiUrlAddress)
    }

signals:
    void configChanged();
};

class CameraControlPanel : public QWidget {
    Q_OBJECT
public:
    using CameraFactory = std::function<CameraSettingsPanel*(QWidget*)>;

    explicit CameraControlPanel(QWidget* parent = nullptr);

    void registerCamera(const QString& type, const QString& displayName, CameraFactory factory);

    CameraControlConfig config() const;
    void setConfig(const CameraControlConfig& config);
    void setNdiSourceEndpoint(const QString& sourceName, const QString& ndiUrlAddress);

signals:
    void configChanged(const CameraControlConfig& config);

private:
    void registerBuiltInCameras();
    void emitConfigChanged();
    void updateControls();
    void activateType(const QString& type);
    QJsonObject configForType(const QString& type) const;

    struct RegisteredCamera {
        QString displayName;
        CameraFactory factory;
        CameraSettingsPanel* panel = nullptr;
    };

    QCheckBox*      enabledCheck_ = nullptr;
    QComboBox*      typeCombo_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel*         emptyLabel_ = nullptr;

    QMap<QString, RegisteredCamera> cameras_;
    QString currentType_;
    QString currentNdiSource_;
    QString currentNdiEndpoint_;
    QJsonObject perTypeConfig_;
    bool setting_ = false;
};
