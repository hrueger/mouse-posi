#pragma once
#include <QWidget>
#include <QList>
#include <QMap>
#include <QString>
#include "../Project.h"

class QTableWidget;
class QLabel;
class QPushButton;

struct FixtureStatus {
    bool    active   = false;
    float   panDeg   = 0.f;
    float   tiltDeg  = 0.f;
    quint16 panDmx   = 0;   // full 16-bit: high byte = coarse, low byte = fine
    quint16 tiltDmx  = 0;
};

class FixturesPanel : public QWidget {
    Q_OBJECT
public:
    explicit FixturesPanel(QWidget* parent = nullptr);

    void setData(const QList<MvrImportData>& imports,
                 const QList<TrackerConfig>& trackers);

    void updateStatus(const QMap<QString, FixtureStatus>& statusByKey);

signals:
    void gdtfAssignRequested(int importIdx, int layerIdx, int objIdx);
    void dmxAddressChanged(int importIdx, int layerIdx, int objIdx, int universe, int address);

private:
    enum class DisplayMode { Physical, Dmx };

    struct Row {
        QString key;
        int importIdx, layerIdx, objIdx;
    };

    void rebuild();
    void applyStatus();
    void syncRowsFromTable();

    bool obj_linkedToTracker(const Row& r) const;
    bool obj_hasGdtf(const Row& r) const;

    QTableWidget*                table_;
    QLabel*                      emptyLabel_;
    QPushButton*                 btnPhysical_;
    QPushButton*                 btnDmx_;
    QPushButton*                 btnPatch_;
    QWidget*                     toggleBar_;

    QList<QLabel*>               statusLabels_;  // status mode only

    DisplayMode                  displayMode_ = DisplayMode::Physical;
    bool                         patchMode_   = false;
    bool                         rebuilding_  = false;
    QMap<QString, FixtureStatus> lastStatus_;

    QList<Row>                   rows_;
    QList<MvrImportData>         imports_;
    QList<TrackerConfig>         trackers_;
};
