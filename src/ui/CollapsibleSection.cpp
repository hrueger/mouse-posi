#include "CollapsibleSection.h"
#include <QPushButton>
#include <QFrame>
#include <QVBoxLayout>
#include <QApplication>
#include <QPalette>

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent), title_(title)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    header_ = new QPushButton;
    header_->setFlat(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(header_, &QPushButton::clicked, this, &CollapsibleSection::toggleExpanded);

    // palette() in QSS is not reliably resolved from qlementine's palette on macOS.
    // Extract actual colors from qApp->palette() and bake them into the style string.
    auto applyHeaderStyle = [this]() {
        const QPalette p = qApp->palette();
        const QString text = p.color(QPalette::WindowText).name();
        const QString bg   = p.color(QPalette::Window).name();
        const QString mid  = p.color(QPalette::Mid).name();
        header_->setStyleSheet(QString(
            "QPushButton {"
            "  border: none;"
            "  border-bottom: 1px solid %1;"
            "  padding: 5px 8px;"
            "  font-weight: bold;"
            "  text-align: left;"
            "  color: %2;"
            "  background: %3;"
            "}"
            "QPushButton:hover { background: rgba(128,128,128,50); }"
        ).arg(mid, text, bg));
    };
    applyHeaderStyle();
    QObject::connect(qApp, &QApplication::paletteChanged,
                     header_, [applyHeaderStyle](const QPalette&) { applyHeaderStyle(); });

    body_ = new QFrame;
    body_->setFrameShape(QFrame::NoFrame);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(6, 4, 6, 6);
    bodyLayout_->setSpacing(4);

    mainLayout->addWidget(header_);
    mainLayout->addWidget(body_);

    updateChevron();
}

void CollapsibleSection::setContentWidget(QWidget* widget) {
    bodyLayout_->addWidget(widget);
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    body_->setVisible(expanded_);
    updateChevron();
    emit expandedChanged(expanded_);
}

void CollapsibleSection::toggleExpanded() {
    setExpanded(!expanded_);
}

void CollapsibleSection::updateChevron() {
    header_->setText((expanded_ ? "▼  " : "▶  ") + title_);
}
