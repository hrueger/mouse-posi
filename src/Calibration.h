#pragma once
#include <QPointF>
#include <QList>
#include <QVector3D>
#include <QSize>
#include <opencv2/core.hpp>

class Calibration {
public:
    // 2D floor homography from ≥4 point pairs. Returns mean reprojection error (px), -1 on failure.
    double compute(const QList<QPointF>& imagePoints, const QList<QPointF>& stagePoints);

    // 3D calibration from 4 floor + 4 elevated image points at the same stage XZ positions.
    // markerHeight is the real-world Y of the elevated markers (metres).
    // Also sets the internal floor homography so pixelToStage() stays valid.
    // Returns mean reprojection error over all 8 points (px), -1 on failure.
    double compute3D(const QList<QPointF>& floorImagePts,
                     const QList<QPointF>& elevatedImagePts,
                     const QList<QPointF>& stageXZPts,
                     float markerHeight);

    bool isValid()   const { return !H_.empty(); }
    bool has3D()     const { return has3D_; }

    // pixel → stage XZ (floor plane, Y=0)
    QPointF pixelToStage(QPointF px) const;

    // pixel → stage XZ on an arbitrary height plane.
    // Uses the 3D camera model when available, falls back to floor homography for h=0.
    QPointF pixelToStageAtHeight(QPointF px, float h) const;

    // stage XZ → pixel (floor plane)
    QPointF stageToPixel(float stageX, float stageZ) const;

    // 3D stage point → pixel.  Falls back to stageToPixel when no 3D calibration.
    QPointF stageAtHeightToPixel(float stX, float h, float stZ) const;

    // Serialize/deserialize the 3×3 floor homography (9 values, row-major)
    QList<double> toList() const;
    void          fromList(const QList<double>& vals);

    // Serialize/deserialize the 3×4 projection matrix (12 values, row-major).
    // Call fromList() first to restore the floor homography.
    QList<double> projectionToList() const;
    void          projectionFromList(const QList<double>& vals);

    // Camera centre in stage space (X,Y,Z). Valid only when has3D().
    QVector3D cameraCenter3D() const;

    // Estimate camera 3D position from floor calibration + horizontal FOV.
    // Uses PnP with the provided image/stage point pairs.
    // imageSize is the video frame dimensions in pixels.
    // Returns null vector on failure.
    static QVector3D computeCameraFromFov(
        const QList<QPointF>& imagePoints,
        const QList<QPointF>& stagePoints,
        float fovHDeg, QSize imageSize);

private:
    cv::Mat    H_;      // 3×3 floor homography  (image → stage XZ)
    cv::Mat    H_inv_;  // inverse               (stage XZ → image)
    cv::Mat    P_;      // 3×4 projection matrix (3D stage → image)
    cv::Vec3d  C3d_;    // camera centre in stage 3D space
    bool       has3D_ = false;
};
