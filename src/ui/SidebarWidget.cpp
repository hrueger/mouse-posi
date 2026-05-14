#include "SidebarWidget.h"
#include "CollapsibleSection.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QSizePolicy>

SidebarWidget::SidebarWidget(QWidget* parent) : QWidget(parent) {
    setFixedWidth(280);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);

    container_ = new QWidget;
    layout_    = new QVBoxLayout(container_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    layout_->addStretch();

    scroll_->setWidget(container_);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scroll_);
}

CollapsibleSection* SidebarWidget::addPanel(const QString& title, QWidget* content,
                                             bool startExpanded)
{
    auto* section = new CollapsibleSection(title, container_);
    section->setContentWidget(content);
    // Insert before the trailing stretch
    layout_->insertWidget(layout_->count() - 1, section);
    if (!startExpanded)
        section->setExpanded(false);
    return section;
}

void SidebarWidget::setFullscreenMode(bool on) {
    setVisible(!on);
}
