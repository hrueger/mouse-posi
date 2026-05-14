#pragma once
#include <QWidget>
#include <QMap>
#include <QStringList>
#include "Project.h"

class QTableWidget;
class QTableWidgetItem;
class QPushButton;

class TrackersPanel : public QWidget {
    Q_OBJECT
public:
    explicit TrackersPanel(QWidget* parent = nullptr);

    void setTrackers(const QList<TrackerConfig>& trackers);
    QList<TrackerConfig> trackers() const { return trackers_; }

    void setActiveTrackerId(int id);
    int  activeTrackerId() const { return activeId_; }
    QColor activeColor() const;

    void setSessionContext(bool isAdminOrHost,
                           const QStringList& peerNames,
                           const QMap<QString, QList<int>>& peerAssignments);

signals:
    void trackersChanged(QList<TrackerConfig> trackers);
    void trackerAccessChanged(QString peerName, QList<int> assignedTrackerIds);

private slots:
    void onAddTracker();
    void onRemoveTracker();
    void onCellDoubleClicked(int row, int col);
    void onItemChanged(QTableWidgetItem* item);

private:
    void rebuildTable();
    void updateRowStyle(int row);

    static constexpr int COL_ID    = 0;
    static constexpr int COL_NAME  = 1;
    static constexpr int COL_COLOR = 2;
    static constexpr int COL_PEERS = 3; // first peer column (Host, then per-peer)

    QTableWidget*             table_;
    QPushButton*              addBtn_;
    QPushButton*              removeBtn_;
    QList<TrackerConfig>      trackers_;
    bool                      isAdminOrHost_ = false;
    QStringList               peerNames_;
    QMap<QString, QList<int>> peerAssignments_;
    int                       activeId_      = -1;
    bool                      updatingTable_ = false;
};
