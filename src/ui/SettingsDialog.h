#pragma once
#include <QDialog>
#include "../Project.h"

class QTabWidget;
class NetworkSettingsPanel;
class InputAdaptersPanel;
class FixtureUniversesPanel;
class ModeSelectionWidget;
class StreamSourcePanel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void setNetworkConfig(const NetworkConfig& cfg);
    void setInputAdapters(const QList<InputAdapterConfig>& adapters);
    void setFixtureUniverseConfigs(const QList<FixtureUniverseConfig>& configs);
    void setOperatingMode(OperatingMode mode);
    void setStreamSourcePanel(StreamSourcePanel* panel); // takes ownership for display
    void setMvrData(const MvrSettings& mvr);

    void showModeTab();
    void showNetworkTab();
    void showAdaptersTab();
    void showDmxTab();

    InputAdaptersPanel* adaptersPanel() const { return adaptersPanel_; }

signals:
    void networkConfigChanged(NetworkConfig cfg);
    void inputAdaptersChanged(QList<InputAdapterConfig> adapters);
    void fixtureUniverseConfigsChanged(QList<FixtureUniverseConfig> configs);
    void operatingModeChanged(OperatingMode mode);

private:
    QTabWidget*            tabs_;
    ModeSelectionWidget*   modeWidget_;
    StreamSourcePanel*     streamPanel_ = nullptr;
    NetworkSettingsPanel*  networkPanel_;
    InputAdaptersPanel*    adaptersPanel_;
    FixtureUniversesPanel* fixtureUniversesPanel_;
    int                    streamTabIndex_ = -1;
};
