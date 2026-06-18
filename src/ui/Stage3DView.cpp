#include "Stage3DView.h"
#include <QOpenGLContext>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>
#include <cmath>
#include <algorithm>

// ─── Shader sources ──────────────────────────────────────────────────────────

// Flat-color shader (grid, trackers, stage objects, fixture crosses)
static const char* kVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)";

static const char* kFragSrc = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";

// Phong-lit shader for MVR geometry (position + normal interleaved)
static const char* kLitVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vNormal;
out vec3 vFragPos;
void main() {
    vFragPos  = aPos;
    vNormal   = aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kLitFragSrc = R"(
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
uniform vec4 uColor;
uniform vec3 uLightDir;   // normalised, world space
uniform vec3 uViewPos;    // camera position, world space
out vec4 fragColor;
void main() {
    // Flip normal for back-facing fragments (renders inner walls / single-sided
    // architectural surfaces correctly when viewed from outside).
    vec3 norm = normalize(gl_FrontFacing ? vNormal : -vNormal);
    float wrap  = (dot(norm, uLightDir) + 0.4) / 1.4;
    float light = 0.18 + 0.72 * max(wrap, 0.0);

    vec3 viewDir = normalize(uViewPos - vFragPos);
    vec3 halfDir = normalize(uLightDir + viewDir);
    float spec   = pow(max(dot(norm, halfDir), 0.0), 32.0);

    vec3 base  = uColor.rgb * light + vec3(0.06) * spec;
    fragColor  = vec4(base, uColor.a);
}
)";

// ─── Construction / destruction ───────────────────────────────────────────────

Stage3DView::Stage3DView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // Format is set globally in main() via QSurfaceFormat::setDefaultFormat()
}

Stage3DView::~Stage3DView()
{
    cleanup();
}

void Stage3DView::cleanup()
{
    if (context())
        disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Stage3DView::cleanup);
    makeCurrent();
    if (vbo_.isCreated())    vbo_.destroy();
    if (vao_.isCreated())    vao_.destroy();
    if (litVbo_.isCreated()) litVbo_.destroy();
    if (litVao_.isCreated()) litVao_.destroy();
    if (!mvrGpuCache_.empty()) {
        for (auto &kv : mvrGpuCache_) {
            auto &g = kv.second;
            if (g->vbo.isCreated())     g->vbo.destroy();
            if (g->vao.isCreated())     g->vao.destroy();
            if (g->lineIbo.isCreated()) g->lineIbo.destroy();
            if (g->lineVao.isCreated()) g->lineVao.destroy();
        }
        mvrGpuCache_.clear();
    }
    delete shader_;    shader_    = nullptr;
    delete litShader_; litShader_ = nullptr;
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void Stage3DView::setCalibration(const Calibration* c, const QList<QPointF>& stagePoints)
{
    calibration_       = c;
    calibStagePoints_  = stagePoints;
    update();
}

void Stage3DView::setStageObjects(const QList<StageObject>& objs)
{
    stageObjects_ = objs;
    update();
}

void Stage3DView::setTrackerPositions(const QMap<int, QPair<float,float>>& pos,
                                      const QList<TrackerConfig>& trackers)
{
    trackerPositions_ = pos;
    trackers_         = trackers;
    update();
}

void Stage3DView::setMvrImports(const QList<MvrImport>& imports)
{
    mvrImports_ = imports;

    // Pre-size litVbo_ to the largest mesh across all imports
    if (litVbo_.isCreated()) {
        int maxBytes = 0;
        for (const auto& import : imports)
            for (const auto& layer : import.layers)
                for (const auto& obj : layer.objects)
                    for (const auto& mesh : obj.meshes) {
                        const int bytes = mesh.vertices.size() * 6 * int(sizeof(float));
                        if (bytes > maxBytes) maxBytes = bytes;
                    }
        if (maxBytes > litVboCapacity_) {
            makeCurrent();
            litVao_.bind();
            litVbo_.bind();
            litVbo_.allocate(maxBytes);   // one-time resize — not per-frame
            litVboCapacity_ = maxBytes;
            litVbo_.release();
            litVao_.release();
            doneCurrent();
        }
    }

    // Build GPU-side VBO/VAO cache for MVR meshes across all imports
    if (litVao_.isCreated()) {
        // Clear any previous cache
        if (!mvrGpuCache_.empty()) {
            makeCurrent();
            for (auto &kv : mvrGpuCache_) {
                auto &g = kv.second;
                if (g->vbo.isCreated())     g->vbo.destroy();
                if (g->vao.isCreated())     g->vao.destroy();
                if (g->lineIbo.isCreated()) g->lineIbo.destroy();
                if (g->lineVao.isCreated()) g->lineVao.destroy();
            }
            mvrGpuCache_.clear();
            doneCurrent();
        }

        // Create GPU buffers for each mesh from all imports
        makeCurrent();
        for (const auto &import : imports) {
            for (const auto &layer : import.layers) {
                for (const auto &obj : layer.objects) {
                    for (const auto &mesh : obj.meshes) {
                        const MvrMesh* key = &mesh;
                        if (mvrGpuCache_.find(key) != mvrGpuCache_.end()) continue;
                        auto gm = std::make_unique<GpuMvrMesh>();
                        // Interleaved pos(3)+normal(3)
                        const int n = mesh.vertices.size();
                        QVector<float> buf;
                        buf.reserve(n * 6);
                        const bool hasNormals = (mesh.normals.size() == n);
                        for (int i = 0; i < n; ++i) {
                            const QVector3D& p = mesh.vertices[i];
                            const QVector3D nm = hasNormals ? mesh.normals[i] : QVector3D(0,1,0);
                            buf << p.x() << p.y() << p.z() << nm.x() << nm.y() << nm.z();
                        }

                        gm->vertexCount = n;
                        gm->vbo.create();
                        gm->vbo.bind();
                        gm->vbo.allocate(buf.constData(), int(buf.size() * sizeof(float)));

                        gm->vao.create();
                        gm->vao.bind();
                        // Bind attributes using the lit shader (program must be bound)
                        if (litShader_) litShader_->bind();
                        litShader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * int(sizeof(float)));
                        litShader_->setAttributeBuffer(1, GL_FLOAT, 3 * int(sizeof(float)), 3, 6 * int(sizeof(float)));
                        litShader_->enableAttributeArray(0);
                        litShader_->enableAttributeArray(1);
                        if (litShader_) litShader_->release();
                        gm->vao.release();
                        gm->vbo.release();

                        // Wireframe: index buffer of triangle edges referencing gm->vbo.
                        // Triangle soup → 3 edges (6 indices) per triangle.
                        const int triCount = n / 3;
                        if (triCount > 0) {
                            QVector<GLuint> idx;
                            idx.reserve(triCount * 6);
                            for (int t = 0; t < triCount; ++t) {
                                const GLuint a = GLuint(t * 3 + 0);
                                const GLuint b = GLuint(t * 3 + 1);
                                const GLuint c = GLuint(t * 3 + 2);
                                idx << a << b << b << c << c << a;
                            }
                            gm->lineIndexCount = idx.size();

                            gm->lineVao.create();
                            gm->lineVao.bind();
                            gm->vbo.bind();   // positions live in the interleaved vbo
                            gm->lineIbo.create();
                            gm->lineIbo.bind();
                            gm->lineIbo.allocate(idx.constData(), int(idx.size() * sizeof(GLuint)));
                            // Flat shader: position-only attribute 0, stride matches interleaved layout
                            if (shader_) shader_->bind();
                            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * int(sizeof(float)));
                            shader_->enableAttributeArray(0);
                            if (shader_) shader_->release();
                            gm->lineVao.release();
                            gm->lineIbo.release();
                            gm->vbo.release();
                        }

                        mvrGpuCache_.emplace(key, std::move(gm));
                    }
                }
            }
        }
        doneCurrent();
    }

    update();
}


void Stage3DView::setShowMvrLabels(bool show)
{
    showMvrLabels_ = show;
    update();
}

void Stage3DView::setMvrRenderMode(MvrRenderMode mode)
{
    mvrRenderMode_ = mode;
    update();
}

void Stage3DView::setActiveTool(Stage3DTool tool)
{
    activeTool_   = tool;
    rectDrawing_  = false;
    polyVerts_.clear();
    update();
    switch (tool) {
        case Stage3DTool::OrbitCamera: setCursor(Qt::ArrowCursor);  break;
        case Stage3DTool::Select:      setCursor(Qt::ArrowCursor);  break;
        case Stage3DTool::DrawRect:    setCursor(Qt::CrossCursor);  break;
        case Stage3DTool::DrawPolygon: setCursor(Qt::CrossCursor);  break;
    }
}

void Stage3DView::setSelectedObject(int id) { selectedObjectId_ = id; update(); }

void Stage3DView::setCalibRectVisible(bool visible)
{
    calibRectVisible_ = visible;
    update();
}

void Stage3DView::setCameraMarker(QVector3D pos, float fovDeg, bool visible)
{
    cameraMarkerPos_    = pos;
    cameraMarkerFov_    = fovDeg;
    cameraMarkerVisible_= visible;
    update();
}

Stage3DCameraState Stage3DView::getCameraState() const
{
    return {camCenter_.x(), camCenter_.y(), camCenter_.z(), camYaw_, camPitch_, camDist_};
}

void Stage3DView::setCameraState(const Stage3DCameraState& s)
{
    camCenter_ = QVector3D(s.centerX, s.centerY, s.centerZ);
    camYaw_    = s.yaw;
    camPitch_  = s.pitch;
    camDist_   = s.dist;
    update();
}

void Stage3DView::applyCameraPreset(CameraPreset preset)
{
    switch (preset) {
        case CameraPreset::Top:      camYaw_ =   0; camPitch_ = 89.0f; camDist_ = 15; break;
        case CameraPreset::Front:    camYaw_ =   0; camPitch_ =  8.0f; camDist_ = 15; break;
        case CameraPreset::FrontTop: camYaw_ =   0; camPitch_ = 45.0f; camDist_ = 15; break;
        case CameraPreset::Left:     camYaw_ = -90; camPitch_ =  8.0f; camDist_ = 15; break;
        case CameraPreset::Right:    camYaw_ =  90; camPitch_ =  8.0f; camDist_ = 15; break;
    }
    update();
}

// ─── OpenGL ───────────────────────────────────────────────────────────────────

void Stage3DView::initializeGL()
{
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Stage3DView::cleanup, Qt::DirectConnection);

    shader_ = new QOpenGLShaderProgram();
    initShaders();

    vao_.create();
    vbo_.create();
    vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    litShader_ = new QOpenGLShaderProgram();
    initLitShader();

    litVao_.create();
    litVbo_.create();
    litVbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    buildGridGeometry();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glLineWidth(1.0f);
}

void Stage3DView::initShaders()
{
    shader_->addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc);
    shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    shader_->link();
}

void Stage3DView::initLitShader()
{
    litShader_->addShaderFromSourceCode(QOpenGLShader::Vertex,   kLitVertSrc);
    litShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, kLitFragSrc);
    litShader_->link();
}

void Stage3DView::buildGridGeometry()
{
    gridVerts_.clear();
    const float range = 20.0f;
    const float step  = 1.0f;
    for (float v = -range; v <= range + 0.001f; v += step) {
        gridVerts_ << QVector3D(-range, 0, v) << QVector3D(range, 0, v);
        gridVerts_ << QVector3D(v, 0, -range) << QVector3D(v, 0,  range);
    }
}

void Stage3DView::resizeGL(int w, int h)
{
    viewW_ = w ? w : 1;
    viewH_ = h ? h : 1;
    glViewport(0, 0, w, h);
}

void Stage3DView::paintGL()
{
    // QPainter/Qt internals may leave GL state altered between frames.
    // Re-assert depth state so 3D occlusion remains deterministic.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);

    glClearColor(0.13f, 0.13f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader_->bind();

    glDisable(GL_BLEND);
    drawGrid();
    drawCalibRect();
    drawOriginMarkers();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawStageObjects();

    glDisable(GL_BLEND);
    drawTrackers();
    drawFixtureRays();
    drawMvrLayers();
    drawDrawingPreview();
    drawCameraMarker();

    shader_->release();

    // 2D overlay drawn via QPainter on top of GL content
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawMvrLabels(p);
    drawOriginLabels(p);
    drawGizmoOverlay(p);
}

void Stage3DView::paintEvent(QPaintEvent* e)
{
    QOpenGLWidget::paintEvent(e);
}

// ─── Camera math ─────────────────────────────────────────────────────────────

QVector3D Stage3DView::cameraPos() const
{
    const float yawRad   = qDegreesToRadians(camYaw_);
    const float pitchRad = qDegreesToRadians(camPitch_);
    return camCenter_ + QVector3D(
        camDist_ * std::cos(pitchRad) * std::sin(yawRad),
        camDist_ * std::sin(pitchRad),  // negative = above
        camDist_ * std::cos(pitchRad) * std::cos(yawRad)
    );
}

QMatrix4x4 Stage3DView::mvpMatrix() const
{
    QMatrix4x4 proj, view;
    proj.perspective(45.0f, float(viewW_) / float(viewH_), 0.1f, 300.0f);
    const QVector3D eye = cameraPos();
    QVector3D up(0, 1, 0);
    // Avoid gimbal at exactly ±90°: use the horizontal-forward direction as up
    if (std::abs(camPitch_) > 88.0f)
        up = QVector3D(-std::sin(qDegreesToRadians(camYaw_)), 0,
                       -std::cos(qDegreesToRadians(camYaw_)));
    view.lookAt(eye, camCenter_, up);
    return proj * view;
}

bool Stage3DView::unprojectToHeight(QPoint screenPt, float y, QPointF& out) const
{
    // Build inverse MVP
    QMatrix4x4 mvp = mvpMatrix();
    bool ok;
    QMatrix4x4 inv = mvp.inverted(&ok);
    if (!ok) return false;

    // Normalised device coords
    float ndcX = (2.0f * screenPt.x() / viewW_) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPt.y() / viewH_);

    QVector4D near4 = inv * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D far4  = inv * QVector4D(ndcX, ndcY,  1.0f, 1.0f);
    if (std::abs(near4.w()) < 1e-7f || std::abs(far4.w()) < 1e-7f) return false;

    QVector3D nearPt = near4.toVector3DAffine();
    QVector3D farPt  = far4.toVector3DAffine();
    QVector3D dir    = farPt - nearPt;

    if (std::abs(dir.y()) < 1e-7f) return false;
    float t = (y - nearPt.y()) / dir.y();
    QVector3D hit = nearPt + t * dir;
    out = QPointF(hit.x(), hit.z());
    return true;
}

// ─── Pick ─────────────────────────────────────────────────────────────────────

int Stage3DView::pickObject(QPoint screenPt)
{
    QPointF stageXZ;
    if (!unprojectToHeight(screenPt, 0.0f, stageXZ)) return -1;

    // Proximity check for origin markers (visible system objects)
    const float kPickRadius = 0.5f;
    if (showStageOrigin_) {
        const float d = std::hypot(stageXZ.x(), stageXZ.y());
        if (d < kPickRadius) return -20;
    }
    if (showPsnOrigin_) {
        const float d = std::hypot(float(stageXZ.x()) - psnOffset_.x(),
                                   float(stageXZ.y()) - psnOffset_.z());
        if (d < kPickRadius) return -21;
    }

    for (const auto& obj : stageObjects_) {
        // System objects (camera, calib rect) and stage outlines are not pickable
        if (obj.id < 0 || obj.isStageOutline) continue;
        if (obj.polygon.containsPoint(stageXZ, Qt::OddEvenFill))
            return obj.id;
    }
    return -1;
}

// ─── Draw helpers ─────────────────────────────────────────────────────────────

void Stage3DView::drawPrimitive(GLenum mode, const QVector<QVector3D>& verts,
                                 const QColor& color, float alpha)
{
    if (verts.isEmpty()) return;

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(verts.constData(), int(verts.size() * sizeof(QVector3D)));

    shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    shader_->enableAttributeArray(0);

    QMatrix4x4 mvp = mvpMatrix();
    shader_->setUniformValue("uMVP", mvp);
    shader_->setUniformValue("uColor",
        QVector4D(color.redF(), color.greenF(), color.blueF(),
                  alpha < 0.0f ? color.alphaF() : alpha));

    glDrawArrays(mode, 0, verts.size());

    shader_->disableAttributeArray(0);
    vbo_.release();
    vao_.release();
}

void Stage3DView::drawPrimitiveEx(GLenum mode, const QVector<QVector3D>& verts,
                                   const QColor& color, float alpha, const QMatrix4x4& mvp)
{
    if (verts.isEmpty()) return;

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(verts.constData(), int(verts.size() * sizeof(QVector3D)));

    shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    shader_->enableAttributeArray(0);

    shader_->setUniformValue("uMVP", mvp);
    shader_->setUniformValue("uColor",
        QVector4D(color.redF(), color.greenF(), color.blueF(), alpha));

    glDrawArrays(mode, 0, verts.size());

    shader_->disableAttributeArray(0);
    vbo_.release();
    vao_.release();
}

void Stage3DView::drawGizmoOverlay(QPainter& p) const
{
    const int size   = 64;
    const int margin = 10;
    const int cx = margin + size / 2;
    const int cy = height() - margin - size / 2;

    const float yawRad   = qDegreesToRadians(camYaw_);
    const float pitchRad = qDegreesToRadians(camPitch_);
    QVector3D eye(std::cos(pitchRad) * std::sin(yawRad),
                  std::sin(pitchRad),
                  std::cos(pitchRad) * std::cos(yawRad));
    eye *= 3.0f;
    QVector3D up(0, 1, 0);
    if (std::abs(camPitch_) > 88.0f)
        up = QVector3D(-std::sin(yawRad), 0, -std::cos(yawRad));

    QMatrix4x4 proj, view;
    proj.perspective(45.0f, 1.0f, 0.1f, 10.0f);
    view.lookAt(eye, QVector3D(0, 0, 0), up);
    const QMatrix4x4 gizmoMVP = proj * view;

    auto project = [&](QVector3D v) -> QPointF {
        QVector4D clip = gizmoMVP * QVector4D(v, 1.0f);
        if (qAbs(clip.w()) < 1e-7f) return QPointF(cx, cy);
        QVector3D ndc = clip.toVector3DAffine();
        return QPointF(cx + ndc.x() * size / 2.0f,
                       cy - ndc.y() * size / 2.0f);
    };

    // Background box
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 130));
    p.drawRoundedRect(QRectF(margin, height() - margin - size, size, size), 5, 5);

    const float len = 0.75f;
    QPointF orig = project({0, 0, 0});

    struct Axis { QVector3D dir; QColor color; QString label; };
    const Axis axes[] = {
        {{len,0,0}, QColor(210,60,60),  "X"},
        {{0,len,0}, QColor(60,200,60),  "Y"},
        {{0,0,len}, QColor(60,100,220), "Z"},
    };

    for (const auto& ax : axes) {
        QPointF tip = project(ax.dir);
        p.setPen(QPen(ax.color, 2));
        p.drawLine(orig, tip);
        // arrowhead
        QPointF d = tip - orig;
        double dlen = std::sqrt(d.x()*d.x() + d.y()*d.y());
        if (dlen > 2.0) {
            d /= dlen;
            QPointF perp(-d.y(), d.x());
            const double head = 5.0;
            p.drawLine(tip, tip - d * head + perp * head * 0.45);
            p.drawLine(tip, tip - d * head - perp * head * 0.45);
        }
        p.setFont(QFont("Arial", 7, QFont::Bold));
        p.drawText(tip + QPointF(2, 3), ax.label);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(210, 210, 210));
    p.drawEllipse(orig, 3, 3);
}

void Stage3DView::drawCameraMarker()
{
    if (!cameraMarkerVisible_) return;
    const float x = cameraMarkerPos_.x();
    const float y = cameraMarkerPos_.y();
    const float z = cameraMarkerPos_.z();

    const float s = 0.25f;
    QVector<QVector3D> cross = {
        {x-s, y, z}, {x+s, y, z},
        {x, y, z-s}, {x, y, z+s},
        {x, y-s, z}, {x, y+s, z},
    };
    const QColor camColor(255, 200, 50);
    glLineWidth(2.0f);
    drawPrimitive(GL_LINES, cross, camColor);
    glLineWidth(1.0f);

    // Draw lines from camera toward the calibration stage points (FOV lines)
    if (calibStagePoints_.size() >= 2) {
        QVector<QVector3D> fovLines;
        QList<QPointF> sorted = calibStagePoints_;
        if (sorted.size() >= 3) {
            QPointF c;
            for (const auto& pt : sorted) c += pt;
            c /= sorted.size();
            std::sort(sorted.begin(), sorted.end(), [c](const QPointF& a, const QPointF& b) {
                return std::atan2(a.y()-c.y(), a.x()-c.x()) < std::atan2(b.y()-c.y(), b.x()-c.x());
            });
        }
        for (const auto& pt : sorted)
            fovLines << QVector3D(x, y, z) << QVector3D(float(pt.x()), 0, float(pt.y()));
        drawPrimitive(GL_LINES, fovLines, QColor(255, 200, 50, 100), 0.4f);
    }
}

void Stage3DView::drawGrid()
{
    drawPrimitive(GL_LINES, gridVerts_, QColor(60, 62, 68));
    // Draw axes
    QVector<QVector3D> axes = {
        {0,0,0}, {1,0,0},   // X red
        {0,0,0}, {0,1,0},   // Y green
        {0,0,0}, {0,0,1},   // Z blue
    };
    QVector<QVector3D> axX = {axes[0], axes[1]};
    QVector<QVector3D> axY = {axes[2], axes[3]};
    QVector<QVector3D> axZ = {axes[4], axes[5]};
    drawPrimitive(GL_LINES, axX, QColor(200, 60, 60));
    drawPrimitive(GL_LINES, axY, QColor(60, 200, 60));
    drawPrimitive(GL_LINES, axZ, QColor(60, 60, 200));
}

void Stage3DView::drawCalibRect()
{
    if (!calibRectVisible_) return;
    if (calibStagePoints_.size() < 2) return;

    // Sort points by angle around centroid so they form a proper outline (no crossed lines)
    QList<QPointF> sorted = calibStagePoints_;
    if (sorted.size() >= 3) {
        QPointF c;
        for (const auto& p : sorted) c += p;
        c /= sorted.size();
        std::sort(sorted.begin(), sorted.end(), [c](const QPointF& a, const QPointF& b) {
            return std::atan2(a.y() - c.y(), a.x() - c.x())
                 < std::atan2(b.y() - c.y(), b.x() - c.x());
        });
    }

    QVector<QVector3D> lines;
    const int n = sorted.size();
    for (int i = 0; i < n; ++i) {
        const QPointF& a = sorted[i];
        const QPointF& b = sorted[(i + 1) % n];
        lines << QVector3D(a.x(), 0, a.y()) << QVector3D(b.x(), 0, b.y());
    }
    drawPrimitive(GL_LINES, lines, QColor(255, 180, 0));
}

QVector<QVector3D> Stage3DView::triangulatePolygon(const QPolygonF& poly, float y) const
{
    QVector<QVector3D> verts;
    if (poly.size() < 3) return verts;
    // Fan from vertex 0
    for (int i = 1; i + 1 < poly.size(); ++i) {
        verts << QVector3D(poly[0].x(), y, poly[0].y());
        verts << QVector3D(poly[i].x(), y, poly[i].y());
        verts << QVector3D(poly[i+1].x(), y, poly[i+1].y());
    }
    return verts;
}

QVector<QVector3D> Stage3DView::extrudePolygonSides(const QPolygonF& poly,
                                                      float yBottom, float yTop) const
{
    QVector<QVector3D> verts;
    const int n = poly.size();
    if (n < 2) return verts;
    for (int i = 0; i < n; ++i) {
        const QPointF& a = poly[i];
        const QPointF& b = poly[(i + 1) % n];
        // Two triangles per side
        verts << QVector3D(a.x(), yBottom, a.y());
        verts << QVector3D(b.x(), yBottom, b.y());
        verts << QVector3D(b.x(), yTop,    b.y());

        verts << QVector3D(a.x(), yBottom, a.y());
        verts << QVector3D(b.x(), yTop,    b.y());
        verts << QVector3D(a.x(), yTop,    a.y());
    }
    return verts;
}

void Stage3DView::drawStageObjects()
{
    for (const auto& obj : stageObjects_) {
        if (!obj.visibleIn3D) continue;
        if (obj.polygon.isEmpty()) continue;

        bool selected = (obj.id == selectedObjectId_);

        if (obj.isStageOutline) {
            // Floor-level boundary only — no height extrusion
            QVector<QVector3D> wireLines;
            const auto& poly = obj.polygon;
            const int n = poly.size();
            for (int i = 0; i < n; ++i) {
                const QPointF& a = poly[i];
                const QPointF& b = poly[(i+1) % n];
                wireLines << QVector3D(a.x(), 0, a.y()) << QVector3D(b.x(), 0, b.y());
            }
            glLineWidth(2.0f);
            drawPrimitive(GL_LINES, wireLines, selected ? QColor(255, 220, 60) : obj.color.lighter(160));
            glLineWidth(1.0f);
            continue;
        }

        QColor fill  = obj.color;
        QColor edge  = fill.lighter(150);

        // Top face (semi-transparent fill)
        auto topFace = triangulatePolygon(obj.polygon, obj.height);
        drawPrimitive(GL_TRIANGLES, topFace, fill, fill.alphaF() * 0.7f);

        // Side walls
        auto sides = extrudePolygonSides(obj.polygon, 0.0f, obj.height);
        drawPrimitive(GL_TRIANGLES, sides, fill, fill.alphaF() * 0.4f);

        // Wireframe edges (top + sides + bottom)
        glDisable(GL_BLEND);
        QVector<QVector3D> wireLines;
        const auto& poly = obj.polygon;
        const int n = poly.size();
        for (int i = 0; i < n; ++i) {
            const QPointF& a = poly[i];
            const QPointF& b = poly[(i + 1) % n];
            wireLines << QVector3D(a.x(), obj.height, a.y()) << QVector3D(b.x(), obj.height, b.y());
            wireLines << QVector3D(a.x(), 0,          a.y()) << QVector3D(b.x(), 0,          b.y());
            wireLines << QVector3D(a.x(), 0, a.y()) << QVector3D(a.x(), obj.height, a.y());
        }
        drawPrimitive(GL_LINES, wireLines, selected ? QColor(255, 220, 60) : edge);
        if (selected)
            drawPrimitive(GL_LINES, wireLines, QColor(255, 220, 60));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

void Stage3DView::drawTrackers()
{
    for (auto it = trackerPositions_.constBegin(); it != trackerPositions_.constEnd(); ++it) {
        const int id = it.key();
        const float x = it.value().first;
        const float z = it.value().second;

        // Find stage height at this position (platform objects only)
        float y = 0.0f;
        for (const auto& obj : stageObjects_)
            if (!obj.isStageOutline && obj.polygon.containsPoint({x, z}, Qt::OddEvenFill))
                { y = obj.height; break; }

        QColor color = Qt::white;
        for (const auto& t : trackers_)
            if (t.id == id) { color = t.color; break; }

        // Draw a small cross / diamond marker
        const float s = 0.15f;
        QVector<QVector3D> cross = {
            {x-s, y, z}, {x+s, y, z},
            {x, y, z-s}, {x, y, z+s},
            {x, y-s, z}, {x, y+s, z},
        };
        drawPrimitive(GL_LINES, cross, color);
    }
}

void Stage3DView::setFixtureRays(const QList<FixtureRay>& rays)
{
    fixtureRays_ = rays;
    update();
}

void Stage3DView::setShowRays(bool on)
{
    showRays_ = on;
    update();
}

void Stage3DView::setPsnOrigin(QVector3D offset, float rotDeg)
{
    psnOffset_ = offset;
    psnRotDeg_ = rotDeg;
    update();
}

void Stage3DView::setShowStageOrigin(bool show)
{
    showStageOrigin_ = show;
    update();
}

void Stage3DView::setShowPsnOrigin(bool show)
{
    showPsnOrigin_ = show;
    update();
}

void Stage3DView::setShowMvrOrigins(bool show)
{
    showMvrOrigins_ = show;
    update();
}

void Stage3DView::drawFixtureRays()
{
    if (!showRays_ || fixtureRays_.isEmpty()) return;

    constexpr float kMaxLen = 30.0f; // metres

    for (const auto& ray : fixtureRays_) {
        // Clip ray to floor (y=0) if fixture is above and ray points down
        float len = kMaxLen;
        if (ray.origin.y() > 0.0f && ray.direction.y() < 0.0f) {
            const float t = -ray.origin.y() / ray.direction.y();
            if (t > 0.0f && t < kMaxLen)
                len = t;
        }
        const QVector3D end = ray.origin + ray.direction * len;
        drawPrimitive(GL_LINES, { ray.origin, end }, ray.color);
    }
}

void Stage3DView::drawMvrMeshLit(const MvrMesh& mesh)
{
    if (mesh.vertices.size() < 3) return;

    const MvrMesh* key = &mesh;
    auto it = mvrGpuCache_.find(key);
    if (it != mvrGpuCache_.end()) {
        // Fast path: use pre-created VAO/VBO and draw without rebuilding buffers
        auto gm = it->second.get();
        gm->vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, gm->vertexCount);
        gm->vao.release();
        return;
    }

    // Fallback: create temporary interleaved buffer and upload (previous behavior)
    const int n = mesh.vertices.size();
    const bool hasNormals = (mesh.normals.size() == n);
    QVector<float> buf;
    buf.reserve(n * 6);
    for (int i = 0; i < n; ++i) {
        const QVector3D& p = mesh.vertices[i];
        const QVector3D  nm = hasNormals ? mesh.normals[i] : QVector3D(0, 1, 0);
        buf << p.x() << p.y() << p.z() << nm.x() << nm.y() << nm.z();
    }

    litVao_.bind();
    litVbo_.bind();
    const int bytesNeeded = buf.size() * int(sizeof(float));
    if (bytesNeeded <= litVboCapacity_) {
        litVbo_.write(0, buf.constData(), bytesNeeded);
    } else {
        litVbo_.allocate(buf.constData(), bytesNeeded);
        litVboCapacity_ = bytesNeeded;
    }

    const int stride = 6 * int(sizeof(float));
    litShader_->setAttributeBuffer(0, GL_FLOAT, 0,              3, stride);
    litShader_->setAttributeBuffer(1, GL_FLOAT, 3*sizeof(float), 3, stride);
    litShader_->enableAttributeArray(0);
    litShader_->enableAttributeArray(1);

    glDrawArrays(GL_TRIANGLES, 0, n);

    litShader_->disableAttributeArray(0);
    litShader_->disableAttributeArray(1);
    litVbo_.release();
    litVao_.release();
}

// Draws a mesh as wireframe (GL_LINES) using the flat shader_.
// Caller must have shader_ bound and uMVP/uColor uniforms set.
void Stage3DView::drawMvrMeshWireframe(const MvrMesh& mesh)
{
    if (mesh.vertices.size() < 3) return;

    const MvrMesh* key = &mesh;
    auto it = mvrGpuCache_.find(key);
    if (it != mvrGpuCache_.end() && it->second->lineIndexCount > 0) {
        // Fast path: indexed line edges referencing the cached vertex buffer.
        auto gm = it->second.get();
        gm->lineVao.bind();
        glDrawElements(GL_LINES, gm->lineIndexCount, GL_UNSIGNED_INT, nullptr);
        gm->lineVao.release();
        return;
    }

    // Fallback (cache not built yet): rebuild the line list and upload.
    const int tc = mesh.vertices.size() / 3;
    QVector<QVector3D> lines;
    lines.reserve(tc * 6);
    for (int t = 0; t < tc; ++t) {
        const auto& a  = mesh.vertices[t*3+0];
        const auto& b  = mesh.vertices[t*3+1];
        const auto& c2 = mesh.vertices[t*3+2];
        lines << a << b << b << c2 << c2 << a;
    }

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(lines.constData(), int(lines.size() * sizeof(QVector3D)));
    shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    shader_->enableAttributeArray(0);
    glDrawArrays(GL_LINES, 0, lines.size());
    shader_->disableAttributeArray(0);
    vbo_.release();
    vao_.release();
}


void Stage3DView::drawMvrLayers()
{
    if (mvrImports_.isEmpty()) return;

    const QColor meshColor(160, 165, 185);
    const QColor fixtureColor(255, 170, 40);
    const float  s = 0.18f;

    static const QVector3D kLightDir = QVector3D(0.55f, 0.80f, 0.35f).normalized();

    // Render each import with its own offset/rotation
    for (const MvrImport& import : mvrImports_) {
        if (!import.enabled || import.layers.isEmpty()) continue;

        // Build offset model matrix for this import (translate + rotate Y)
        QMatrix4x4 model;
        model.translate(import.offsetX, import.offsetY, import.offsetZ);
        if (import.rotDeg != 0.f)
            model.rotate(import.rotDeg, 0, 1, 0);
        const QMatrix4x4 offsetMvp = mvpMatrix() * model;

        if (mvrRenderMode_ == MvrRenderMode::Shaded) {
            shader_->release();
            litShader_->bind();
            litShader_->setUniformValue("uMVP",      offsetMvp);
            litShader_->setUniformValue("uLightDir", kLightDir);
            litShader_->setUniformValue("uViewPos",  cameraPos());

            for (const MvrLayer& layer : import.layers) {
                if (!layer.enabled) continue;
                for (const MvrObject& obj : layer.objects) {
                    if (!obj.enabled) continue;
                    for (const MvrMesh& mesh : obj.meshes) {
                        if (mesh.vertices.size() < 3) continue;
                        const QColor c = mesh.color;
                        litShader_->setUniformValue("uColor",
                            float(c.redF()), float(c.greenF()),
                            float(c.blueF()), 1.0f);
                        drawMvrMeshLit(mesh);
                    }
                }
            }

            // Fixture crosses: GL_LINES, switch to flat shader.
            litShader_->release();
            shader_->bind();
            for (const MvrLayer& layer : import.layers) {
                if (!layer.enabled) continue;
                for (const MvrObject& obj : layer.objects) {
                    if (!obj.enabled || obj.type != MvrObject::Type::Fixture) continue;
                    const float x = obj.positionM.x();
                    const float y = obj.positionM.y();
                    const float z = obj.positionM.z();
                    QVector<QVector3D> cross = {
                        {x-s,y,z},{x+s,y,z},
                        {x,y,z-s},{x,y,z+s},
                        {x,y-s,z},{x,y+s,z},
                    };
                    drawPrimitiveEx(GL_LINES, cross, fixtureColor, 1.0f, offsetMvp);
                }
            }
        } else if (mvrRenderMode_ == MvrRenderMode::Wireframe) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            shader_->setUniformValue("uMVP", offsetMvp);
            for (const MvrLayer& layer : import.layers) {
                if (!layer.enabled) continue;
                for (const MvrObject& obj : layer.objects) {
                    if (!obj.enabled) continue;

                    for (const MvrMesh& mesh : obj.meshes) {
                        if (mesh.vertices.size() < 3) continue;
                        const QColor c = mesh.color;
                        shader_->setUniformValue("uColor",
                            QVector4D(c.redF(), c.greenF(), c.blueF(), 0.8f));
                        drawMvrMeshWireframe(mesh);
                    }

                    if (obj.type == MvrObject::Type::Fixture) {
                        const float x = obj.positionM.x();
                        const float y = obj.positionM.y();
                        const float z = obj.positionM.z();
                        QVector<QVector3D> cross = {
                            {x-s,y,z},{x+s,y,z},
                            {x,y,z-s},{x,y,z+s},
                            {x,y-s,z},{x,y+s,z},
                        };
                        drawPrimitiveEx(GL_LINES, cross, fixtureColor, 1.0f, offsetMvp);
                    }
                }
            }
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            for (const MvrLayer& layer : import.layers) {
                if (!layer.enabled) continue;
                for (const MvrObject& obj : layer.objects) {
                    if (!obj.enabled) continue;

                    for (const MvrMesh& mesh : obj.meshes) {
                        if (mesh.vertices.size() < 3) continue;
                        drawPrimitiveEx(GL_TRIANGLES, mesh.vertices, mesh.color, 0.45f, offsetMvp);
                    }

                    if (obj.type == MvrObject::Type::Fixture) {
                        const float x = obj.positionM.x();
                        const float y = obj.positionM.y();
                        const float z = obj.positionM.z();
                        QVector<QVector3D> cross = {
                            {x-s,y,z},{x+s,y,z},
                            {x,y,z-s},{x,y,z+s},
                            {x,y-s,z},{x,y+s,z},
                        };
                        drawPrimitiveEx(GL_LINES, cross, fixtureColor, 1.0f, offsetMvp);
                    }
                }
            }
            glDisable(GL_BLEND);
        }
    }
}

void Stage3DView::drawMvrLabels(QPainter& p) const
{
    if (!showMvrLabels_ || mvrImports_.isEmpty()) return;

    p.setFont(QFont(QStringLiteral("Arial"), 8));

    for (const MvrImport& import : mvrImports_) {
        if (!import.enabled || import.layers.isEmpty()) continue;

        QMatrix4x4 model;
        model.translate(import.offsetX, import.offsetY, import.offsetZ);
        if (import.rotDeg != 0.f)
            model.rotate(import.rotDeg, 0, 1, 0);
        const QMatrix4x4 mvp = mvpMatrix() * model;

        for (const MvrLayer& layer : import.layers) {
            if (!layer.enabled) continue;
            for (const MvrObject& obj : layer.objects) {
                if (!obj.enabled || obj.name.isEmpty()) continue;
                if (obj.type != MvrObject::Type::Fixture &&
                    obj.meshes.isEmpty()) continue;

                const QVector4D clip = mvp * QVector4D(obj.positionM, 1.0f);
                if (clip.w() <= 0.0f) continue;
                QVector3D ndc = clip.toVector3DAffine();
                if (ndc.x() < -1.1f || ndc.x() > 1.1f ||
                    ndc.y() < -1.1f || ndc.y() > 1.1f) continue;

                const float sx = (ndc.x() + 1.0f) * 0.5f * float(viewW_);
                const float sy = (1.0f - ndc.y()) * 0.5f * float(viewH_);
                const QPointF screenPos(sx + 5, sy - 2);

                p.setPen(QColor(0, 0, 0, 160));
                p.drawText(screenPos + QPointF(1, 1), obj.name);
                p.setPen(QColor(255, 200, 80));
                p.drawText(screenPos, obj.name);
            }
        }
    }
}

QPolygonF Stage3DView::rectToPolygon(QPointF center, float width, float depth, float rotDeg) const
{
    const float hw = width / 2.0f;
    const float hd = depth / 2.0f;
    const float cosA = std::cos(qDegreesToRadians(rotDeg));
    const float sinA = std::sin(qDegreesToRadians(rotDeg));
    QPolygonF p;
    for (auto [lx, lz] : QList<QPair<float,float>>{
            {-hw, -hd}, {hw, -hd}, {hw, hd}, {-hw, hd}}) {
        p << QPointF(center.x() + lx * cosA - lz * sinA,
                     center.y() + lx * sinA + lz * cosA);
    }
    return p;
}

void Stage3DView::drawOriginMarkers()
{
    const bool stageSelected = (selectedObjectId_ == -20);
    const bool psnSelected   = (selectedObjectId_ == -21);

    // Stage origin — fixed cross at (0,0,0)
    if (showStageOrigin_) {
        const float arm = 0.35f;
        QVector<QVector3D> cross = {
            {-arm,0,0},{arm,0,0},
            {0,0,-arm},{0,0,arm},
            {0,-arm,0},{0,arm,0},
        };
        glLineWidth(stageSelected ? 3.0f : 2.0f);
        drawPrimitive(GL_LINES, cross, stageSelected ? QColor(255,220,60) : QColor(220,220,220));
        glLineWidth(1.0f);
    }

    // MVR origin markers — one per enabled import
    if (showMvrOrigins_) {
        const float arm = 0.35f;
        for (const MvrImport& imp : mvrImports_) {
            if (!imp.enabled) continue;
            QMatrix4x4 model;
            model.translate(imp.offsetX, imp.offsetY, imp.offsetZ);
            if (imp.rotDeg != 0.f) model.rotate(imp.rotDeg, 0, 1, 0);
            const QMatrix4x4 mvp = mvpMatrix() * model;
            QVector<QVector3D> cross = {
                {-arm,0,0},{arm,0,0},
                {0,0,-arm},{0,0,arm},
                {0,-arm,0},{0,arm,0},
            };
            glLineWidth(2.0f);
            drawPrimitiveEx(GL_LINES, cross, QColor(255, 190, 40), 1.0f, mvp);
            glLineWidth(1.0f);
        }
    }

    // PSN origin — cyan cross at (psnOffset.x, 0, psnOffset.z), rotated
    if (showPsnOrigin_) {
        const float arm = 0.45f;
        const float hs  = 0.10f;

        QMatrix4x4 model;
        model.translate(psnOffset_.x(), 0, psnOffset_.z());
        if (psnRotDeg_ != 0.f) model.rotate(psnRotDeg_, 0, 1, 0);
        const QMatrix4x4 psnMvp = mvpMatrix() * model;

        const QColor psnColor    = psnSelected ? QColor(255, 220, 60) : QColor(0, 190, 200);
        const QColor handleColor = psnSelected ? QColor(255, 180, 0)  : QColor(200, 100, 0);

        QVector<QVector3D> cross = {
            {-arm,0,0},{arm,0,0},
            {0,0,-arm},{0,0,arm},
            {0,-arm,0},{0,arm,0},
        };
        glLineWidth(psnSelected ? 3.0f : 2.0f);
        drawPrimitiveEx(GL_LINES, cross, psnColor, 1.0f, psnMvp);

        // Rotation handle: small diamond at X-arm tip indicating orientation
        QVector<QVector3D> handle = {
            {arm-hs, 0, 0}, {arm+hs, 0, 0},
            {arm, 0, -hs},  {arm, 0, hs},
        };
        glLineWidth(psnSelected ? 2.5f : 1.5f);
        drawPrimitiveEx(GL_LINES, handle, handleColor, 1.0f, psnMvp);
        glLineWidth(1.0f);
    }
}

void Stage3DView::drawOriginLabels(QPainter& p) const
{
    auto projectPoint = [&](QVector3D world) -> QPointF {
        const QVector4D clip = mvpMatrix() * QVector4D(world, 1.0f);
        if (clip.w() <= 0.0f) return QPointF(-9999, -9999);
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.x() < -1.1f || ndc.x() > 1.1f || ndc.y() < -1.1f || ndc.y() > 1.1f)
            return QPointF(-9999, -9999);
        return QPointF((ndc.x() + 1.f) * 0.5f * viewW_,
                       (1.f - ndc.y()) * 0.5f * viewH_);
    };

    p.setFont(QFont(QStringLiteral("Arial"), 8, QFont::Bold));

    auto drawLabel = [&](QPointF screen, const QString& text, QColor col) {
        if (screen.x() < -100) return;
        p.setPen(QColor(0, 0, 0, 160));
        p.drawText(screen + QPointF(5+1, -3+1), text);
        p.setPen(col);
        p.drawText(screen + QPointF(5, -3), text);
    };

    if (showStageOrigin_)
        drawLabel(projectPoint({0, 0, 0}), QStringLiteral("Stage"), QColor(220, 220, 220));

    if (showPsnOrigin_) {
        const QPointF sc = projectPoint({psnOffset_.x(), 0, psnOffset_.z()});
        drawLabel(sc, QStringLiteral("PSN"), QColor(0, 220, 200));
    }

    if (showMvrOrigins_) {
        for (const MvrImport& imp : mvrImports_) {
            if (!imp.enabled) continue;
            const QPointF sc = projectPoint({imp.offsetX, imp.offsetY, imp.offsetZ});
            drawLabel(sc, imp.name.isEmpty() ? QStringLiteral("MVR") : imp.name,
                      QColor(255, 190, 40));
        }
    }
}

void Stage3DView::drawDrawingPreview()
{
    if (activeTool_ == Stage3DTool::DrawRect && rectDrawing_) {
        QPointF c = QPointF((rectStart_.x() + rectCurrent_.x()) / 2.0f,
                            (rectStart_.y() + rectCurrent_.y()) / 2.0f);
        float w = std::abs(float(rectCurrent_.x() - rectStart_.x()));
        float d = std::abs(float(rectCurrent_.y() - rectStart_.y()));
        QPolygonF poly = rectToPolygon(c, std::max(w, 0.01f), std::max(d, 0.01f), 0.0f);
        QVector<QVector3D> lines;
        for (int i = 0; i < poly.size(); ++i) {
            const QPointF& a = poly[i];
            const QPointF& b = poly[(i+1) % poly.size()];
            lines << QVector3D(a.x(), 0, a.y()) << QVector3D(b.x(), 0, b.y());
        }
        drawPrimitive(GL_LINES, lines, QColor(255, 255, 100));
    }

    if (activeTool_ == Stage3DTool::DrawPolygon && !polyVerts_.isEmpty()) {
        QVector<QVector3D> lines;
        for (int i = 0; i + 1 < polyVerts_.size(); ++i)
            lines << QVector3D(polyVerts_[i].x(), 0, polyVerts_[i].y())
                  << QVector3D(polyVerts_[i+1].x(), 0, polyVerts_[i+1].y());
        // Line to cursor
        lines << QVector3D(polyVerts_.last().x(), 0, polyVerts_.last().y())
              << QVector3D(polyCurrent_.x(), 0, polyCurrent_.y());
        drawPrimitive(GL_LINES, lines, QColor(255, 255, 100));

        // Dots for each vertex
        for (const QPointF& v : polyVerts_) {
            const float s = 0.08f;
            QVector<QVector3D> cross = {
                {float(v.x())-s, 0, float(v.y())}, {float(v.x())+s, 0, float(v.y())},
                {float(v.x()), 0, float(v.y())-s}, {float(v.x()), 0, float(v.y())+s},
            };
            drawPrimitive(GL_LINES, cross, QColor(255, 255, 100));
        }
    }
}

// ─── Mouse / keyboard events ─────────────────────────────────────────────────

void Stage3DView::mousePressEvent(QMouseEvent* e)
{
    lastMousePos_ = e->pos();
    isDragging_   = false;

    if (activeTool_ == Stage3DTool::OrbitCamera) {
        isDragging_ = true;
    } else if (activeTool_ == Stage3DTool::DrawRect && e->button() == Qt::LeftButton) {
        QPointF stageXZ;
        if (unprojectToHeight(e->pos(), 0.0f, stageXZ)) {
            rectStart_   = stageXZ;
            rectCurrent_ = stageXZ;
            rectDrawing_ = true;
        }
    } else if (activeTool_ == Stage3DTool::DrawPolygon && e->button() == Qt::LeftButton) {
        QPointF stageXZ;
        if (unprojectToHeight(e->pos(), 0.0f, stageXZ))
            polyVerts_ << stageXZ;
        update();
    }
}

void Stage3DView::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint delta = e->pos() - lastMousePos_;
    lastMousePos_ = e->pos();

    if (activeTool_ == Stage3DTool::OrbitCamera && isDragging_) {
        if (e->buttons() & Qt::LeftButton &&
            !(e->modifiers() & Qt::ControlModifier)) {
            // Convention: drag right = clockwise rotation from above (stage-right
            // swings toward the camera). Negating delta.x achieves this uniformly
            // at every yaw angle with no discontinuity.
            camYaw_   -= delta.x() * 0.5f;
            camPitch_ -= delta.y() * 0.5f;
            camPitch_  = qBound(0.0f, camPitch_, 89.0f);
        } else if ((e->buttons() & Qt::RightButton) ||
                   ((e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier))) {
            // Pan
            const float yawRad = qDegreesToRadians(camYaw_);
            const float speed  = camDist_ * 0.003f;
            QVector3D right( std::cos(yawRad), 0, -std::sin(yawRad));
            QVector3D fwd  (-std::sin(yawRad), 0, -std::cos(yawRad));
            camCenter_ -= right * float(delta.x()) * speed;
            camCenter_ += fwd  * float(delta.y()) * speed;
        }
        update();
    }

    if (activeTool_ == Stage3DTool::DrawRect && rectDrawing_) {
        QPointF stageXZ;
        if (unprojectToHeight(e->pos(), 0.0f, stageXZ))
            rectCurrent_ = stageXZ;
        update();
    }

    if (activeTool_ == Stage3DTool::DrawPolygon) {
        QPointF stageXZ;
        if (unprojectToHeight(e->pos(), 0.0f, stageXZ))
            polyCurrent_ = stageXZ;
        update();
    }

}

void Stage3DView::mouseReleaseEvent(QMouseEvent* e)
{
    isDragging_ = false;

    if (activeTool_ == Stage3DTool::Select && e->button() == Qt::LeftButton) {
        emit objectSelected(pickObject(e->pos()));
    }

    if (activeTool_ == Stage3DTool::DrawRect && e->button() == Qt::LeftButton && rectDrawing_) {
        QPointF stageXZ;
        if (unprojectToHeight(e->pos(), 0.0f, stageXZ))
            rectCurrent_ = stageXZ;

        rectDrawing_ = false;
        float w = std::abs(float(rectCurrent_.x() - rectStart_.x()));
        float d = std::abs(float(rectCurrent_.y() - rectStart_.y()));
        if (w > 0.05f && d > 0.05f) {
            QPointF center((rectStart_.x() + rectCurrent_.x()) / 2.0f,
                           (rectStart_.y() + rectCurrent_.y()) / 2.0f);
            emit rectDrawn(center, w, d);
        }
        update();
    }
}

void Stage3DView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (activeTool_ == Stage3DTool::DrawPolygon && e->button() == Qt::LeftButton) {
        if (polyVerts_.size() >= 3) {
            QPolygonF poly;
            for (const QPointF& v : polyVerts_) poly << v;
            polyVerts_.clear();
            emit polygonDrawn(poly);
            update();
        }
    }
}

void Stage3DView::wheelEvent(QWheelEvent* e)
{
    const float factor = e->angleDelta().y() > 0 ? 0.85f : 1.15f;
    camDist_ = qBound(0.5f, camDist_ * factor, 150.0f);
    update();
}

void Stage3DView::keyPressEvent(QKeyEvent* e)
{
    if (activeTool_ == Stage3DTool::DrawPolygon) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (polyVerts_.size() >= 3) {
                QPolygonF poly;
                for (const QPointF& v : polyVerts_) poly << v;
                polyVerts_.clear();
                emit polygonDrawn(poly);
                update();
            }
        } else if (e->key() == Qt::Key_Escape) {
            polyVerts_.clear();
            update();
        }
    }
    QOpenGLWidget::keyPressEvent(e);
}
