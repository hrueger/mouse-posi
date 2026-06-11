#pragma once
#include <QWidget>
#include "Project.h"

class QButtonGroup;
class QFormLayout;
class QRadioButton;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;

class NetworkSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit NetworkSettingsPanel(QWidget* parent = nullptr);

    void setConfig(const NetworkConfig& cfg);
    NetworkConfig config() const;

signals:
    void configChanged(NetworkConfig cfg);

private:
    void populateInterfaces();
    void updateIpFieldVisibility();

    QButtonGroup*   psnModeGroup_;
    QFormLayout*    formLayout_;
    QRadioButton*   multicastRadio_;
    QRadioButton*   unicastRadio_;
    QRadioButton*   broadcastRadio_;
    QLineEdit*      multicastIpEdit_;
    QLineEdit*      unicastIpEdit_;
    QLineEdit*      broadcastIpEdit_;
    QSpinBox*       portSpin_;
    QComboBox*      psnIfaceCombo_;
    QComboBox*      sessionIfaceCombo_;
};
