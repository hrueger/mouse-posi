#include "VideoWidget.h"
#include "Calibration.h"
#include "Project.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QFont>
#include <QFontMetrics>
#include <cmath>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 180);
    setFocusPolicy(Qt::NoFocus);

}

void VideoWidget::setFrame(const QImage& frame) {
    frame_ = frame;
    updateScaling();
    rebuildScaledFrame();
    update();
}

void VideoWidget::setRemotePositions(const QMap<int, QVector3D>& positions,
                                      const QList<TrackerConfig>& trackers) {
    remotePositions_ = positions;
    remoteTrackers_  = trackers;
}

void VideoWidget::setOwnPositions(const QMap<int, QPair<float,float>>& positions,
                                   const QList<TrackerConfig>& trackers) {
    ownPositions_ = positions;
    ownTrackers_  = trackers;
}

void VideoWidget::setOwnRawPositions(const QMap<int, QPair<float,float>>& rawPositions) {
    ownRawPositions_ = rawPositions;
}

void VideoWidget::setActiveTracker(int id, const QColor& color) {
    activeTrackerId_ = id;
    activeColor_     = color;
    update();
}

void VideoWidget::setNdiSourceConfigured(bool configured) {
    ndiSourceConfigured_ = configured;
    if (scaledFrame_.isNull()) update();
}

void VideoWidget::setCalibrationMode(bool on) {
    calibMode_     = on;
    calibDragging_ = false;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void VideoWidget::setShowCalibrationOverlay(bool on) {
    showCalibOverlay_ = on;
    update();
}

void VideoWidget::setAssignedTrackers(const QList<int>& ids, int unassignedAlpha) {
    assignedTrackers_  = ids;
    unassignedAlpha_   = qBound(0, unassignedAlpha, 255);
    update();
}

void VideoWidget::setCalibrationOverlay(const QList<QPointF>& imagePoints, int highlighted) {
    calibOverlayPoints_    = imagePoints;
    calibOverlayHighlight_ = highlighted;
    update();
}

void VideoWidget::setCalibOriginPoint(QPointF pt) {
    hasCalibOrigin_   = true;
    calibOriginPoint_ = pt;
    update();
}

void VideoWidget::clearCalibOriginPoint() {
    hasCalibOrigin_ = false;
    update();
}

void VideoWidget::setCalibDistanceLabels(const QList<QString>& labels) {
    calibDistanceLabels_ = labels;
    update();
}

void VideoWidget::setShowFloorGrid(bool on) {
    showFloorGrid_ = on;
    update();
}

void VideoWidget::setClickPlaneHeight(float h) {
    clickPlaneHeight_ = h;
    update();
}

void VideoWidget::setShowClickPlane(bool on) {
    showClickPlane_ = on;
    update();
}

void VideoWidget::setCalibExplicitLines(const QList<QPair<QPointF,QPointF>>& lines) {
    calibExplicitLines_ = lines;
    update();
}

void VideoWidget::updateScaling() {
    if (frame_.isNull()) return;
    double scaleX = double(width())  / frame_.width();
    double scaleY = double(height()) / frame_.height();
    double s = std::min(scaleX, scaleY);
    scale_  = {s, s};
    offset_ = {(width()  - frame_.width()  * s) / 2.0,
               (height() - frame_.height() * s) / 2.0};
}

void VideoWidget::rebuildScaledFrame() {
    if (frame_.isNull() || width() <= 0 || height() <= 0) { scaledFrame_ = {}; return; }
    int w = int(frame_.width()  * scale_.width());
    int h = int(frame_.height() * scale_.height());
    if (w <= 0 || h <= 0) { scaledFrame_ = {}; return; }
    scaledFrame_ = QPixmap::fromImage(frame_).scaled(w, h, Qt::IgnoreAspectRatio,
                                                      Qt::FastTransformation);
}

void VideoWidget::resizeEvent(QResizeEvent*) {
    updateScaling();
    rebuildScaledFrame();

}

QPointF VideoWidget::widgetToFrame(QPointF wpt) const {
    return {(wpt.x() - offset_.x()) / scale_.width(),
            (wpt.y() - offset_.y()) / scale_.height()};
}

QPointF VideoWidget::frameToWidget(QPointF fpt) const {
    return {fpt.x() * scale_.width()  + offset_.x(),
            fpt.y() * scale_.height() + offset_.y()};
}

// ── Drawing helpers ───────────────────────────────────────────────────────────

void VideoWidget::drawTrackerDot(QPainter& p, QPointF wpt, int id,
                                  const QColor& color, bool isActive) const {
    int r = isActive ? 12 : 9;
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(color);
    p.drawEllipse(wpt, r, r);
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(wpt, r + 2, r + 2);
    p.setPen(Qt::white);
    QFont f("Arial", 9, QFont::Bold);
    p.setFont(f);
    p.drawText(wpt + QPointF(r + 4, 4), QString::number(id));
}

static void drawArrow(QPainter& p, QPointF from, QPointF to, double headLen = 6.0) {
    p.drawLine(from, to);
    QPointF dir = to - from;
    double len  = std::sqrt(dir.x()*dir.x() + dir.y()*dir.y());
    if (len < 1e-6) return;
    dir /= len;
    QPointF perp(-dir.y(), dir.x());
    p.drawLine(to, to - dir * headLen + perp * (headLen * 0.45));
    p.drawLine(to, to - dir * headLen - perp * (headLen * 0.45));
}

void VideoWidget::drawAxisLegend(QPainter& p) const {
    // Position: bottom-left of the video frame area
    double vw = frame_.isNull() ? width()  : frame_.width()  * scale_.width();
    double vh = frame_.isNull() ? height() : frame_.height() * scale_.height();
    QPointF bl(offset_.x() + 10, offset_.y() + vh - 10);

    bool show3D = calibration_ && calibration_->has3D();
    const double boxW = show3D ? 100 : 82, boxH = show3D ? 80 : 68;
    QRectF box(bl.x(), bl.y() - boxH, boxW, boxH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 5, 5);

    // Axis cross origin inside the box (lower-left quadrant of box)
    QPointF axO(box.left() + 28, box.bottom() - 18);
    const double axLen = 24;

    QPointF xDir(axLen, 0);   // default: +X right
    QPointF yDir(0, -axLen);  // default: +Y up
    QPointF zDir(-axLen * 0.6, -axLen * 0.6); // default: +Z up-left (height)

    if (calibration_ && calibration_->isValid()) {
        QPointF o  = frameToWidget(calibration_->stageToPixel(0, 0));
        QPointF xp = frameToWidget(calibration_->stageToPixel(1, 0));
        QPointF yp = frameToWidget(calibration_->stageToPixel(0, 1));
        QPointF xd = xp - o, yd = yp - o;
        double xl = std::sqrt(xd.x()*xd.x() + xd.y()*xd.y());
        double yl = std::sqrt(yd.x()*yd.x() + yd.y()*yd.y());
        if (xl > 0.1) xDir = xd / xl * axLen;
        if (yl > 0.1) yDir = yd / yl * axLen;

        if (show3D) {
            QPointF zBase = frameToWidget(calibration_->stageAtHeightToPixel(0, 0, 0));
            QPointF zTip  = frameToWidget(calibration_->stageAtHeightToPixel(0, 1, 0));
            QPointF zd    = zTip - zBase;
            double   zl   = std::sqrt(zd.x()*zd.x() + zd.y()*zd.y());
            if (zl > 0.1) zDir = zd / zl * axLen;
        }
    }

    p.setFont(QFont("Arial", 8, QFont::Bold));

    // X axis — green
    p.setPen(QPen(QColor(80, 220, 80), 2));
    drawArrow(p, axO, axO + xDir, 6);
    p.drawText(axO + xDir + QPointF(3, 4), "+X");

    // Y axis — cornflower blue
    p.setPen(QPen(QColor(100, 160, 255), 2));
    drawArrow(p, axO, axO + yDir, 6);
    QPointF yLabelOff = yDir + QPointF(yDir.x() > 0 ? 3 : -22, yDir.y() > 0 ? 10 : -3);
    p.drawText(axO + yLabelOff, "+Y");

    // Z axis (height) — amber, only when 3D calibrated
    if (show3D) {
        p.setPen(QPen(QColor(255, 180, 40), 2));
        drawArrow(p, axO, axO + zDir, 6);
        QPointF zLabelOff = zDir + QPointF(zDir.x() > 0 ? 3 : -22, zDir.y() > 0 ? 10 : -3);
        p.drawText(axO + zLabelOff, "+Z");
    }

    // Dot at axis origin
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(200, 200, 200, 180));
    p.drawEllipse(axO, 3, 3);

    // Legend title
    p.setPen(QColor(180, 180, 180));
    p.setFont(QFont("Arial", 7));
    p.drawText(QRectF(box.left() + 2, box.top() + 3, boxW - 4, 13),
               Qt::AlignCenter, show3D ? "Stage axes (3D)" : "Stage axes");
}

void VideoWidget::drawCalibOverlay(QPainter& p) const {
    if (!showCalibOverlay_ && !calibMode_) return;
    bool hasAny = hasCalibOrigin_ || !calibOverlayPoints_.isEmpty();
    if (!hasAny && !calibMode_) return;

    QPointF originWpt;

    if (hasCalibOrigin_) {
        originWpt = frameToWidget(calibOriginPoint_);

        // Explicit lines (e.g. rectangle closing edges) take priority over auto lines.
        // Both can coexist: auto lines for origin→points, explicit for extra edges.
        for (auto& [a, b] : calibExplicitLines_) {
            p.setPen(QPen(QColor(100, 220, 100, 140), 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawLine(frameToWidget(a), frameToWidget(b));
        }

        // Auto origin→point lines with distance labels
        for (int i = 0; i < calibOverlayPoints_.size(); ++i) {
            QPointF ptWpt = frameToWidget(calibOverlayPoints_[i]);

            p.setPen(QPen(QColor(100, 220, 100, 160), 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawLine(originWpt, ptWpt);

            if (i < calibDistanceLabels_.size() && !calibDistanceLabels_[i].isEmpty()) {
                QPointF mid = (originWpt + ptWpt) / 2.0 + QPointF(0, -10);
                QString lbl = calibDistanceLabels_[i];
                QFont f("Arial", 8);
                p.setFont(f);
                QFontMetrics fm(f);
                QRectF lr = fm.boundingRect(lbl);
                lr.adjust(-3, -2, 3, 2);
                lr.moveCenter(mid);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 160));
                p.drawRoundedRect(lr, 3, 3);
                p.setPen(QColor(200, 255, 200));
                p.setBrush(Qt::NoBrush);
                p.drawText(lr, Qt::AlignCenter, lbl);
            }
        }

        // Origin marker: green circle + crosshair arms
        bool originHL = (calibOverlayHighlight_ == -2);
        int r = originHL ? 11 : 8;
        QColor fill = originHL ? QColor(120, 255, 120) : QColor(80, 210, 80, 230);

        const double arm = r + 10;
        p.setPen(QPen(fill, 1.5));
        p.drawLine(QPointF(originWpt.x() - arm, originWpt.y()),
                   QPointF(originWpt.x() + arm, originWpt.y()));
        p.drawLine(QPointF(originWpt.x(), originWpt.y() - arm),
                   QPointF(originWpt.x(), originWpt.y() + arm));

        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(fill);
        p.drawEllipse(originWpt, r, r);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(originWpt, r + 2, r + 2);

        p.setPen(Qt::black);
        p.setFont(QFont("Arial", 7, QFont::Bold));
        p.drawText(QRectF(originWpt.x() - r, originWpt.y() - r, r * 2.0, r * 2.0),
                   Qt::AlignCenter, "O");
    }

    // Non-origin calibration points
    for (int i = 0; i < calibOverlayPoints_.size(); ++i) {
        QPointF wpt = frameToWidget(calibOverlayPoints_[i]);
        bool isHighlighted = (i == calibOverlayHighlight_);
        QColor fill = isHighlighted ? QColor(255, 200, 0) : QColor(255, 220, 60, 200);
        QColor ring = isHighlighted ? Qt::white : QColor(180, 140, 0);
        int r = isHighlighted ? 10 : 7;

        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(fill);
        p.drawEllipse(wpt, r, r);
        p.setPen(QPen(ring, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(wpt, r + 2, r + 2);

        p.setPen(Qt::black);
        p.setFont(QFont("Arial", 8, QFont::Bold));
        p.drawText(QRectF(wpt.x() - r, wpt.y() - r, r * 2.0, r * 2.0),
                   Qt::AlignCenter, QString::number(i + 1));
    }

    // Ghost preview dot while waiting to place a new point
    if (calibMode_) {
        p.setPen(QPen(QColor(255, 220, 60, 200), 1.5));
        p.setBrush(QColor(255, 220, 60, 60));
        p.drawEllipse(mouseWidgetPos_, 8, 8);
    }

    // Axis legend whenever calib overlay is active
    if (hasCalibOrigin_ || calibMode_)
        drawAxisLegend(p);
}

void VideoWidget::drawFloorGrid(QPainter& p) const {
    if (!showFloorGrid_ || !calibration_ || !calibration_->isValid()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setClipRect(QRectF(offset_.x(), offset_.y(),
                         frame_.width()  * scale_.width(),
                         frame_.height() * scale_.height()));

    const int kRange = 25;

    // ── Floor plane (Y=0) — green ─────────────────────────────────────────
    const QColor floorGrid(80, 200, 80, 45);
    const QColor floorAxis(80, 220, 80, 90);
    for (int x = -kRange; x <= kRange; x++) {
        p.setPen(QPen(x == 0 ? floorAxis : floorGrid, x == 0 ? 1.5 : 1.0));
        p.drawLine(frameToWidget(calibration_->stageToPixel(float(x), float(-kRange))),
                   frameToWidget(calibration_->stageToPixel(float(x), float( kRange))));
    }
    for (int z = -kRange; z <= kRange; z++) {
        p.setPen(QPen(z == 0 ? floorAxis : floorGrid, z == 0 ? 1.5 : 1.0));
        p.drawLine(frameToWidget(calibration_->stageToPixel(float(-kRange), float(z))),
                   frameToWidget(calibration_->stageToPixel(float( kRange), float(z))));
    }

    p.restore();
}

void VideoWidget::drawClickPlaneOverlay(QPainter& p) const {
    if (!showClickPlane_ || !calibration_ || !calibration_->isValid()) return;

    const double vw = frame_.isNull() ? width()  : frame_.width()  * scale_.width();
    const double vh = frame_.isNull() ? height() : frame_.height() * scale_.height();
    const float  h  = clickPlaneHeight_;

    // ── Amber click-plane grid (requires 3D calibration) ─────────────────
    bool canDraw3D = calibration_->has3D();

    if (canDraw3D) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setClipRect(QRectF(offset_.x(), offset_.y(), vw, vh));

        const int kRange = 25;
        const QColor clickGrid(255, 160, 40, 50);
        const QColor clickAxis(255, 190, 60, 100);

        std::function<QPointF(float, float)> clickPt;
        clickPt = [&](float stX, float stZ) -> QPointF {
            return frameToWidget(calibration_->stageAtHeightToPixel(stX, h, stZ));
        };

        for (int x = -kRange; x <= kRange; x++) {
            p.setPen(QPen(x == 0 ? clickAxis : clickGrid, x == 0 ? 1.5 : 1.0));
            p.drawLine(clickPt(float(x), float(-kRange)),
                       clickPt(float(x), float( kRange)));
        }
        for (int z = -kRange; z <= kRange; z++) {
            p.setPen(QPen(z == 0 ? clickAxis : clickGrid, z == 0 ? 1.5 : 1.0));
            p.drawLine(clickPt(float(-kRange), float(z)),
                       clickPt(float( kRange), float(z)));
        }
        p.restore();
    }

    // ── Label ─────────────────────────────────────────────────────────────
    const QString label = QString("Height: %1 m").arg(double(h), 0, 'f', 2);
    QFont f("Arial", 9);
    p.setFont(f);
    QFontMetrics fm(f);
    QRectF lr = fm.boundingRect(label);
    lr.adjust(-5, -3, 5, 3);
    lr.moveBottomRight(QPointF(offset_.x() + vw - 8, offset_.y() + vh - 8));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 80, 140, 200));
    p.drawRoundedRect(lr, 4, 4);
    p.setPen(QColor(180, 210, 255));
    p.setBrush(Qt::NoBrush);
    p.drawText(lr, Qt::AlignCenter, label);
}

void VideoWidget::drawHeightGhosts(QPainter& p) const {
    if (!calibration_ || !calibration_->isValid()) return;

    for (auto it = ownPositions_.cbegin(); it != ownPositions_.cend(); ++it) {
        int id = it.key();
        QColor color = Qt::white;
        for (const auto& t : ownTrackers_)
            if (t.id == id) { color = t.color; break; }

        // Corrected position — where the person actually is (main dot, click plane)
        QPointF wpt = frameToWidget(calibration_->stageToPixel(it.value().first,
                                                                it.value().second));

        // ① Floor / calibration-plane shadow.
        //    If parallax correction is active, the raw position differs from the
        //    corrected one and shows where the image projects onto the floor.
        //    Otherwise raw == corrected and the shadow sits under the main dot.
        QPointF shadowWpt = wpt;
        if (ownRawPositions_.contains(id)) {
            const auto& raw = ownRawPositions_[id];
            shadowWpt = frameToWidget(calibration_->stageToPixel(raw.first, raw.second));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 100));
        p.setOpacity(1.0);
        p.drawEllipse(shadowWpt, 14.0, 14.0);
        // Shadow cross-hair nub
        p.setPen(QPen(QColor(0, 0, 0, 80), 1.5));
        p.drawLine(shadowWpt + QPointF(-8, 0), shadowWpt + QPointF(8, 0));
        p.drawLine(shadowWpt + QPointF(0, -8), shadowWpt + QPointF(0, 8));

        // ② Height-plane ring (click plane = PSN output height) — solid, tracker colour.
        //    Main dot painted on top by the tracker loop.
        QColor clickColor = color;
        clickColor.setAlphaF(0.40);
        p.setPen(QPen(clickColor, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(wpt, 13.0, 13.0);

        p.setOpacity(1.0);
    }
}

int VideoWidget::nearestCalibPoint(QPointF widgetPos, double threshold) const {
    for (int i = 0; i < calibOverlayPoints_.size(); ++i) {
        if (QLineF(widgetPos, frameToWidget(calibOverlayPoints_[i])).length() < threshold)
            return i;
    }
    if (hasCalibOrigin_ &&
        QLineF(widgetPos, frameToWidget(calibOriginPoint_)).length() < threshold)
        return -1;
    return -2;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);

    if (scaledFrame_.isNull()) {
        p.setPen(QColor(100, 100, 100));
        if (ndiSourceConfigured_) {
            p.setFont(QFont("Arial", 18));
            p.drawText(rect(), Qt::AlignCenter, "Waiting for video…");
        } else {
            p.setFont(QFont("Arial", 18, QFont::Bold));
            p.drawText(rect().adjusted(0, 0, 0, -30), Qt::AlignCenter, "No video source");
            p.setFont(QFont("Arial", 12));
            p.setPen(QColor(70, 70, 70));
            p.drawText(rect().adjusted(0, 30, 0, 0), Qt::AlignCenter,
                       "Select a video source in the sidebar");
        }
    } else {
        p.drawPixmap(int(offset_.x()), int(offset_.y()), scaledFrame_);
    }

    // Floor grid — drawn behind everything else
    drawFloorGrid(p);

    // Calibration overlay is always drawn, independent of calibration validity.
    drawCalibOverlay(p);

    if (!calibration_ || !calibration_->isValid()) {
        if (activeTrackerId_ >= 0) {
            QPointF wpt = mouseWidgetPos_;
            p.setPen(QPen(activeColor_, mouseHeld_ ? 2.5 : 1.5,
                          mouseHeld_ ? Qt::SolidLine : Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            const double arm = 16;
            p.drawLine(QPointF(wpt.x() - arm, wpt.y()), QPointF(wpt.x() + arm, wpt.y()));
            p.drawLine(QPointF(wpt.x(), wpt.y() - arm), QPointF(wpt.x(), wpt.y() + arm));
            p.drawEllipse(wpt, 6, 6);
        }
        return;
    }

    // ── Height ghosts: floor shadow + PSN output ring (drawn behind main dots) ──
    drawHeightGhosts(p);

    // ── Own tracker positions (main dot = click plane) ────────────────────
    for (auto it = ownPositions_.cbegin(); it != ownPositions_.cend(); ++it) {
        int id = it.key();
        QColor color = Qt::white;
        for (const auto& t : ownTrackers_)
            if (t.id == id) { color = t.color; break; }
        bool assigned = assignedTrackers_.isEmpty() || assignedTrackers_.contains(id);
        p.setOpacity(assigned ? 1.0 : unassignedAlpha_ / 255.0);
        drawTrackerDot(p, frameToWidget(calibration_->stageToPixel(it.value().first,
                                                                    it.value().second)),
                       id, color, id == activeTrackerId_);
        p.setOpacity(1.0);
    }

    // ── Remote tracker positions ──────────────────────────────────────────
    for (auto it = remotePositions_.cbegin(); it != remotePositions_.cend(); ++it) {
        int id = it.key();
        if (ownPositions_.contains(id)) continue;
        QColor color = Qt::cyan;
        for (const auto& t : remoteTrackers_)
            if (t.id == id) { color = t.color; break; }
        bool assigned = assignedTrackers_.isEmpty() || assignedTrackers_.contains(id);
        p.setOpacity(assigned ? 0.85 : unassignedAlpha_ / 255.0 * 0.85);
        drawTrackerDot(p, frameToWidget(calibration_->stageToPixel(it.value().x(),
                                                                    it.value().z())),
                       id, color.lighter(130), false);
        p.setOpacity(1.0);
    }

    // ── Click plane indicator ─────────────────────────────────────────────
    drawClickPlaneOverlay(p);

    // ── Axis legend on main view ──────────────────────────────────────────
    drawAxisLegend(p);

    // ── Mouse crosshair for active tracker ───────────────────────────────
    if (activeTrackerId_ >= 0) {
        QPointF wpt = mouseWidgetPos_;
        p.setPen(QPen(activeColor_, mouseHeld_ ? 2.5 : 1.5,
                      mouseHeld_ ? Qt::SolidLine : Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        const double arm = 16;
        p.drawLine(QPointF(wpt.x() - arm, wpt.y()), QPointF(wpt.x() + arm, wpt.y()));
        p.drawLine(QPointF(wpt.x(), wpt.y() - arm), QPointF(wpt.x(), wpt.y() + arm));
        p.drawEllipse(wpt, 6, 6);
    }
}

// ── Input events ──────────────────────────────────────────────────────────────

void VideoWidget::mouseMoveEvent(QMouseEvent* e) {
    mouseHeld_ = e->buttons() & Qt::LeftButton;
    mouseWidgetPos_ = e->position();

    if (mouseHeld_ && draggingCalibIdx_ >= -1) {
        emit existingCalibPointMoved(draggingCalibIdx_, widgetToFrame(mouseWidgetPos_));
    }

    emit mousePosInFrame(widgetToFrame(mouseWidgetPos_));
    update();
}

void VideoWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    mouseHeld_ = true;
    mouseWidgetPos_ = e->position();

    if (calibMode_) {
        // Press starts a drag; release (or immediate release) finalises the position.
        calibDragging_ = true;
    } else {
        int idx = nearestCalibPoint(e->position());
        if (idx >= -1) {
            draggingCalibIdx_ = idx;
            // Don't emit click yet — wait for release so drag can happen first.
            update();
            return;
        }
        emit mouseLeftPressed(widgetToFrame(e->position()));
    }
    update();
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    mouseHeld_ = false;

    if (calibMode_ && calibDragging_) {
        calibDragging_ = false;
        calibMode_ = false;
        setCursor(Qt::ArrowCursor);
        emit calibPointClicked(widgetToFrame(mouseWidgetPos_));
    } else if (draggingCalibIdx_ >= -1) {
        int idx = draggingCalibIdx_;
        draggingCalibIdx_ = -2;
        emit existingCalibPointClicked(idx, mouseWidgetPos_.toPoint());
    }
    update();
}

void VideoWidget::wheelEvent(QWheelEvent* e) {
    int ticks = e->angleDelta().y();
    if (ticks != 0) {
        emit planeHeightScrolled((ticks / 120.0f) * 0.05f);
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

bool VideoWidget::event(QEvent* e) {
    if (e->type() == QEvent::WindowDeactivate) {
        mouseHeld_        = false;
        calibDragging_    = false;
        draggingCalibIdx_ = -2;
        update();
    }
    return QWidget::event(e);
}
