#pragma once
#include <QWidget>

class QScrollArea;
class QVBoxLayout;
class CollapsibleSection;

class SidebarWidget : public QWidget {
    Q_OBJECT
public:
    explicit SidebarWidget(QWidget* parent = nullptr);

    // Add a collapsible panel with title. Returns the section for further control.
    CollapsibleSection* addPanel(const QString& title, QWidget* content,
                                 bool startExpanded = true);

    // Hide sidebar in fullscreen mode; restore after
    void setFullscreenMode(bool on);

private:
    QScrollArea* scroll_;
    QWidget*     container_;
    QVBoxLayout* layout_;
};
