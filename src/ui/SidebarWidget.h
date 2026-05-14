#pragma once
#include <QWidget>

class QScrollArea;
class QVBoxLayout;
class CollapsibleSection;

class SidebarWidget : public QWidget {
    Q_OBJECT
public:
    explicit SidebarWidget(QWidget* parent = nullptr);

    CollapsibleSection* addPanel(const QString& title, QWidget* content,
                                 bool startExpanded = true);
    void setFullscreenMode(bool on);

private:

    QScrollArea* scroll_;
    QWidget*     container_;
    QVBoxLayout* layout_;
};
