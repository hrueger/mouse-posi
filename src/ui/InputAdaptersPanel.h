#pragma once
#include <QWidget>
#include <QList>
#include "../Project.h"
#include "../adapters/MidiInputAdapter.h"

class QListWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;

// Editable row widget for one MIDI InputAdapterMapping
class MappingRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit MappingRowWidget(const InputAdapterMapping& m, QWidget* parent = nullptr);
    InputAdapterMapping mapping() const;
signals:
    void changed();
    void removeRequested();
private:
    QComboBox*  targetCombo_;
    QSpinBox*   spin1_;      // CC number
    QSpinBox*   spin2_;      // MIDI channel
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
    QCheckBox*    enabledCheck_;
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
