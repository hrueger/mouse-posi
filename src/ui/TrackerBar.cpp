#include "TrackerBar.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QPen>

static QIcon fullscreenIcon() {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(160, 160, 160), 1.5));
    const int m = 1, s = 5;
    const int r = 14;
    p.drawLine(m, m+s, m, m); p.drawLine(m,   m,   m+s, m);
    p.drawLine(r, m+s, r, m); p.drawLine(r,   m,   r-s, m);
    p.drawLine(m, r-s, m, r); p.drawLine(m,   r,   m+s, r);
    p.drawLine(r, r-s, r, r); p.drawLine(r,   r,   r-s, r);
    return QIcon(pm);
}

static QIcon colorDot(const QColor& c, bool enabled) {
    QPixmap pm(10, 10);
    pm.fill(enabled ? c : QColor(110, 110, 110));
    return QIcon(pm);
}

TrackerBar::TrackerBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(40);
    setObjectName("TrackerBar");
    setStyleSheet("QWidget#TrackerBar { border-bottom: 1px solid palette(mid); }");

    fullscreenBtn_ = new QPushButton(this);
    fullscreenBtn_->setIcon(fullscreenIcon());
    fullscreenBtn_->setFixedSize(28, 28);
    fullscreenBtn_->setFlat(true);
    fullscreenBtn_->setFocusPolicy(Qt::NoFocus);
    fullscreenBtn_->setToolTip("Toggle fullscreen");
    connect(fullscreenBtn_, &QPushButton::clicked, this, &TrackerBar::fullscreenClicked);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(6, 5, 6, 5);
    layout_->setSpacing(4);
    layout_->addStretch();
    layout_->addWidget(fullscreenBtn_);
}

void TrackerBar::setTrackers(const QList<TrackerConfig>& trackers) {
    trackers_ = trackers;
    rebuild();
}

void TrackerBar::setActiveTrackerId(int id) {
    activeId_ = id;
    for (int i = 0; i < buttons_.size() && i < trackers_.size(); ++i) {
        buttons_[i]->setChecked(!calibActive_ && trackers_[i].id == id);
    }
}

void TrackerBar::clearRestriction() {
    if (!restricted_) return;
    restricted_ = false;
    allowedIds_.clear();
    rebuild();
}

void TrackerBar::setAllowedTrackers(const QList<int>& ids) {
    if (restricted_ && allowedIds_ == ids) return;
    restricted_ = true;
    allowedIds_ = ids;
    rebuild();
}

void TrackerBar::setCalibrationActive(bool on) {
    if (calibActive_ == on) return;
    calibActive_ = on;
    rebuild();
}

void TrackerBar::rebuild() {
    layout_->removeWidget(fullscreenBtn_);
    while (layout_->count() > 0) {
        QLayoutItem* item = layout_->takeAt(0);
        if (QWidget* w = item->widget()) delete w;
        delete item;
    }
    buttons_.clear();

    for (const auto& t : trackers_) {
        bool allowed = !calibActive_ && (!restricted_ || allowedIds_.contains(t.id));

        auto* btn = new QPushButton(QString("%1  %2").arg(t.id).arg(t.name));
        btn->setCheckable(true);
        btn->setChecked(!calibActive_ && t.id == activeId_);
        btn->setEnabled(allowed);
        btn->setFixedHeight(28);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        btn->setIcon(colorDot(t.color, allowed));

        if (!allowed)
            btn->setToolTip("Not assigned to you in this session");

        connect(btn, &QPushButton::clicked, this, [this, id = t.id, btn](bool) {
            btn->setChecked(true); // prevent toggle-off on re-click
            emit trackerSelected(id);
        });

        layout_->addWidget(btn);
        buttons_.append(btn);
    }

    layout_->addStretch();
    layout_->addWidget(fullscreenBtn_);
}
