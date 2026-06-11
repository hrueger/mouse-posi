#pragma once
#include <QDialog>
#include "../Project.h"

class QTabWidget;
class NetworkSettingsPanel;
class InputAdaptersPanel;
class ModeSelectionWidget;
class StreamSourcePanel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void setNetworkConfig(const NetworkConfig& cfg);
    void setInputAdapters(const QList<InputAdapterConfig>& adapters);
    void setOperatingMode(OperatingMode mode);
    void setStreamSourcePanel(StreamSourcePanel* panel); // takes ownership for display

    void showModeTab();
    void showNetworkTab();
    void showAdaptersTab();

signals:
    void networkConfigChanged(NetworkConfig cfg);
    void inputAdaptersChanged(QList<InputAdapterConfig> adapters);
    void operatingModeChanged(OperatingMode mode);

private:
    QTabWidget*           tabs_;
    ModeSelectionWidget*  modeWidget_;
    StreamSourcePanel*    streamPanel_ = nullptr;
    NetworkSettingsPanel* networkPanel_;
    InputAdaptersPanel*   adaptersPanel_;
    int                   streamTabIndex_ = -1;
};
