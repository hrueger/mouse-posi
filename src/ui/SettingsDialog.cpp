#include "SettingsDialog.h"
#include "NetworkSettingsPanel.h"
#include "InputAdaptersPanel.h"
#include "DmxUniversesPanel.h"
#include "ModeSelectionWidget.h"
#include "StreamSourcePanel.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QSettings>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Project Settings");
    setMinimumSize(680, 560);
    resize(760, 600);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QSettings s("onpoint", "onpoint");
    if (s.contains("settingsGeometry"))
        restoreGeometry(s.value("settingsGeometry").toByteArray());

    auto* lay = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);

    // Mode tab (scrollable so all 3 cards fit on small screens)
    modeWidget_ = new ModeSelectionWidget;
    auto* modeScroll = new QScrollArea;
    modeScroll->setFrameShape(QFrame::NoFrame);
    modeScroll->setWidgetResizable(true);
    modeScroll->setWidget(modeWidget_);
    tabs_->addTab(modeScroll, "Operating Mode");

    networkPanel_      = new NetworkSettingsPanel;
    adaptersPanel_     = new InputAdaptersPanel;
    dmxUniversesPanel_ = new DmxUniversesPanel;
    tabs_->addTab(networkPanel_,       "Network");
    tabs_->addTab(adaptersPanel_,      "Input Adapters");
    tabs_->addTab(dmxUniversesPanel_,  "DMX Universes");

    lay->addWidget(tabs_, 1);

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Close);
    lay->addWidget(bbox);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::hide);

    connect(modeWidget_, &ModeSelectionWidget::modeChanged,
            this, &SettingsDialog::operatingModeChanged);
    connect(networkPanel_, &NetworkSettingsPanel::configChanged,
            this, &SettingsDialog::networkConfigChanged);
    connect(adaptersPanel_, &InputAdaptersPanel::adaptersChanged,
            this, &SettingsDialog::inputAdaptersChanged);
    connect(dmxUniversesPanel_, &DmxUniversesPanel::dmxUniversesChanged,
            this, &SettingsDialog::dmxUniversesChanged);
}

void SettingsDialog::setNetworkConfig(const NetworkConfig& cfg) {
    networkPanel_->setConfig(cfg);
}

void SettingsDialog::setInputAdapters(const QList<InputAdapterConfig>& adapters) {
    adaptersPanel_->setAdapters(adapters);
}

void SettingsDialog::setDmxUniverses(const QList<DmxUniverseEntry>& universes) {
    dmxUniversesPanel_->setUniverses(universes);
}

void SettingsDialog::setOperatingMode(OperatingMode mode) {
    modeWidget_->setMode(mode);
}

void SettingsDialog::setStreamSourcePanel(StreamSourcePanel* panel) {
    if (streamPanel_ == panel) return;
    // Remove old tab if present
    if (streamPanel_ && streamTabIndex_ >= 0) {
        tabs_->removeTab(streamTabIndex_);
        streamTabIndex_ = -1;
    }
    streamPanel_ = panel;
    if (panel) {
        // Insert as second tab (after Mode)
        streamTabIndex_ = tabs_->insertTab(1, panel, "Video Source");
    }
}

void SettingsDialog::showModeTab() {
    tabs_->setCurrentIndex(0);
    show(); raise(); activateWindow();
}

void SettingsDialog::showNetworkTab() {
    // Network tab is after Mode and optionally Video Source
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->tabText(i) == QLatin1String("Network")) {
            tabs_->setCurrentIndex(i);
            break;
        }
    }
    show(); raise(); activateWindow();
}

void SettingsDialog::showAdaptersTab() {
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->tabText(i) == QLatin1String("Input Adapters")) {
            tabs_->setCurrentIndex(i);
            break;
        }
    }
    show(); raise(); activateWindow();
}

void SettingsDialog::showDmxTab() {
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->tabText(i) == QLatin1String("DMX Universes")) {
            tabs_->setCurrentIndex(i);
            break;
        }
    }
    show(); raise(); activateWindow();
}
