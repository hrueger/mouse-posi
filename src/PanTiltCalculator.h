#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include "Project.h"

struct PanTiltDmx {
    quint16 pan    = 0;    // full 16-bit: high byte = coarse, low byte = fine
    quint16 tilt   = 0;
    float   panDeg  = 0.f;
    float   tiltDeg = 0.f;
    bool    valid  = false;
};

// Computes pan/tilt DMX values for a fixture given its position and a target position.
// All positions are in view space: X-right, Y-up, Z-depth (same as MvrObjectData::positionM).
// Moving-head convention: pan=0°/tilt=0° = beam pointing straight down (-Y in fixture-local space).
// fixtureRot: combined world-from-fixture rotation (importRot * obj.xformRot); identity = upright.
class PanTiltCalculator {
public:
    static PanTiltDmx calculate(
        QVector3D             fixturePos,
        QVector3D             targetPos,
        const GdtfDmxProfile& profile,
        const QMatrix4x4&     fixtureRot = QMatrix4x4());

private:
    // Returns full 16-bit value: high byte = coarse, low byte = fine
    static quint16 degreesToDmx(float deg, const GdtfChannelInfo& ch);
};
