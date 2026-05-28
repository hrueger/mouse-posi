#pragma once
#include <QWidget>
#include <QList>
#include "Project.h"

class QTableWidget;
class QTableWidgetItem;
class QToolButton;

class StageItemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StageItemsPanel(QWidget* parent = nullptr);

    void setAllObjects(const QList<StageObject>& all);
    void setSelectedObject(int id);

signals:
    void selectionChanged(int id);
    void addRectRequested();
    void addPolygonRequested();
    void addStageOutlineRequested();
    void deleteRequested(int id);
    void duplicateRequested(int id);
    void visibilityChanged(int id, bool inVideo, bool in3D);

private slots:
    void onTableRowChanged(int row);
    void onTableItemChanged(QTableWidgetItem* item);

private:
    void rebuildTable();
    void updateButtonStates();

    QTableWidget*   objectTable_;
    QToolButton*    addRectBtn_;
    QToolButton*    addPolyBtn_;
    QToolButton*    addOutlineBtn_;
    QToolButton*    deleteBtn_;

    QList<StageObject> objects_;
    int  selectedId_    = -999;
    bool updatingTable_ = false;
};
