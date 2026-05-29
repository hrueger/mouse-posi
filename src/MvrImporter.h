#pragma once
#include <QString>
#include <QVector3D>
#include <QList>
#include <QVector>
#include <QColor>

// ─── Legacy fixture-only structure (kept for old callers) ─────────────────────

struct MvrFixture {
    QString   name;
    QString   gdtfSpec;
    QVector3D positionM;   // metres; view space X=right, Y=up, Z=depth
    int       unitNumber = 0;
    int       dmxAddress = 0;
};

// ─── Rich structures for full MVR parse ───────────────────────────────────────

// Triangle mesh: flat list of vertices (every 3 = one triangle, no index buffer)
struct MvrMesh {
    QVector<QVector3D> vertices;   // positions, 3 per triangle
    QVector<QVector3D> normals;    // per-vertex normals (same count); may be empty
    QColor             color = QColor(180, 180, 180);
};

struct MvrObject {
    enum class Type { Fixture, SceneObject, Truss, Group, Unknown };

    QString          name;
    Type             type      = Type::Unknown;
    QVector<MvrMesh> meshes;
    QVector3D        positionM;  // centre in metres, view space (fixtures/groups)
    QString          gdtfSpec;
    int              unitNumber = 0;
    int              dmxAddress = 0;
    bool             enabled    = true;
};

struct MvrLayer {
    QString          name;
    QList<MvrObject> objects;
    bool             enabled = true;
};

// ─── Top-level import container ───────────────────────────────────────────────

struct MvrImport {
    QString         name;           // display name (defaults to filename)
    QList<MvrLayer> layers;
    float           offsetX = 0.f; // metres, view X (right)
    float           offsetY = 0.f; // metres, view Y (up)
    float           offsetZ = 0.f; // metres, view Z (depth)
    float           rotDeg  = 0.f; // degrees, CCW around view Y axis
    bool            enabled = true; // visibility in 3D
};

// ─── Importer ─────────────────────────────────────────────────────────────────

class MvrImporter {
public:
    // Legacy: fixture-only import (calls parse() internally)
    struct Result {
        QList<MvrFixture> fixtures;
        QString           error;
    };
    static Result import(const QString& filePath);

    // Full parse: layer → object hierarchy with optional GLB geometry
    struct ParseResult {
        QList<MvrLayer> layers;
        QString         error;
    };
    static ParseResult parse(const QString& filePath);
};
