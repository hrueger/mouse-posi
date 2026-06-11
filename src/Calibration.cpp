#include "Calibration.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <QtMath>
#include <cmath>

// ── 2D floor homography ───────────────────────────────────────────────────────

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
    has3D_ = false;
    P_     = cv::Mat();

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

// ── 3D calibration from two planes ───────────────────────────────────────────

double Calibration::compute3D(const QList<QPointF>& floorImg,
                               const QList<QPointF>& elevImg,
                               const QList<QPointF>& stageXZ,
                               float markerHeight) {
    if (floorImg.size() < 4 || elevImg.size() < 4 || stageXZ.size() < 4
        || markerHeight <= 0.0f)
        return -1.0;

    std::vector<cv::Point2f> floorSrc, elevSrc, stageDst;
    for (int i = 0; i < 4; ++i) {
        floorSrc.emplace_back(floorImg[i].x(), floorImg[i].y());
        elevSrc.emplace_back( elevImg[i].x(),  elevImg[i].y());
        stageDst.emplace_back(stageXZ[i].x(),  stageXZ[i].y());
    }

    // Floor homography (image → stage XZ) for pixelToStage() fallback
    cv::Mat Hf = cv::findHomography(floorSrc, stageDst, cv::RANSAC, 3.0);
    if (Hf.empty()) return -1.0;

    // Fit 3×4 projection matrix P via DLT over all 8 correspondences:
    // 4 floor (Y=0) + 4 elevated (Y=markerHeight).
    //
    // Using only the translation column of (He_inv - Hf_inv) to derive P[:,1]
    // is only correct at stage origin; the X/Z direction columns also differ
    // between planes under perspective, causing uniform-looking height shifts
    // away from the origin. DLT solves for P directly without that assumption.
    std::vector<cv::Point3d> pts3;
    std::vector<cv::Point2d> pts2;
    for (int i = 0; i < 4; ++i) {
        pts3.emplace_back(stageDst[i].x, 0.0,                  stageDst[i].y);
        pts2.emplace_back(floorSrc[i].x, floorSrc[i].y);
        pts3.emplace_back(stageDst[i].x, double(markerHeight),  stageDst[i].y);
        pts2.emplace_back(elevSrc[i].x,  elevSrc[i].y);
    }

    // Hartley normalisation: centroid → origin, mean distance → √3 (3D) / √2 (2D)
    cv::Point3d c3(0, 0, 0);
    cv::Point2d c2(0, 0);
    for (int i = 0; i < 8; ++i) { c3.x += pts3[i].x; c3.y += pts3[i].y; c3.z += pts3[i].z; }
    for (int i = 0; i < 8; ++i) { c2.x += pts2[i].x; c2.y += pts2[i].y; }
    c3.x /= 8; c3.y /= 8; c3.z /= 8;
    c2.x /= 8; c2.y /= 8;

    double d3 = 0, d2 = 0;
    for (int i = 0; i < 8; ++i) {
        double dx = pts3[i].x-c3.x, dy = pts3[i].y-c3.y, dz = pts3[i].z-c3.z;
        d3 += std::sqrt(dx*dx + dy*dy + dz*dz);
        dx = pts2[i].x-c2.x; dy = pts2[i].y-c2.y;
        d2 += std::sqrt(dx*dx + dy*dy);
    }
    d3 /= 8; d2 /= 8;
    double s3 = std::sqrt(3.0) / d3;
    double s2 = std::sqrt(2.0) / d2;

    cv::Mat T3 = (cv::Mat_<double>(4,4) <<
        s3, 0,  0,  -s3*c3.x,
        0,  s3, 0,  -s3*c3.y,
        0,  0,  s3, -s3*c3.z,
        0,  0,  0,  1);
    cv::Mat T2 = (cv::Mat_<double>(3,3) <<
        s2, 0,  -s2*c2.x,
        0,  s2, -s2*c2.y,
        0,  0,  1);

    // Each 3D↔2D pair gives 2 linear equations in the 12 elements of P.
    cv::Mat A(16, 12, CV_64F, 0.0);
    for (int i = 0; i < 8; ++i) {
        double X = s3*(pts3[i].x - c3.x);
        double Y = s3*(pts3[i].y - c3.y);
        double Z = s3*(pts3[i].z - c3.z);
        double u = s2*(pts2[i].x - c2.x);
        double v = s2*(pts2[i].y - c2.y);

        double* a0 = A.ptr<double>(2*i);
        a0[0]=X; a0[1]=Y; a0[2]=Z; a0[3]=1.0;
        a0[8]=-u*X; a0[9]=-u*Y; a0[10]=-u*Z; a0[11]=-u;

        double* a1 = A.ptr<double>(2*i + 1);
        a1[4]=X; a1[5]=Y; a1[6]=Z; a1[7]=1.0;
        a1[8]=-v*X; a1[9]=-v*Y; a1[10]=-v*Z; a1[11]=-v;
    }

    cv::SVD svd(A, cv::SVD::FULL_UV);
    cv::Mat Pn = svd.vt.row(11).clone().reshape(1, 3); // normalised 3×4 P
    P_ = T2.inv() * Pn * T3;                           // denormalise

    // Camera centre = null space of P  (last right-singular vector)
    cv::SVD svdP(P_, cv::SVD::FULL_UV);
    cv::Mat cn = svdP.vt.row(3).t();
    double cw  = cn.at<double>(3);
    C3d_ = cv::Vec3d(cn.at<double>(0)/cw, cn.at<double>(1)/cw, cn.at<double>(2)/cw);

    H_     = Hf;
    H_inv_ = Hf.inv();
    has3D_ = true;

    // Reprojection error over all 8 points
    double err = 0.0;
    for (int i = 0; i < 8; ++i) {
        cv::Mat pt = (cv::Mat_<double>(4,1) << pts3[i].x, pts3[i].y, pts3[i].z, 1.0);
        cv::Mat im = P_ * pt;
        double u = im.at<double>(0) / im.at<double>(2);
        double v = im.at<double>(1) / im.at<double>(2);
        double dx = u - pts2[i].x, dy = v - pts2[i].y;
        err += std::sqrt(dx*dx + dy*dy);
    }
    return err / 8.0;
}

// ── Pixel → stage ─────────────────────────────────────────────────────────────

QPointF Calibration::pixelToStage(QPointF px) const {
    if (H_.empty()) return {};
    std::vector<cv::Point2f> src = {{float(px.x()), float(px.y())}};
    std::vector<cv::Point2f> dst;
    cv::perspectiveTransform(src, dst, H_);
    return {dst[0].x, dst[0].y};
}

QPointF Calibration::pixelToStageAtHeight(QPointF px, float h) const {
    if (!has3D_ || h == 0.0f) return pixelToStage(px);

    // Floor point the ray passes through
    QPointF floor = pixelToStage(px);
    double Cx = C3d_[0], Cy = C3d_[1], Cz = C3d_[2];
    if (std::abs(Cy) < 1e-6) return floor;

    // Ray: R(t) = C + t*(floor - C); at Y=h: Cy + t*(0-Cy) = h  → t = (Cy-h)/Cy
    double t  = (Cy - static_cast<double>(h)) / Cy;
    return QPointF(Cx + t * (floor.x() - Cx),
                   Cz + t * (floor.y() - Cz));
}

// ── Stage → pixel ─────────────────────────────────────────────────────────────

QPointF Calibration::stageToPixel(float stageX, float stageZ) const {
    if (H_inv_.empty()) return {};
    std::vector<cv::Point2f> src = {{stageX, stageZ}};
    std::vector<cv::Point2f> dst;
    cv::perspectiveTransform(src, dst, H_inv_);
    return {dst[0].x, dst[0].y};
}

QPointF Calibration::stageAtHeightToPixel(float stX, float h, float stZ) const {
    if (!has3D_ || P_.empty()) return stageToPixel(stX, stZ);
    cv::Mat pt = (cv::Mat_<double>(4,1) << (double)stX, (double)h, (double)stZ, 1.0);
    cv::Mat px = P_ * pt;
    return QPointF(px.at<double>(0) / px.at<double>(2),
                   px.at<double>(1) / px.at<double>(2));
}

// ── Serialisation ─────────────────────────────────────────────────────────────

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
    has3D_ = false;
    P_     = cv::Mat();
}

QList<double> Calibration::projectionToList() const {
    if (!has3D_ || P_.empty()) return {};
    QList<double> vals;
    const double* d = reinterpret_cast<const double*>(P_.data);
    for (int i = 0; i < 12; ++i) vals << d[i];
    return vals;
}

void Calibration::projectionFromList(const QList<double>& vals) {
    if (vals.size() != 12) return;
    P_ = cv::Mat(3, 4, CV_64F);
    double* d = reinterpret_cast<double*>(P_.data);
    for (int i = 0; i < 12; ++i) d[i] = vals[i];
    cv::SVD svd(P_, cv::SVD::FULL_UV);
    cv::Mat cn = svd.vt.row(3).t();
    double cw  = cn.at<double>(3);
    C3d_  = cv::Vec3d(cn.at<double>(0)/cw, cn.at<double>(1)/cw, cn.at<double>(2)/cw);
    has3D_ = true;
}

// ── Camera position helpers ───────────────────────────────────────────────────

QVector3D Calibration::cameraCenter3D() const
{
    if (!has3D_) return {};
    return QVector3D(float(C3d_[0]), float(C3d_[1]), float(C3d_[2]));
}

QVector3D Calibration::computeCameraFromFov(
    const QList<QPointF>& imagePoints,
    const QList<QPointF>& stagePoints,
    float fovHDeg, QSize imageSize)
{
    if (imagePoints.size() < 4 || imagePoints.size() != stagePoints.size())
        return {};
    if (imageSize.isEmpty() || fovHDeg <= 0.0f || fovHDeg >= 180.0f)
        return {};

    const float cx = imageSize.width()  / 2.0f;
    const float cy = imageSize.height() / 2.0f;
    const float fx = cx / float(std::tan(double(fovHDeg) * M_PI / 360.0));

    std::vector<cv::Point3f> worldPts;
    std::vector<cv::Point2f> imgPts;
    for (int i = 0; i < imagePoints.size(); ++i) {
        worldPts.emplace_back(float(stagePoints[i].x()), 0.0f, float(stagePoints[i].y()));
        imgPts.emplace_back(float(imagePoints[i].x()), float(imagePoints[i].y()));
    }

    cv::Mat K = (cv::Mat_<double>(3,3) <<
        double(fx), 0, double(cx),
        0, double(fx), double(cy),
        0, 0, 1);
    cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F);
    cv::Mat rvec, tvec;

    if (!cv::solvePnP(worldPts, imgPts, K, dist, rvec, tvec))
        return {};

    cv::Mat R;
    cv::Rodrigues(rvec, R);
    cv::Mat camCenter = -R.t() * tvec;

    return QVector3D(
        float(camCenter.at<double>(0)),
        float(camCenter.at<double>(1)),
        float(camCenter.at<double>(2)));
}

// ── Camera 2D calibration (pixel → pan/tilt DMX) ─────────────────────────────

double Calibration::computeCamera2D(Camera2DCalibration& calib) {
    if (calib.points.size() < 4) return -1.0;

    std::vector<cv::Point2f> src, dst;
    for (const auto& p : calib.points) {
        src.emplace_back(float(p.pixel.x()), float(p.pixel.y()));
        dst.emplace_back(p.panDmx, p.tiltDmx);
    }

    cv::Mat H = cv::findHomography(src, dst, cv::RANSAC, 3.0);
    if (H.empty()) { calib.valid = false; return -1.0; }

    calib.homography.clear();
    const double* d = reinterpret_cast<const double*>(H.data);
    for (int i = 0; i < 9; ++i) calib.homography << d[i];
    calib.valid = true;

    // Compute mean reprojection error
    double err = 0.0;
    for (const auto& p : calib.points) {
        const cv::Mat pt = (cv::Mat_<double>(3,1) << p.pixel.x(), p.pixel.y(), 1.0);
        const cv::Mat res = H * pt;
        const double px = res.at<double>(0) / res.at<double>(2);
        const double py = res.at<double>(1) / res.at<double>(2);
        err += std::sqrt((px - p.panDmx)*(px - p.panDmx) + (py - p.tiltDmx)*(py - p.tiltDmx));
    }
    return err / calib.points.size();
}

QPointF Calibration::pixelToPanTilt(const Camera2DCalibration& calib, QPointF pixel) {
    if (!calib.valid || calib.homography.size() != 9) return QPointF(-1, -1);

    cv::Mat H(3, 3, CV_64F);
    double* d = reinterpret_cast<double*>(H.data);
    for (int i = 0; i < 9; ++i) d[i] = calib.homography[i];

    const cv::Mat pt = (cv::Mat_<double>(3,1) << pixel.x(), pixel.y(), 1.0);
    const cv::Mat res = H * pt;
    const double w = res.at<double>(2);
    if (std::abs(w) < 1e-9) return QPointF(-1, -1);
    return QPointF(res.at<double>(0) / w, res.at<double>(1) / w);
}
