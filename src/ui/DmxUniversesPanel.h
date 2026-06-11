#pragma once
#include <QWidget>
#include <QList>
#include "../Project.h"

class QTableWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QStackedWidget;

// One editable row for a DmxChannelMapping
class DmxMappingRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit DmxMappingRowWidget(const DmxChannelMapping& m, QWidget* parent = nullptr);
    DmxChannelMapping mapping() const;
signals:
    void changed();
    void removeRequested();
private:
    QComboBox*      targetCombo_;
    QSpinBox*       channelSpin_;
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
};

class DmxUniversesPanel : public QWidget {
    Q_OBJECT
public:
    explicit DmxUniversesPanel(QWidget* parent = nullptr);

    void setUniverses(const QList<DmxUniverseEntry>& universes);
    QList<DmxUniverseEntry> universes() const { return inControlUniverses_ + universes_; }

signals:
    void dmxUniversesChanged(QList<DmxUniverseEntry> universes);

private slots:
    void onSelectionChanged();
    void onAddUniverse();
    void onRemoveUniverse();
    void onAddMapping();
    void onFieldChanged();
    void onRoleChanged(int idx);
    void onNetModeChanged(int idx);

private:
    void rebuildTable();
    void showDetail(int index);
    void collectCurrentDetail();
    void rebuildMappingRows();
    void rebuildMergeCombo();
    void emitChanged();
    void populateIfaceCombo();

    // Left pane
    QTableWidget* universeTable_;
    QPushButton*  addBtn_;
    QPushButton*  removeBtn_;

    // Right pane — detail
    QWidget*     detailWidget_;
    QLineEdit*   nameEdit_;
    QSpinBox*    numberSpin_;
    QComboBox*   roleCombo_;
    QComboBox*   protocolCombo_;
    QComboBox*   netModeCombo_;
    QComboBox*   ifaceCombo_;
    QWidget*     unicastRow_;
    QLineEdit*   unicastIpEdit_;

    // InControl-only section
    QWidget*      inControlSection_;
    QVBoxLayout*  mappingLayout_;
    QList<DmxMappingRowWidget*> mappingRows_;

    // OutFixtures-only section
    QWidget*   outFixturesSection_;
    QComboBox* mergeSourceCombo_;

    QList<DmxUniverseEntry> universes_;          // InFixtures + OutFixtures only (editable)
    QList<DmxUniverseEntry> inControlUniverses_; // preserved but not shown/edited here
    int  currentIndex_ = -1;
    bool updating_     = false;
};
