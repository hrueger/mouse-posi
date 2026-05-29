#include "MvrImporter.h"

#include <QFile>
#include <QMatrix4x4>
#include <QtCore/private/qzipreader_p.h>

#include "Include/VectorworksMVR.h"
#include "cgltf.h"

using namespace VectorworksMVR;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static QString toQString(MvrString text)
{
    return text ? QString::fromUtf8(text) : QString();
}

static MvrObject::Type typeFromSceneType(ESceneObjType type)
{
    switch (type) {
    case ESceneObjType::Fixture:     return MvrObject::Type::Fixture;
    case ESceneObjType::Truss:       return MvrObject::Type::Truss;
    case ESceneObjType::Group:       return MvrObject::Type::Group;
    case ESceneObjType::SceneObj:
    case ESceneObjType::FocusPoint:
    case ESceneObjType::VideoScreen:
    case ESceneObjType::Projector:
    case ESceneObjType::Support:
        return MvrObject::Type::SceneObject;
    default:
        return MvrObject::Type::Unknown;
    }
}

// Build QMatrix4x4 that transforms a GLTF Y-up local vertex (metres) to
// view space (metres), given an MVR transform matrix.
static QMatrix4x4 mvrToViewMatrix(const STransformMatrix& mx)
{
    // libMVRgdtf provides an STransformMatrix (u,v,w,o) in MVR coordinates.
    // MVR coordinates (Vectorworks): X-right, Y-forward, Z-up (RH).
    // Onpoint view space:            X-right, Y-up,      Z-depth.
    //
    // Coordinate change for vectors/points: (x,y,z)_mvr -> (x, z, -y)_view.
    // For a full transform matrix, we must change basis:
    //   M_view = C * M_mvr * C^-1
    // where C maps MVR vectors into view vectors.
    //
    // Vectorworks uses the row-vector convention (p' = p * M) and stores the
    // basis-vector images u,v,w as the *rows* of that matrix. In the standard
    // column-vector convention used by QMatrix4x4 (p' = M * p), the images of
    // the local X/Y/Z axes are the *columns*, so u,v,w become the columns here.
    const QMatrix4x4 mvrM(
        float(mx.ux), float(mx.vx), float(mx.wx), float(mx.ox * 0.001),
        float(mx.uy), float(mx.vy), float(mx.wy), float(mx.oy * 0.001),
        float(mx.uz), float(mx.vz), float(mx.wz), float(mx.oz * 0.001),
        0.f,          0.f,          0.f,          1.f
    );

    const QMatrix4x4 C(
        1.f,  0.f,  0.f,  0.f,
        0.f,  0.f,  1.f,  0.f,
        0.f, -1.f,  0.f,  0.f,
        0.f,  0.f,  0.f,  1.f
    );

    return C * mvrM * C.inverted();
}

static QVector3D positionFromMatrix(const STransformMatrix& mx)
{
    // Keep consistent with mvrToViewMatrix() translation mapping.
    // (x,y,z)_mvr -> (x, z, -y)_view
    return QVector3D(
        float(mx.ox * 0.001),
        float(mx.oz * 0.001),
        float(-mx.oy * 0.001)
    );
}

// ─── GLB loading ──────────────────────────────────────────────────────────────

static QVector<MvrMesh> loadGlb(const QByteArray& data, const QMatrix4x4& xform)
{
    QVector<MvrMesh> result;
    if (data.isEmpty()) return result;

    cgltf_options opts{};
    cgltf_data* gltf = nullptr;
    if (cgltf_parse(&opts, data.constData(), size_t(data.size()), &gltf)
            != cgltf_result_success)
        return result;

    if (cgltf_load_buffers(&opts, gltf, nullptr) != cgltf_result_success) {
        cgltf_free(gltf);
        return result;
    }

    auto processPrim = [&](const cgltf_primitive& prim, const QMatrix4x4& combined) {
        if (prim.type != cgltf_primitive_type_triangles &&
            prim.type != cgltf_primitive_type_triangle_strip &&
            prim.type != cgltf_primitive_type_triangle_fan)
            return;

        cgltf_accessor* posAcc  = nullptr;
        cgltf_accessor* normAcc = nullptr;
        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
            if (prim.attributes[ai].type == cgltf_attribute_type_position)
                posAcc = prim.attributes[ai].data;
            else if (prim.attributes[ai].type == cgltf_attribute_type_normal)
                normAcc = prim.attributes[ai].data;
        }
        if (!posAcc) return;

        QMatrix3x3 normMat = combined.normalMatrix();
        MvrMesh mvrMesh;

        auto readVertex = [&](cgltf_size idx) {
            float p[3] = {};
            cgltf_accessor_read_float(posAcc, idx, p, 3);
            mvrMesh.vertices.append(combined.map(QVector3D(p[0], p[1], p[2])));
            if (normAcc) {
                float n[3] = {};
                cgltf_accessor_read_float(normAcc, idx, n, 3);
                const QVector3D wn(
                    normMat(0,0)*n[0] + normMat(0,1)*n[1] + normMat(0,2)*n[2],
                    normMat(1,0)*n[0] + normMat(1,1)*n[1] + normMat(1,2)*n[2],
                    normMat(2,0)*n[0] + normMat(2,1)*n[1] + normMat(2,2)*n[2]
                );
                mvrMesh.normals.append(wn.normalized());
            }
        };

        const cgltf_size indexCount = prim.indices ? prim.indices->count : posAcc->count;
        auto indexAt = [&](cgltf_size k) -> cgltf_size {
            if (prim.indices) {
                cgltf_uint idx = 0;
                cgltf_accessor_read_uint(prim.indices, k, &idx, 1);
                return cgltf_size(idx);
            }
            return k;
        };

        switch (prim.type) {
        case cgltf_primitive_type_triangles:
            for (cgltf_size k = 0; k + 2 < indexCount; k += 3) {
                readVertex(indexAt(k));
                readVertex(indexAt(k + 1));
                readVertex(indexAt(k + 2));
            }
            break;
        case cgltf_primitive_type_triangle_strip:
            for (cgltf_size k = 2; k < indexCount; ++k) {
                readVertex(indexAt(k - 2));
                readVertex(indexAt(k - 1));
                readVertex(indexAt(k));
            }
            break;
        case cgltf_primitive_type_triangle_fan:
            for (cgltf_size k = 2; k < indexCount; ++k) {
                readVertex(indexAt(0));
                readVertex(indexAt(k - 1));
                readVertex(indexAt(k));
            }
            break;
        default: break;
        }

        if (!mvrMesh.vertices.isEmpty())
            result.append(std::move(mvrMesh));
    };

    // Traverse node hierarchy so embedded node transforms (e.g. Z-up conversion
    // nodes exported by some tools) are applied correctly.
    if (gltf->nodes_count > 0) {
        for (cgltf_size ni = 0; ni < gltf->nodes_count; ++ni) {
            cgltf_node* node = &gltf->nodes[ni];
            if (!node->mesh) continue;

            // cgltf_node_transform_world returns column-major float[16]
            float mat[16];
            cgltf_node_transform_world(node, mat);
            const QMatrix4x4 nodeXform(
                mat[0], mat[4], mat[8],  mat[12],
                mat[1], mat[5], mat[9],  mat[13],
                mat[2], mat[6], mat[10], mat[14],
                mat[3], mat[7], mat[11], mat[15]
            );
            const QMatrix4x4 combined = xform * nodeXform;
            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
                processPrim(node->mesh->primitives[pi], combined);
        }
    } else {
        for (cgltf_size mi = 0; mi < gltf->meshes_count; ++mi)
            for (cgltf_size pi = 0; pi < gltf->meshes[mi].primitives_count; ++pi)
                processPrim(gltf->meshes[mi].primitives[pi], xform);
    }

    cgltf_free(gltf);
    return result;
}

// ─── Scene traversal ────────────────────────────────────────────────────────

static void appendGeometryFromReference(IGeometryReference* geomRef,
                                        const QMatrix4x4& xform,
                                        const QZipReader& zip,
                                        MvrObject& outObject);

static void appendGeometryFromSymDef(ISymDef* symDef,
                                     const QMatrix4x4& xform,
                                     const QZipReader& zip,
                                     MvrObject& outObject)
{
    if (!symDef) return;

    size_t geometryCount = 0;
    if (symDef->GetGeometryCount(geometryCount) != kVCOMError_NoError) return;

    for (size_t index = 0; index < geometryCount; ++index) {
        IGeometryReferencePtr geomRef;
        if (symDef->GetGeometryAt(index, &geomRef) != kVCOMError_NoError || !geomRef) {
            continue;
        }
        appendGeometryFromReference(geomRef, xform, zip, outObject);
    }
}

static void appendGeometryFromReference(IGeometryReference* geomRef,
                                        const QMatrix4x4& xform,
                                        const QZipReader& zip,
                                        MvrObject& outObject)
{
    if (!geomRef) return;

    QMatrix4x4 localXform = xform;
    STransformMatrix geomMatrix{};
    if (geomRef->GetTransfromMatrix(geomMatrix) == kVCOMError_NoError) {
        localXform = xform * mvrToViewMatrix(geomMatrix);
    }

    bool isSymbol = false;
    if (geomRef->GetIsSymbol(isSymbol) != kVCOMError_NoError) return;

    if (isSymbol) {
        ISymDefPtr symDef;
        if (geomRef->GetSymDef(&symDef) == kVCOMError_NoError && symDef) {
            appendGeometryFromSymDef(symDef, localXform, zip, outObject);
        }
        return;
    }

    const QString fileName = toQString(geomRef->GetFileForGeometry());
    if (fileName.isEmpty()) return;

    QByteArray glbData;
    QFile geometryFile(fileName);
    if (geometryFile.open(QIODevice::ReadOnly)) {
        glbData = geometryFile.readAll();
    } else {
        glbData = zip.fileData(fileName);
    }
    for (MvrMesh& mesh : loadGlb(glbData, localXform)) {
        outObject.meshes.append(std::move(mesh));
    }
}

static void appendGeometryFromSceneObject(ISceneObj* sceneObj,
                                          const QMatrix4x4& xform,
                                          const QZipReader& zip,
                                          MvrObject& outObject)
{
    if (!sceneObj) return;

    size_t geometryCount = 0;
    if (sceneObj->GetGeometryCount(geometryCount) != kVCOMError_NoError) return;

    for (size_t index = 0; index < geometryCount; ++index) {
        IGeometryReferencePtr geomRef;
        if (sceneObj->GetGeometryAt(index, &geomRef) != kVCOMError_NoError || !geomRef) {
            continue;
        }
        appendGeometryFromReference(geomRef, xform, zip, outObject);
    }
}

static MvrObject readSceneObject(ISceneObj* sceneObj,
                                 const QZipReader& zip)
{
    MvrObject outObject;
    if (!sceneObj) return outObject;

    ESceneObjType sceneType;
    if (sceneObj->GetType(sceneType) != kVCOMError_NoError) {
        return outObject;
    }

    outObject.type = typeFromSceneType(sceneType);
    outObject.name = toQString(sceneObj->GetName());

    STransformMatrix matrix{};
    if (sceneObj->GetTransfromMatrix(matrix) == kVCOMError_NoError) {
        const QMatrix4x4 xform = mvrToViewMatrix(matrix);
        outObject.positionM = positionFromMatrix(matrix);
        appendGeometryFromSceneObject(sceneObj, xform, zip, outObject);
    }

    if (outObject.type == MvrObject::Type::Fixture) {
        outObject.gdtfSpec = toQString(sceneObj->GetGdtfName());

        Sint32 unitNumber = 0;
        if (sceneObj->GetUnitNumber(unitNumber) == kVCOMError_NoError) {
            outObject.unitNumber = int(unitNumber);
        }

        size_t addressCount = 0;
        if (sceneObj->GetAdressCount(addressCount) == kVCOMError_NoError && addressCount > 0) {
            SDmxAdress address{};
            if (sceneObj->GetAdressAt(0, address) == kVCOMError_NoError) {
                outObject.dmxAddress = int(address.fAbsuluteAdress);
            }
        }
    }

    return outObject;
}

static void collectLayerObjects(IMediaRessourceVectorInterfacePtr& mvr,
                                ISceneObj* firstObject,
                                QList<MvrObject>& out,
                                const QZipReader& zip)
{
    ISceneObjPtr object = firstObject;
    while (object) {
        MvrObject current = readSceneObject(object, zip);
        ISceneObjPtr child;
        const bool hasChildren = (mvr->GetFirstChild(object, &child) == kVCOMError_NoError && child);

        if (current.type != MvrObject::Type::Unknown) {
            if (current.type == MvrObject::Type::Group && hasChildren && current.meshes.isEmpty()) {
                // Keep empty groups out of the render tree, but continue traversing.
            } else {
                out.append(std::move(current));
            }
        }

        if (hasChildren) {
            collectLayerObjects(mvr, child, out, zip);
        }

        ISceneObjPtr next;
        if (mvr->GetNextObject(object, &next) == kVCOMError_NoError && next) {
            object = next;
        } else {
            break;
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

MvrImporter::ParseResult MvrImporter::parse(const QString& filePath)
{
    ParseResult result;

    IMediaRessourceVectorInterfacePtr mvr(IID_MediaRessourceVectorInterface);
    if (!mvr) {
        result.error = QStringLiteral("Cannot create libMVRgdtf interface");
        return result;
    }

    const QByteArray utf8Path = filePath.toUtf8();
    if (mvr->OpenForRead(utf8Path.constData()) != kVCOMError_NoError) {
        result.error = QStringLiteral("libMVRgdtf failed to open the MVR file");
        return result;
    }

    QZipReader zip(filePath);
    if (zip.status() != QZipReader::NoError) {
        result.error = QStringLiteral("Cannot open MVR file contents");
        return result;
    }

    ISceneObjPtr layer;
    if (mvr->GetFirstLayer(&layer) != kVCOMError_NoError) {
        result.error = QStringLiteral("libMVRgdtf failed to read the scene layers");
        return result;
    }

    while (layer) {
        MvrLayer layerInfo;
        layerInfo.name = toQString(layer->GetName());

        ISceneObjPtr child;
        if (mvr->GetFirstChild(layer, &child) == kVCOMError_NoError && child) {
            collectLayerObjects(mvr, child, layerInfo.objects, zip);
        }

        result.layers.append(layerInfo);

        ISceneObjPtr nextLayer;
        if (mvr->GetNextObject(layer, &nextLayer) == kVCOMError_NoError && nextLayer) {
            layer = nextLayer;
        } else {
            break;
        }
    }

    return result;
}

MvrImporter::Result MvrImporter::import(const QString& filePath)
{
    Result result;
    const ParseResult pr = parse(filePath);
    result.error = pr.error;
    for (const MvrLayer& layer : pr.layers) {
        for (const MvrObject& obj : layer.objects) {
            if (obj.type != MvrObject::Type::Fixture) continue;
            MvrFixture f;
            f.name       = obj.name;
            f.gdtfSpec   = obj.gdtfSpec;
            f.positionM  = obj.positionM;
            f.unitNumber = obj.unitNumber;
            f.dmxAddress = obj.dmxAddress;
            result.fixtures.append(f);
        }
    }
    return result;
}
