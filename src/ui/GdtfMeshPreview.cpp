#include "GdtfMeshPreview.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QtMath>
#include <cmath>
#include <limits>

static const char* kLitVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vNormal;
out vec3 vFragPos;
void main() {
    vFragPos    = aPos;
    vNormal     = aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kLitFragSrc = R"(
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
out vec4 fragColor;
void main() {
    vec3 norm  = normalize(gl_FrontFacing ? vNormal : -vNormal);
    float wrap = (dot(norm, uLightDir) + 0.4) / 1.4;
    float light = 0.18 + 0.72 * max(wrap, 0.0);
    vec3 viewDir = normalize(uViewPos - vFragPos);
    vec3 halfDir = normalize(uLightDir + viewDir);
    float spec   = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3 base    = uColor.rgb * light + vec3(0.06) * spec;
    fragColor    = vec4(base, uColor.a);
}
)";

GdtfMeshPreview::GdtfMeshPreview(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(180, 180);
}

GdtfMeshPreview::~GdtfMeshPreview()
{
    cleanup();
}

void GdtfMeshPreview::cleanup()
{
    if (!context()) return;
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed,
               this, &GdtfMeshPreview::cleanup);
    makeCurrent();
    if (vbo_.isCreated()) vbo_.destroy();
    if (vao_.isCreated()) vao_.destroy();
    delete shader_;
    shader_ = nullptr;
    doneCurrent();
}

void GdtfMeshPreview::setMeshes(const QVector<MvrMesh>& meshes)
{
    meshes_ = meshes;

    if (!meshes_.isEmpty()) {
        float mnX =  std::numeric_limits<float>::max();
        float mnY =  std::numeric_limits<float>::max();
        float mnZ =  std::numeric_limits<float>::max();
        float mxX = -std::numeric_limits<float>::max();
        float mxY = -std::numeric_limits<float>::max();
        float mxZ = -std::numeric_limits<float>::max();

        for (const auto& mesh : meshes_) {
            for (const auto& v : mesh.vertices) {
                mnX = std::min(mnX, v.x()); mxX = std::max(mxX, v.x());
                mnY = std::min(mnY, v.y()); mxY = std::max(mxY, v.y());
                mnZ = std::min(mnZ, v.z()); mxZ = std::max(mxZ, v.z());
            }
        }

        center_ = QVector3D((mnX + mxX) * 0.5f, (mnY + mxY) * 0.5f, (mnZ + mxZ) * 0.5f);
        const float dx = mxX - mnX, dy = mxY - mnY, dz = mxZ - mnZ;
        radius_ = std::sqrt(dx*dx + dy*dy + dz*dz) * 0.5f;
        if (radius_ < 0.001f) radius_ = 1.0f;
        dist_  = radius_ * 3.0f;
        yaw_   = -30.f;
        pitch_ = 20.f;
    }

    update();
}

void GdtfMeshPreview::clearMeshes()
{
    meshes_.clear();
    update();
}

void GdtfMeshPreview::initializeGL()
{
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &GdtfMeshPreview::cleanup, Qt::DirectConnection);

    shader_ = new QOpenGLShaderProgram();
    shader_->addShaderFromSourceCode(QOpenGLShader::Vertex,   kLitVertSrc);
    shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, kLitFragSrc);
    shader_->link();

    vao_.create();
    vbo_.create();
    vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
}

void GdtfMeshPreview::resizeGL(int w, int h)
{
    viewW_ = w ? w : 1;
    viewH_ = h ? h : 1;
    glViewport(0, 0, w, h);
}

void GdtfMeshPreview::paintGL()
{
    glClearColor(0.14f, 0.14f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (meshes_.isEmpty()) {
        QPainter p(this);
        p.setPen(QColor(90, 90, 100));
        p.drawText(rect(), Qt::AlignCenter, "No 3D model");
        return;
    }

    if (!shader_) return;

    QMatrix4x4 proj;
    proj.perspective(45.f, float(viewW_) / float(viewH_),
                     dist_ * 0.001f, dist_ * 100.f);

    const float yr = qDegreesToRadians(yaw_);
    const float pr = qDegreesToRadians(pitch_);
    const QVector3D dir(std::cos(pr) * std::sin(yr),
                        std::sin(pr),
                        std::cos(pr) * std::cos(yr));
    const QVector3D camPos = center_ + dir * dist_;

    QMatrix4x4 view;
    view.lookAt(camPos, center_, QVector3D(0, 1, 0));

    const QMatrix4x4 vp       = proj * view;
    const QVector3D  lightDir = QVector3D(0.5f, 0.9f, 0.4f).normalized();

    shader_->bind();
    shader_->setUniformValue("uMVP",      vp);
    shader_->setUniformValue("uLightDir", lightDir);
    shader_->setUniformValue("uViewPos",  camPos);
    shader_->setUniformValue("uColor",    QVector4D(0.72f, 0.72f, 0.76f, 1.0f));

    for (const auto& mesh : meshes_)
        drawMesh(mesh);

    shader_->release();
}

void GdtfMeshPreview::drawMesh(const MvrMesh& mesh)
{
    if (mesh.vertices.size() < 3) return;

    const int  n          = mesh.vertices.size();
    const bool hasNormals = (mesh.normals.size() == n);

    QVector<float> buf;
    buf.reserve(n * 6);
    for (int i = 0; i < n; ++i) {
        const QVector3D& p  = mesh.vertices[i];
        const QVector3D  nm = hasNormals ? mesh.normals[i] : QVector3D(0, 1, 0);
        buf << p.x() << p.y() << p.z() << nm.x() << nm.y() << nm.z();
    }

    vao_.bind();
    vbo_.bind();
    const int bytes = buf.size() * int(sizeof(float));
    if (bytes <= vboCapacity_)
        vbo_.write(0, buf.constData(), bytes);
    else {
        vbo_.allocate(buf.constData(), bytes);
        vboCapacity_ = bytes;
    }

    const int stride = 6 * int(sizeof(float));
    shader_->setAttributeBuffer(0, GL_FLOAT, 0,               3, stride);
    shader_->setAttributeBuffer(1, GL_FLOAT, 3*sizeof(float), 3, stride);
    shader_->enableAttributeArray(0);
    shader_->enableAttributeArray(1);

    glDrawArrays(GL_TRIANGLES, 0, n);

    shader_->disableAttributeArray(0);
    shader_->disableAttributeArray(1);
    vbo_.release();
    vao_.release();
}

void GdtfMeshPreview::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        lastMouse_ = e->pos();
        dragging_  = true;
    }
}

void GdtfMeshPreview::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging_) return;
    const QPoint delta = e->pos() - lastMouse_;
    lastMouse_ = e->pos();
    yaw_   += float(delta.x()) * 0.5f;
    pitch_ -= float(delta.y()) * 0.5f;
    pitch_  = qBound(-89.f, pitch_, 89.f);
    update();
}

void GdtfMeshPreview::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) dragging_ = false;
}

void GdtfMeshPreview::wheelEvent(QWheelEvent* e)
{
    const float factor = (e->angleDelta().y() > 0) ? 0.85f : 1.18f;
    dist_ = qMax(radius_ * 0.3f, dist_ * factor);
    update();
}
