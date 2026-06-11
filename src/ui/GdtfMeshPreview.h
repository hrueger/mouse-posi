#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>
#include <QVector>
#include <QMatrix4x4>
#include "../MvrImporter.h"

class GdtfMeshPreview : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GdtfMeshPreview(QWidget* parent = nullptr);
    ~GdtfMeshPreview() override;

    void setMeshes(const QVector<MvrMesh>& meshes);
    void clearMeshes();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void cleanup();
    void drawMesh(const MvrMesh& mesh);

    QOpenGLShaderProgram*    shader_      = nullptr;
    QOpenGLBuffer            vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    int                      vboCapacity_ = 0;

    QVector<MvrMesh> meshes_;

    float     yaw_    = -30.f;
    float     pitch_  = 20.f;
    float     dist_   = 5.f;
    QVector3D center_;
    float     radius_ = 1.f;

    QPoint    lastMouse_;
    bool      dragging_ = false;

    int viewW_ = 1, viewH_ = 1;
};
