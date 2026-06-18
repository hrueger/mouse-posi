#pragma once
#include <QWidget>
#include <QList>
#include "Project.h"
#include "MvrImporter.h"

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class QLineEdit;

class StageItemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StageItemsPanel(QWidget* parent = nullptr);

    void setAllObjects(const QList<StageObject>& all);
    void setSelectedObject(int id);
    void setMvrImports(const QList<MvrImport>& imports);
    void setOriginVisibility(bool stageOriginIn3D, bool psnOriginIn3D);

signals:
    void selectionChanged(int id);
    void addRectRequested();
    void addPolygonRequested();
    void addStageOutlineRequested();
    void deleteRequested(int id);
    void duplicateRequested(int id);
    void objectRenamed(int id, QString name);
    void visibilityChanged(int id, bool inVideo, bool in3D);
    void mvrVisibilityChanged(int importIdx, int layerIdx, int objIdx, bool visible);
    void mvrImportRenamed(int importIdx, QString name);
    void mvrImportDeleteRequested(int importIdx);
    void mvrImportSelected(int importIdx);
    void mvrChildItemSelected();
    // Emitted when an MVR fixture object is selected (not a layer or group)
    void mvrFixtureSelected(int importIdx, int layerIdx, int objIdx);

private slots:
    void onItemSelectionChanged();
    void onItemChanged(QTreeWidgetItem* item, int col);
    void onItemDoubleClicked(QTreeWidgetItem* item, int col);

private:
    void rebuildTree();
    void updateButtonStates();

    // UserRole data keys stored on tree items
    enum ItemRole {
        RoleKind       = Qt::UserRole,      // "stage" | "mvr-root" | "mvr-layer" | "mvr-obj"
        RoleId         = Qt::UserRole + 1,  // StageObject id (stage items)
        RoleMvrLayer   = Qt::UserRole + 2,  // MVR layer index
        RoleMvrObj     = Qt::UserRole + 3,  // MVR object index (-1 for layer rows)
        RoleMvrImport  = Qt::UserRole + 4,  // MVR import index (which root)
        RoleTypeStr    = Qt::UserRole + 5,  // type string for filtering
    };

    void applyFilter(const QString& text);

    QLineEdit*     filterEdit_;
    QTreeWidget*   tree_;
    QToolButton*   addRectBtn_;
    QToolButton*   addPolyBtn_;
    QToolButton*   addOutlineBtn_;
    QToolButton*   deleteBtn_;

    QList<StageObject> objects_;
    QList<MvrImport>   mvrImports_;
    int  selectedId_          = -999;
    bool updatingTree_        = false;
    bool stageOriginVisible_  = true;
    bool psnOriginVisible_    = true;
};
