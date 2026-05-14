#pragma once
#include <QWidget>
#include "Project.h"

class QListWidget;
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

signals:
    void activeTrackerChanged(int id, QColor color);
    void trackersChanged(QList<TrackerConfig> trackers);

private slots:
    void onAddTracker();
    void onRemoveTracker();
    void onEditTracker();

private:
    void rebuildList();
    void editTrackerAt(int row);

    QListWidget*         list_;
    QPushButton*         addBtn_;
    QPushButton*         removeBtn_;
    QPushButton*         editBtn_;
    QList<TrackerConfig> trackers_;
    int                  activeId_ = -1;
};
