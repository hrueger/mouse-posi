#pragma once
#include <QWidget>
#include <QList>
#include "Project.h"

class QPushButton;
class QHBoxLayout;

class TrackerBar : public QWidget {
    Q_OBJECT
public:
    explicit TrackerBar(QWidget* parent = nullptr);

    void setTrackers(const QList<TrackerConfig>& trackers);
    void setActiveTrackerId(int id);
    void clearRestriction();
    void setAllowedTrackers(const QList<int>& ids);
    void setCalibrationActive(bool on);

signals:
    void trackerSelected(int id);
    void fullscreenClicked();

private:
    void rebuild();

    QHBoxLayout*         layout_;
    QPushButton*         fullscreenBtn_;
    QList<TrackerConfig> trackers_;
    bool                 restricted_   = false;
    bool                 calibActive_  = false;
    QList<int>           allowedIds_;
    int                  activeId_ = -1;
    QList<QPushButton*>  buttons_;
};
