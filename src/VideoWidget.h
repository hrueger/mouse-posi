#pragma once
#include <QWidget>
#include <QImage>
#include <QMap>
#include <QVector3D>

struct TrackerConfig;
class Calibration;

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);

    void setCalibration(Calibration* cal) { calibration_ = cal; }

    QPointF mapFrameToWidget(QPointF fpt) const { return frameToWidget(fpt); }
    QSize   frameSize()      const { return frame_.size(); }

public slots:
    void setFrame(const QImage& frame);
    void setNdiSourceConfigured(bool configured);
    void setRemotePositions(const QMap<int, QVector3D>& positions,
                            const QList<TrackerConfig>& trackers);
    void setOwnPositions(const QMap<int, QPair<float,float>>& positions,
                         const QList<TrackerConfig>& trackers);
    void setActiveTracker(int id, const QColor& color);
    void setCalibrationMode(bool on);
    void setShowCalibrationOverlay(bool on);
    // Session: which trackers the local user is assigned (empty = all at full opacity)
    void setAssignedTrackers(const QList<int>& ids, int unassignedAlpha = 80);

    // Non-origin calibration point overlay.
    // highlighted: -2 = origin selected, -1 = none, >=0 = point index.
    void setCalibrationOverlay(const QList<QPointF>& imagePoints, int highlighted = -1);

    void setCalibOriginPoint(QPointF pt);
    void clearCalibOriginPoint();

    // Labels shown along the origin→point lines (one per non-origin point).
    void setCalibDistanceLabels(const QList<QString>& labels);

    // Arbitrary lines drawn on the overlay (frame-space endpoints).
    // When non-empty, the automatic origin→point lines are suppressed.
    void setCalibExplicitLines(const QList<QPair<QPointF,QPointF>>& lines);

signals:
    void fullscreenRequested();
    // Emitted on mouse/touch *release* in calibration mode (after optional drag to adjust).
    void calibPointClicked(QPointF imagePos);
    void mousePosInFrame(QPointF imagePos);
    void mouseLeftPressed(QPointF imagePos);
    // Emitted live while dragging an existing calib point (index -1 = origin, >=0 = point).
    void existingCalibPointMoved(int index, QPointF imagePos);
    // Emitted on release after clicking/dragging an existing point.
    void existingCalibPointClicked(int index, QPoint widgetPos);

protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QPointF widgetToFrame(QPointF wpt) const;
    QPointF frameToWidget(QPointF fpt) const;
    void    updateScaling();
    void    rebuildScaledFrame();
    void    drawTrackerDot(QPainter& p, QPointF wpt, int id, const QColor& color,
                           bool isActive) const;
    void    drawCalibOverlay(QPainter& p) const;
    void    drawAxisLegend(QPainter& p) const;
    // Returns -1 (origin), >=0 (point index), or -2 (nothing nearby).
    int     nearestCalibPoint(QPointF widgetPos, double threshold = 15.0) const;

    QImage  frame_;
    QPixmap scaledFrame_;

    QMap<int, QVector3D>           remotePositions_;
    QList<TrackerConfig>           remoteTrackers_;
    QMap<int, QPair<float,float>>  ownPositions_;
    QList<TrackerConfig>           ownTrackers_;

    int     activeTrackerId_    = -1;
    QColor  activeColor_        = Qt::white;
    bool    calibMode_          = false;
    bool    showCalibOverlay_   = false;
    QList<int> assignedTrackers_;    // empty = all fully opaque
    int     unassignedAlpha_    = 80; // 0-255
    bool    calibDragging_   = false;   // press-and-hold active in calib mode
    int     draggingCalibIdx_ = -2;     // -2=none, -1=origin, >=0=point being dragged
    bool    mouseHeld_       = false;
    QPointF mouseWidgetPos_;

    QSizeF  scale_  = {1.0, 1.0};
    QPointF offset_ = {0.0, 0.0};

    bool    ndiSourceConfigured_ = false;
    Calibration* calibration_ = nullptr;

    QList<QPointF> calibOverlayPoints_;
    int            calibOverlayHighlight_ = -1;

    bool           hasCalibOrigin_      = false;
    QPointF        calibOriginPoint_;
    QList<QString> calibDistanceLabels_;
    QList<QPair<QPointF,QPointF>> calibExplicitLines_;


public:
    bool    mouseHeld()     const { return mouseHeld_; }
    QPointF mouseFramePos() const { return widgetToFrame(mouseWidgetPos_); }
};
