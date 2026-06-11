#pragma once
#include <QWidget>
#include <QList>
#include "../Project.h"
#include "../adapters/MidiInputAdapter.h"

class QListWidget;
class QStackedWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;

// Editable row widget for one InputAdapterMapping
class MappingRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit MappingRowWidget(const InputAdapterMapping& m, bool isMidi, QWidget* parent = nullptr);
    InputAdapterMapping mapping() const;
signals:
    void changed();
    void removeRequested();
private:
    void buildDmxRow(const InputAdapterMapping& m, QHBoxLayout* lay);
    void buildMidiRow(const InputAdapterMapping& m, QHBoxLayout* lay);
    bool        isMidi_;
    QComboBox*  targetCombo_;
    QSpinBox*   spin1_;      // universe (DMX) or CC number (MIDI)
    QSpinBox*   spin2_;      // channel (DMX) or MIDI channel
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
};

class InputAdaptersPanel : public QWidget {
    Q_OBJECT
public:
    explicit InputAdaptersPanel(QWidget* parent = nullptr);

    void setAdapters(const QList<InputAdapterConfig>& adapters);
    QList<InputAdapterConfig> adapters() const { return adapters_; }

signals:
    void adaptersChanged(QList<InputAdapterConfig> adapters);

private slots:
    void onAdapterSelectionChanged();
    void onAddAdapter();
    void onRemoveAdapter();
    void onAddMapping();
    void onAdapterFieldChanged();
    void onTypeChanged(int idx);

private:
    void rebuildAdapterList();
    void showAdapterDetail(int index);
    void collectCurrentDetail();
    void rebuildMappingRows();
    void emitChanged();

    // Left panel
    QListWidget*  adapterList_;
    QPushButton*  addAdapterBtn_;
    QPushButton*  removeAdapterBtn_;

    // Right panel — adapter detail
    QWidget*      detailWidget_;
    QComboBox*    typeCombo_;
    QCheckBox*    enabledCheck_;
    // DMX fields
    QWidget*      dmxFields_;
    QComboBox*    protocolCombo_;
    QComboBox*    netModeCombo_;
    QComboBox*    ifaceCombo_;
    QWidget*      unicastRow_;
    QWidget*      unicastIpEdit_;  // actually QLineEdit, forward-declared as QWidget
    // MIDI fields
    QWidget*      midiFields_;
    QComboBox*    midiPortCombo_;
    // Mapping list
    QScrollArea*  mappingScroll_;
    QWidget*      mappingContainer_;
    QVBoxLayout*  mappingLayout_;
    QPushButton*  addMappingBtn_;

    QList<InputAdapterConfig>   adapters_;
    QList<MappingRowWidget*>    mappingRows_;
    int                         currentIndex_ = -1;
    bool                        updating_     = false;
};
