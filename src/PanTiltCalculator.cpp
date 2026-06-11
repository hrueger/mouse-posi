#include "PanTiltCalculator.h"
#include <QtMath>

quint16 PanTiltCalculator::degreesToDmx(float deg, const GdtfChannelInfo& ch) {
    if (ch.address < 0) return 0;

    const float range = ch.maxDeg - ch.minDeg;
    if (qFuzzyIsNull(range)) return 0;

    const float t = qBound(0.f, (deg - ch.minDeg) / range, 1.f);
    return quint16(t * 65535.f + 0.5f);
}

PanTiltDmx PanTiltCalculator::calculate(QVector3D fixturePos,
                                         QVector3D targetPos,
                                         const GdtfDmxProfile& profile,
                                         const QMatrix4x4& fixtureRot) {
    PanTiltDmx result;
    if (!profile.valid) return result;

    // Transform target direction into fixture-local view space.
    // fixtureRot maps fixture-local → world; its inverse maps world → fixture-local.
    // mapVector ignores translation (direction vector, w=0).
    const QVector3D v_world = targetPos - fixturePos;
    const QVector3D v_local = fixtureRot.inverted().mapVector(v_world);

    // Moving-head convention: beam at rest points in local -Y (down when hanging from ceiling).
    // Tilt = deviation from -Y axis:   0° = straight down, 90° = horizontal.
    // Pan  = rotation around local Y: 0° = fixture's -Z forward direction.
    const float horizDist = std::sqrt(v_local.x() * v_local.x() + v_local.z() * v_local.z());
    const float tiltDeg   = qRadiansToDegrees(std::atan2(horizDist, -v_local.y()));
    const float panDeg    = qRadiansToDegrees(std::atan2(-v_local.x(), -v_local.z()));

    result.pan     = degreesToDmx(panDeg,  profile.pan);
    result.tilt    = degreesToDmx(tiltDeg, profile.tilt);
    result.panDeg  = panDeg;
    result.tiltDeg = tiltDeg;
    result.valid   = true;
    return result;
}
