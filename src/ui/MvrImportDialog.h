#pragma once
#include <QDialog>
#include <QList>
#include "MvrImporter.h"

class QTreeWidget;
class QTreeWidgetItem;

class MvrImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit MvrImportDialog(const QList<MvrLayer>& layers, QWidget* parent = nullptr);

    // Call after exec() == Accepted to get user's selection.
    // Layers and objects have their 'enabled' field updated.
    QList<MvrLayer> selectedLayers() const;

private slots:
    void onItemChanged(QTreeWidgetItem* item, int col);

private:
    void buildTree(const QList<MvrLayer>& layers);

    QTreeWidget*    tree_;
    QList<MvrLayer> layers_;
    bool            updatingTree_ = false;
};
