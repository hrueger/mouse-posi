#pragma once
#include <QWidget>
#include <QList>
#include <QMap>
#include <QByteArray>
#include "../Project.h"

class QComboBox;
class QLabel;
class QTableWidget;
class QTimer;
class QStackedWidget;
class DmxGridWidget;

class DmxMonitorPanel : public QWidget {
    Q_OBJECT
public:
    explicit DmxMonitorPanel(QWidget* parent = nullptr);

    void setUniverses(const QList<DmxUniverseEntry>& universes);
    void setMvrImports(const QList<MvrImportData>& imports);

public slots:
    void updateOutFrame(quint16 universe, const QByteArray& frame);
    void updateInFrame(quint16 universe, const QByteArray& frame);

private slots:
    void onUniverseSelected(int index);
    void refreshValues();

public:
    struct ChannelInfo {
        QString fixtureName;
        QString function;
        bool    isComputed      = false;  // pan/tilt channels overridden by OnPoint
        int     fixtureBaseAddr = -1;     // 0-based start address of this fixture instance (unique per universe)
    };

private:
    void rebuildCombo();
    QMap<int, ChannelInfo> buildPatchMap(quint16 universe) const;

    QComboBox*    universeCombo_;
    QLabel*       directionBadge_;
    QStackedWidget* stack_;
    QTableWidget* table_;
    DmxGridWidget* gridWidget_;
    QTimer*       refreshTimer_;

    QList<DmxUniverseEntry> universes_;
    QList<MvrImportData>    mvrImports_;
    QMap<quint16, QByteArray> outFrames_;
    QMap<quint16, QByteArray> inFrames_;
};
