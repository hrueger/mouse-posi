#pragma once
#include <QPointF>
#include <QList>
#include <opencv2/core.hpp>

class Calibration {
public:
    // Compute homography from ≥4 matching point pairs. Returns reprojection error (pixels).
    // Returns -1.0 on failure.
    double compute(const QList<QPointF>& imagePoints, const QList<QPointF>& stagePoints);

    bool isValid() const { return !H_.empty(); }

    // pixel coords (in original frame space) → stage XZ in meters
    QPointF pixelToStage(QPointF px) const;

    // stage XZ in meters → pixel coords (in original frame space)
    QPointF stageToPixel(float stageX, float stageZ) const;

    // Serialize/deserialize the 3×3 matrix as 9 row-major doubles
    QList<double> toList() const;
    void          fromList(const QList<double>& vals);

private:
    cv::Mat H_;     // 3×3 CV_64F homography
    cv::Mat H_inv_; // precomputed inverse
};
