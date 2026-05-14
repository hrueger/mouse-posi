#include "CollapsibleSection.h"
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    header_ = new QToolButton;
    header_->setText(title);
    header_->setCheckable(false);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setArrowType(Qt::DownArrow);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setStyleSheet(
        "QToolButton { border: none; border-bottom: 1px solid palette(mid);"
        " padding: 4px 6px; font-weight: bold; text-align: left; }"
        "QToolButton:hover { background: palette(midlight); }");
    connect(header_, &QToolButton::clicked, this, &CollapsibleSection::toggleExpanded);

    body_ = new QFrame;
    body_->setFrameShape(QFrame::NoFrame);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(6, 4, 6, 6);
    bodyLayout_->setSpacing(4);

    mainLayout->addWidget(header_);
    mainLayout->addWidget(body_);

    // Animation: drive maximumHeight on the body frame
    animation_.setTargetObject(body_);
    animation_.setPropertyName("maximumHeight");
    animation_.setDuration(160);
    animation_.setEasingCurve(QEasingCurve::InOutQuad);

    // When expand animation finishes, release the height cap
    connect(&animation_, &QPropertyAnimation::finished, this, [this]() {
        if (expanded_)
            body_->setMaximumHeight(QWIDGETSIZE_MAX);
        else
            body_->hide();
    });
}

void CollapsibleSection::setContentWidget(QWidget* widget) {
    bodyLayout_->addWidget(widget);
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    updateChevron();

    animation_.stop();

    if (expanded) {
        // Show first so sizeHint() is valid, then animate height from 0
        body_->setMaximumHeight(0);
        body_->show();
        int target = body_->sizeHint().height();
        if (target <= 0) target = 300; // fallback
        animation_.setStartValue(0);
        animation_.setEndValue(target);
    } else {
        // Animate from current height to 0, then hide
        animation_.setStartValue(body_->height());
        animation_.setEndValue(0);
    }

    animation_.start();
    emit expandedChanged(expanded_);
}

void CollapsibleSection::toggleExpanded() {
    setExpanded(!expanded_);
}

void CollapsibleSection::updateChevron() {
    header_->setArrowType(expanded_ ? Qt::DownArrow : Qt::RightArrow);
}

int CollapsibleSection::bodyHeight() const {
    return body_->maximumHeight();
}

void CollapsibleSection::setBodyHeight(int h) {
    body_->setMaximumHeight(h);
}
