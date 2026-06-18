#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QPolygonF>
#include <QList>
#include <QMap>
#include <QPair>
#include <QContextMenuEvent>
#include "Project.h"
#include "MvrImporter.h"
#include <memory>
#include <unordered_map>

class Calibration;

struct FixtureRay {
    QVector3D origin;
    QVector3D direction; // normalized
    QColor    color;
};

enum class Stage3DTool  { OrbitCamera, Select, DrawRect, DrawPolygon };
enum class CameraPreset { Top, Front, FrontTop, Left, Right };
enum class MvrRenderMode { Flat, Shaded, Wireframe };

class Stage3DView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit Stage3DView(QWidget* parent = nullptr);
    ~Stage3DView() override;

    void setCalibration(const Calibration* c, const QList<QPointF>& stagePoints);
    void setStageObjects(const QList<StageObject>& objs);
    void setTrackerPositions(const QMap<int, QPair<float,float>>& pos,
                             const QList<TrackerConfig>& trackers);
    void setMvrImports(const QList<MvrImport>& imports);
    void setShowMvrLabels(bool show);
    void setMvrRenderMode(MvrRenderMode mode);
    void setActiveTool(Stage3DTool tool);
    void applyCameraPreset(CameraPreset preset);
    void setSelectedObject(int id);
    void setCalibRectVisible(bool visible);
    void setCameraMarker(QVector3D pos, float fovDeg, bool visible);
    void setFixtureRays(const QList<FixtureRay>& rays);
    void setShowRays(bool on);
    void setPsnOrigin(QVector3D offset, float rotDeg);
    void setShowStageOrigin(bool show);
    void setShowPsnOrigin(bool show);
    void setShowMvrOrigins(bool show);
    void setOutputMarkerHeight(float h);
    void setShowOutputMarkers(bool show);

    Stage3DCameraState getCameraState() const;
    void               setCameraState(const Stage3DCameraState& s);

signals:
    void polygonDrawn(QPolygonF polygon);
    void rectDrawn(QPointF center, float width, float depth);
    void objectSelected(int id);   // -1 = deselect

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void paintEvent(QPaintEvent* e) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void initShaders();
    void initLitShader();
    void buildGridGeometry();
    void drawMvrMeshLit(const MvrMesh& mesh);
    void drawMvrMeshWireframe(const MvrMesh& mesh);

    QMatrix4x4 mvpMatrix() const;
    QVector3D cameraPos() const;

    // Unproject screen point to XZ plane at height y
    bool unprojectToHeight(QPoint screenPt, float y, QPointF& out) const;

    // Color-picking: render each object with a unique solid color, read pixel
    int pickObject(QPoint screenPt);

    void drawGrid();
    void drawCalibRect();
    void drawStageObjects();
    void drawTrackers();
    void drawFixtureRays();
    void drawMvrLayers();
    void drawMvrLabels(QPainter& p) const;
    void drawDrawingPreview();
    void drawCameraMarker();
    void drawOriginMarkers();
    void drawOriginLabels(QPainter& p) const;
    void drawGizmoOverlay(QPainter& p) const;

    // Upload a vertex array as a temporary draw (lines or triangles)
    void drawPrimitive(GLenum mode, const QVector<QVector3D>& verts, const QColor& color, float alpha = 1.0f);
    void drawPrimitiveEx(GLenum mode, const QVector<QVector3D>& verts, const QColor& color, float alpha, const QMatrix4x4& mvp);

    // Triangulate a polygon (simple fan from centroid — works for convex/star-shaped)
    QVector<QVector3D> triangulatePolygon(const QPolygonF& poly, float y) const;
    QVector<QVector3D> extrudePolygonSides(const QPolygonF& poly, float yBottom, float yTop) const;
    QPolygonF rectToPolygon(QPointF center, float width, float depth, float rotDeg) const;

    void cleanup();

    // Shader + GL state (flat-color, used for grid/trackers/stage objects)
    QOpenGLShaderProgram*    shader_ = nullptr;
    QOpenGLBuffer            vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;

    // Lit shader (Phong, used for MVR geometry in Shaded mode)
    QOpenGLShaderProgram*    litShader_ = nullptr;
    QOpenGLBuffer            litVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject litVao_;
    int                      litVboCapacity_ = 0; // bytes currently allocated in litVbo_

    // Grid geometry (static lines)
    QVector<QVector3D> gridVerts_;

    // Scene data
    const Calibration*            calibration_ = nullptr;
    QList<QPointF>                calibStagePoints_;
    QList<StageObject>            stageObjects_;
    QMap<int, QPair<float,float>> trackerPositions_;
    QList<TrackerConfig>          trackers_;
    QList<MvrImport>              mvrImports_;
    int                           selectedObjectId_ = -1;

    // Camera
    QVector3D camCenter_{0, 0, 0};
    float     camYaw_   = 0.0f;    // degrees, horizontal
    float     camPitch_ = 45.0f;   // degrees, elevation
    float     camDist_  = 10.0f;

    // Mouse interaction
    Stage3DTool activeTool_  = Stage3DTool::OrbitCamera;
    QPoint      lastMousePos_;
    bool        isDragging_  = false;

    // DrawRect state
    bool    rectDrawing_   = false;
    QPointF rectStart_;
    QPointF rectCurrent_;

    // DrawPolygon state
    QList<QPointF> polyVerts_;
    QPointF        polyCurrent_;

    // Viewport
    int viewW_ = 1, viewH_ = 1;

    // GPU cache for MVR meshes: per-mesh VBO + VAO created once on import
    struct GpuMvrMesh {
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLVertexArrayObject vao;
        int vertexCount = 0; // number of vertices (not bytes)
        // Wireframe: index buffer of triangle edges (GL_LINES) referencing vbo.
        // Reuses vbo positions so no extra vertex data is stored.
        QOpenGLBuffer lineIbo{QOpenGLBuffer::IndexBuffer};
        QOpenGLVertexArrayObject lineVao;
        int lineIndexCount = 0; // number of indices in lineIbo
    };
    std::unordered_map<const MvrMesh*, std::unique_ptr<GpuMvrMesh>> mvrGpuCache_;

    // View settings
    bool          showMvrLabels_  = true;
    MvrRenderMode mvrRenderMode_  = MvrRenderMode::Shaded;

    // Visibility overrides for system items
    bool      calibRectVisible_   = true;
    bool      cameraMarkerVisible_= false;
    QVector3D cameraMarkerPos_;
    float     cameraMarkerFov_    = 60.0f;

    // Fixture rays (Show Rays mode)
    bool              showRays_    = false;
    QList<FixtureRay> fixtureRays_;

    // PSN origin alignment
    QVector3D psnOffset_        = {};
    float     psnRotDeg_        = 0.0f;
    bool      showStageOrigin_  = true;
    bool      showPsnOrigin_    = true;
    bool      showMvrOrigins_   = true;

    // PSN output ghost markers (2D-calibration + PSN mode)
    float     outputMarkerHeight_ = 0.0f;
    bool      showOutputMarkers_  = false;
};
