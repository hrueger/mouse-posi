#include "MvrImporter.h"

#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QTemporaryFile>
#include <archive.h>
#include <archive_entry.h>
#include <unordered_map>

#include "Include/VectorworksMVR.h"
#include "cgltf.h"

using namespace VectorworksMVR;

// ─── ZIP file reading (libarchive) ───────────────────────────────────────────

static QByteArray readFileFromZip(const QString& zipPath, const QString& fileName)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    const QByteArray utf8ZipPath = zipPath.toUtf8();
    const QByteArray utf8FileName = fileName.toUtf8();

    if (archive_read_open_filename(a, utf8ZipPath.constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return QByteArray();
    }

    QByteArray result;
    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_pathname(entry) == utf8FileName) {
            size_t size = archive_entry_size(entry);
            char* buffer = new char[size];
            if (archive_read_data(a, buffer, size) > 0) {
                result = QByteArray::fromRawData(buffer, size);
                result.detach();
            }
            delete[] buffer;
            break;
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);
    return result;
}

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
                                        const QString& zipPath,
                                        MvrObject& outObject);

static void appendGeometryFromSymDef(ISymDef* symDef,
                                     const QMatrix4x4& xform,
                                     const QString& zipPath,
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
        appendGeometryFromReference(geomRef, xform, zipPath, outObject);
    }
}

static void appendGeometryFromReference(IGeometryReference* geomRef,
                                        const QMatrix4x4& xform,
                                        const QString& zipPath,
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
            appendGeometryFromSymDef(symDef, localXform, zipPath, outObject);
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
        glbData = readFileFromZip(zipPath, fileName);
    }
    for (MvrMesh& mesh : loadGlb(glbData, localXform)) {
        outObject.meshes.append(std::move(mesh));
    }
}

static void appendGeometryFromSceneObject(ISceneObj* sceneObj,
                                          const QMatrix4x4& xform,
                                          const QString& zipPath,
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
        appendGeometryFromReference(geomRef, xform, zipPath, outObject);
    }
}

// ─── GDTF geometry traversal ─────────────────────────────────────────────────

// Per-geometry rotation derived from MVR CustomCommands.
struct GeomRot { QVector3D axis; float angle; };
using GeomRotMap = std::unordered_map<std::string, GeomRot>;

// Parse CustomCommands on the scene object into a geomName → rotation map.
// The command channel-function string encodes the geometry and attribute:
//   "{GeomName}_{AttrName}.{AttrName}.{FnName}"
// Physical values are in degrees for angle attributes.
// We only handle Pan (→ local Z axis) and Tilt (→ local X axis) here;
// other attributes (blades, zoom, etc.) are not relevant for body pose rendering.
static GeomRotMap buildCustomCommandMap(ISceneObj* sceneObj)
{
    GeomRotMap result;
    size_t cmdCount = 0;
    if (sceneObj->GetCustomCommandCount(cmdCount) != kVCOMError_NoError || cmdCount == 0)
        return result;

    for (size_t i = 0; i < cmdCount; ++i) {
        ICustomCommandPtr cmd;
        if (sceneObj->GetCustomCommandAt(i, &cmd) != kVCOMError_NoError || !cmd) continue;

        bool isPct = false;
        if (cmd->IsPercentage(isPct) != kVCOMError_NoError || isPct) continue;

        double value = 0.0;
        if (cmd->GetValue(value) != kVCOMError_NoError) continue;

        // Parse "{GeomName}_{AttrName}.{AttrName}...." → geomName, attrName
        const QString chanFn = toQString(cmd->GetChannelFunction());
        const int dot = chanFn.indexOf('.');
        if (dot < 0) continue;
        const QString geomAttr = chanFn.left(dot);
        const QString attrName = chanFn.mid(dot + 1, chanFn.indexOf('.', dot + 1) - dot - 1);
        const QString sep = '_' + attrName;
        if (!geomAttr.endsWith(sep)) continue;
        const QString geomName = geomAttr.left(geomAttr.length() - sep.length());

        QVector3D axis;
        if (attrName == QStringLiteral("Pan"))
            axis = QVector3D(0.0f, 0.0f, 1.0f); // pan: vertical axis (local Z)
        else if (attrName == QStringLiteral("Tilt"))
            axis = QVector3D(1.0f, 0.0f, 0.0f); // tilt: horizontal axis (local X)
        else
            continue;

        result[geomName.toStdString()] = { axis, float(value) };
    }
    return result;
}

static void appendGeometryFromGdtfNode(IGdtfGeometry* geom,
                                       const QMatrix4x4& parentXform,
                                       const GeomRotMap& rotMap,
                                       MvrObject& outObject)
{
    if (!geom) return;

    STransformMatrix geomMat{};
    QMatrix4x4 localXform = parentXform;
    if (geom->GetTransformMatrix(geomMat) == kVCOMError_NoError)
        localXform = parentXform * mvrToViewMatrix(geomMat);

    // Apply MVR custom-command rotation for this geometry (in local space).
    const std::string name = toQString(geom->GetName()).toStdString();
    auto it = rotMap.find(name);
    if (it != rotMap.end() && it->second.angle != 0.0f) {
        QMatrix4x4 rot;
        rot.rotate(it->second.angle, it->second.axis);
        localXform = localXform * rot;
    }

    IGdtfModelPtr model;
    if (geom->GetModel(&model) == kVCOMError_NoError && model) {
        void* buf = nullptr;
        size_t len = 0;
        if (model->GetBufferGLTF(&buf, len) == kVCOMError_NoError && buf && len > 0) {
            QByteArray glbData(static_cast<const char*>(buf), int(len));
            for (MvrMesh& mesh : loadGlb(glbData, localXform))
                outObject.meshes.append(std::move(mesh));
        }
    }

    size_t childCount = 0;
    if (geom->GetInternalGeometryCount(childCount) == kVCOMError_NoError) {
        for (size_t i = 0; i < childCount; ++i) {
            IGdtfGeometryPtr child;
            if (geom->GetInternalGeometryAt(i, &child) == kVCOMError_NoError && child)
                appendGeometryFromGdtfNode(child, localXform, rotMap, outObject);
        }
    }
}

// ─── GDTF DMX profile extraction (pan/tilt channels) ────────────────────────

static GdtfDmxProfile extractDmxProfile(IGdtfFixture* fixture, const QString& activeModeName)
{
    GdtfDmxProfile profile;
    if (!fixture) return profile;

    size_t modeCount = 0;
    if (fixture->GetDmxModeCount(modeCount) != kVCOMError_NoError || modeCount == 0)
        return profile;

    // Find the mode the MVR scene requests; fall back to mode 0.
    size_t modeIdx = 0;
    if (!activeModeName.isEmpty()) {
        for (size_t mi = 0; mi < modeCount; ++mi) {
            IGdtfDmxModePtr m;
            if (fixture->GetDmxModeAt(mi, &m) == kVCOMError_NoError && m) {
                if (toQString(m->GetName()) == activeModeName) {
                    modeIdx = mi;
                    break;
                }
            }
        }
    }

    IGdtfDmxModePtr mode;
    if (fixture->GetDmxModeAt(modeIdx, &mode) != kVCOMError_NoError || !mode)
        return profile;

    profile.modeName = toQString(mode->GetName());

    size_t breakCount = 0;
    mode->GetBreakCount(breakCount);
    if (breakCount > 0) {
        size_t fp = 0;
        mode->GetFootprintForBreak(0, fp);
        profile.footprint = int(fp);
    }

    size_t channelCount = 0;
    if (mode->GetDmxChannelCount(channelCount) != kVCOMError_NoError)
        return profile;

    int maxSlot = 0;
    for (size_t ci = 0; ci < channelCount; ++ci) {
        IGdtfDmxChannelPtr channel;
        if (mode->GetDmxChannelAt(ci, &channel) != kVCOMError_NoError || !channel)
            continue;

        Sint32 coarse = -1, fine = -1;
        channel->GetCoarse(coarse);
        channel->GetFine(fine);
        if (coarse < 0) continue;
        maxSlot = qMax(maxSlot, int(coarse));
        if (fine >= 0) maxSlot = qMax(maxSlot, int(fine));

        size_t logCount = 0;
        if (channel->GetLogicalChannelCount(logCount) != kVCOMError_NoError || logCount == 0)
            continue;

        IGdtfDmxLogicalChannelPtr logCh;
        if (channel->GetLogicalChannelAt(0, &logCh) != kVCOMError_NoError || !logCh)
            continue;

        IGdtfAttributePtr attr;
        if (logCh->GetAttribute(&attr) != kVCOMError_NoError || !attr)
            continue;

        const QString attrName = toQString(attr->GetName());
        const bool isPan  = (attrName == QStringLiteral("Pan"));
        const bool isTilt = (attrName == QStringLiteral("Tilt"));
        if (!isPan && !isTilt) continue;

        GdtfChannelInfo info;
        info.address = int(coarse);    // 1-based offset within mode footprint
        info.address2 = (fine >= 0) ? int(fine) : -1;
        info.is16bit  = (fine >= 0);

        // Physical range from the first/default channel function
        size_t fnCount = 0;
        if (logCh->GetDmxFunctionCount(fnCount) == kVCOMError_NoError && fnCount > 0) {
            IGdtfDmxChannelFunctionPtr fn;
            if (logCh->GetDmxFunctionAt(0, &fn) == kVCOMError_NoError && fn) {
                double from = -270.0, to = 270.0;
                fn->GetPhysicalStart(from);
                fn->GetPhysicalEnd(to);
                info.minDeg = float(from);
                info.maxDeg = float(to);
            }
        }

        if (isPan)  profile.pan  = info;
        else        profile.tilt = info;
    }

    if (profile.footprint == 0 && maxSlot > 0)
        profile.footprint = maxSlot;

    // Valid as long as the GDTF was readable (has at least one mode)
    profile.valid = true;
    return profile;
}

static void appendGeometryFromGdtfFixture(ISceneObj* sceneObj,
                                          const QMatrix4x4& fixtureXform,
                                          MvrObject& outObject)
{
    IGdtfFixturePtr gdtfFixture;
    if (sceneObj->GetGdtfFixture(&gdtfFixture) != kVCOMError_NoError || !gdtfFixture)
        return;

    const GeomRotMap rotMap = buildCustomCommandMap(sceneObj);

    size_t geomCount = 0;
    if (gdtfFixture->GetGeometryCount(geomCount) != kVCOMError_NoError) return;

    for (size_t i = 0; i < geomCount; ++i) {
        IGdtfGeometryPtr geom;
        if (gdtfFixture->GetGeometryAt(i, &geom) == kVCOMError_NoError && geom)
            appendGeometryFromGdtfNode(geom, fixtureXform, rotMap, outObject);
    }
}

static MvrObject readSceneObject(ISceneObj* sceneObj,
                                 const QString& zipPath)
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
        // Store rotation part of the transform (zero translation so mapVector works correctly).
        outObject.xformRot = xform;
        outObject.xformRot(0, 3) = 0.f;
        outObject.xformRot(1, 3) = 0.f;
        outObject.xformRot(2, 3) = 0.f;
        appendGeometryFromSceneObject(sceneObj, xform, zipPath, outObject);
        if (outObject.type == MvrObject::Type::Fixture && outObject.meshes.isEmpty())
            appendGeometryFromGdtfFixture(sceneObj, xform, outObject);
    }

    if (outObject.type == MvrObject::Type::Fixture) {
        outObject.gdtfSpec = toQString(sceneObj->GetGdtfName());

        Sint32 unitNumber = 0;
        if (sceneObj->GetUnitNumber(unitNumber) == kVCOMError_NoError)
            outObject.unitNumber = int(unitNumber);
        outObject.fixtureId = toQString(sceneObj->GetFixtureId());

        size_t addressCount = 0;
        if (sceneObj->GetAdressCount(addressCount) == kVCOMError_NoError && addressCount > 0) {
            SDmxAdress address{};
            if (sceneObj->GetAdressAt(0, address) == kVCOMError_NoError) {
                // fAbsuluteAdress is 1-based across the whole DMX space (U1.C1=1, U2.C1=513, …)
                const int absAddr = int(address.fAbsuluteAdress);
                if (absAddr > 0) {
                    outObject.universe   = (absAddr - 1) / 512 + 1;
                    outObject.dmxAddress = (absAddr - 1) % 512 + 1; // 1-based within universe
                } else if (address.fBreakId > 0) {
                    outObject.universe   = int(address.fBreakId);
                    outObject.dmxAddress = 1;
                }
            }
        }

        IGdtfFixturePtr gdtfFixture;
        if (sceneObj->GetGdtfFixture(&gdtfFixture) == kVCOMError_NoError && gdtfFixture) {
            const QString mvrMode = toQString(sceneObj->GetGdtfMode());
            outObject.gdtfProfile = extractDmxProfile(gdtfFixture, mvrMode);
        }
    }

    return outObject;
}

static void collectLayerObjects(IMediaRessourceVectorInterfacePtr& mvr,
                                ISceneObj* firstObject,
                                QList<MvrObject>& out,
                                const QString& zipPath,
                                const std::function<void()>& tickCb)
{
    ISceneObjPtr object = firstObject;
    while (object) {
        MvrObject current = readSceneObject(object, zipPath);
        if (tickCb) tickCb();  // yield to the event loop between objects

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
            collectLayerObjects(mvr, child, out, zipPath, tickCb);
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

MvrImporter::ParseResult MvrImporter::parse(const QString& filePath,
                                             std::function<void()> tickCb)
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
            collectLayerObjects(mvr, child, layerInfo.objects, filePath, tickCb);
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

MvrImporter::ParseResult MvrImporter::parseFromData(const QByteArray& data,
                                                     std::function<void()> tickCb)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        ParseResult r;
        r.error = QStringLiteral("Cannot create temporary file for MVR parsing");
        return r;
    }
    tmp.write(data);
    tmp.flush();
    return parse(tmp.fileName(), tickCb);
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

// Generate a simple axis-aligned box mesh (12 triangles) for fallback preview.
static QVector<MvrMesh> makeBoxMesh(float w, float h, float d)
{
    // Clamp to sane defaults
    if (w < 0.01f) w = 0.3f;
    if (h < 0.01f) h = 0.6f;
    if (d < 0.01f) d = 0.3f;

    const float hw = w * 0.5f, hd = d * 0.5f;
    // Centred at origin, Y from 0..h (fixture stands on floor)
    struct Face { QVector3D v[4]; QVector3D n; };
    const Face faces[] = {
        { { {-hw,0,hd},{hw,0,hd},{hw,h,hd},{-hw,h,hd} }, {0,0,1} },   // front
        { { {hw,0,-hd},{-hw,0,-hd},{-hw,h,-hd},{hw,h,-hd} }, {0,0,-1} }, // back
        { { {-hw,0,-hd},{-hw,0,hd},{-hw,h,hd},{-hw,h,-hd} }, {-1,0,0} }, // left
        { { {hw,0,hd},{hw,0,-hd},{hw,h,-hd},{hw,h,hd} }, {1,0,0} },   // right
        { { {-hw,h,hd},{hw,h,hd},{hw,h,-hd},{-hw,h,-hd} }, {0,1,0} }, // top
        { { {hw,0,hd},{-hw,0,hd},{-hw,0,-hd},{hw,0,-hd} }, {0,-1,0} }, // bottom
    };

    MvrMesh mesh;
    for (const auto& f : faces) {
        // Two triangles per face
        const int idx[6] = {0,1,2, 0,2,3};
        for (int i : idx) {
            mesh.vertices.append(f.v[i]);
            mesh.normals.append(f.n);
        }
    }

    return { mesh };
}

QVector<MvrMesh> MvrImporter::loadGdtfMeshes(const QString& gdtfPath)
{
    IGdtfFixturePtr fix(IID_IGdtfFixture);
    if (!fix) return {};
    const QByteArray p = gdtfPath.toUtf8();
    if (fix->ReadFromFile(p.constData()) != kVCOMError_NoError) return {};

    MvrObject outObject;
    const GeomRotMap emptyRot;

    size_t geomCount = 0;
    if (fix->GetGeometryCount(geomCount) != kVCOMError_NoError) return {};

    for (size_t i = 0; i < geomCount; ++i) {
        IGdtfGeometryPtr geom;
        if (fix->GetGeometryAt(i, &geom) == kVCOMError_NoError && geom)
            appendGeometryFromGdtfNode(geom, QMatrix4x4(), emptyRot, outObject);
    }

    if (!outObject.meshes.isEmpty())
        return outObject.meshes;

    // No GLB geometry found (fixture may only have 3DS models or no geometry).
    // Fall back to a box built from the first model's dimensions, if available.
    size_t modelCount = 0;
    if (fix->GetModelCount(modelCount) == kVCOMError_NoError && modelCount > 0) {
        IGdtfModelPtr model;
        if (fix->GetModelAt(0, &model) == kVCOMError_NoError && model) {
            double w = 0.3, h = 0.6, d = 0.3;
            model->GetWidth(w);
            model->GetHeight(h);
            model->GetLength(d);
            // GDTF model dimensions are in millimetres
            return makeBoxMesh(float(w * 0.001), float(h * 0.001), float(d * 0.001));
        }
    }

    return {};
}

QVector<MvrMesh> MvrImporter::loadGdtfMeshesFromData(const QByteArray& gdtfData)
{
    QTemporaryFile tmp;
    tmp.setFileTemplate(QDir::tempPath() + "/onpoint_gdtf_XXXXXX.gdtf");
    tmp.setAutoRemove(true);
    if (!tmp.open()) return {};
    tmp.write(gdtfData);
    tmp.flush();
    return loadGdtfMeshes(tmp.fileName());
}
