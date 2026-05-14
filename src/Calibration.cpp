#include "Calibration.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>

double Calibration::compute(const QList<QPointF>& imagePoints,
                             const QList<QPointF>& stagePoints) {
    if (imagePoints.size() < 4 || imagePoints.size() != stagePoints.size())
        return -1.0;

    std::vector<cv::Point2f> src, dst;
    for (const auto& p : imagePoints) src.emplace_back(p.x(), p.y());
    for (const auto& p : stagePoints) dst.emplace_back(p.x(), p.y());

    H_ = cv::findHomography(src, dst, cv::RANSAC, 3.0);
    if (H_.empty()) return -1.0;
    H_inv_ = H_.inv();

    // Compute reprojection error in image space
    std::vector<cv::Point2f> projected;
    cv::perspectiveTransform(dst, projected, H_inv_);
    double err = 0.0;
    for (size_t i = 0; i < src.size(); ++i) {
        double dx = src[i].x - projected[i].x;
        double dy = src[i].y - projected[i].y;
        err += std::sqrt(dx*dx + dy*dy);
    }
    return err / static_cast<double>(src.size());
}

QPointF Calibration::pixelToStage(QPointF px) const {
    if (H_.empty()) return {};
    std::vector<cv::Point2f> src = {{static_cast<float>(px.x()), static_cast<float>(px.y())}};
    std::vector<cv::Point2f> dst;
    cv::perspectiveTransform(src, dst, H_);
    return {dst[0].x, dst[0].y};
}

QPointF Calibration::stageToPixel(float stageX, float stageZ) const {
    if (H_inv_.empty()) return {};
    std::vector<cv::Point2f> src = {{stageX, stageZ}};
    std::vector<cv::Point2f> dst;
    cv::perspectiveTransform(src, dst, H_inv_);
    return {dst[0].x, dst[0].y};
}

QList<double> Calibration::toList() const {
    if (H_.empty()) return {};
    QList<double> vals;
    const double* d = reinterpret_cast<const double*>(H_.data);
    for (int i = 0; i < 9; ++i) vals << d[i];
    return vals;
}

void Calibration::fromList(const QList<double>& vals) {
    if (vals.size() != 9) return;
    H_ = cv::Mat(3, 3, CV_64F);
    double* d = reinterpret_cast<double*>(H_.data);
    for (int i = 0; i < 9; ++i) d[i] = vals[i];
    H_inv_ = H_.inv();
}
