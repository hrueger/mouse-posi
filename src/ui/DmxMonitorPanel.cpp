#include "DmxMonitorPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QTimer>
#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QScrollArea>
#include <QStackedWidget>
#include <QPushButton>
#include <QStyledItemDelegate>

// ── Bar delegate for table view ───────────────────────────────────────────────

class DmxBarDelegate : public QStyledItemDelegate {
public:
    explicit DmxBarDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override {
        const int    value  = index.data(Qt::UserRole).toInt();
        const QColor barCol = index.data(Qt::UserRole + 1).value<QColor>();
        painter->save();
        painter->fillRect(opt.rect, opt.palette.base());
        if (value > 0) {
            QRect r = opt.rect.adjusted(2, 3, -2, -3);
            r.setWidth(int(r.width() * value / 255.0));
            painter->fillRect(r, barCol);
        }
        painter->restore();
    }
};

// ── Grid widget ───────────────────────────────────────────────────────────────

static QColor fixtureLineColor(const QString& name) {
    // Spread hues evenly using a multiplicative hash
    const int hue = int((quint32(qHash(name)) * 2654435769u) >> 24) * 360 / 256;
    return QColor::fromHsv(hue, 190, 220);
}

class DmxGridWidget : public QWidget {
public:
    static constexpr int COLS   = 32;
    static constexpr int ROWS   = 512 / COLS;  // 16
    static constexpr int CELL_H = 36;
    static constexpr int LINE_H = 3;           // fixture top-line height

    explicit DmxGridWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(ROWS * CELL_H);
    }

    void setData(const QByteArray& frame,
                 const QMap<int, DmxMonitorPanel::ChannelInfo>& patchMap,
                 bool isOut)
    {
        frame_    = frame;
        patchMap_ = patchMap;
        isOut_    = isOut;
        update();
    }

    QSize sizeHint() const override { return {width(), ROWS * CELL_H}; }

protected:
    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const int    cellW          = width() / COLS;
        const QColor baseColor      = isOut_ ? QColor(220, 140, 40) : QColor(60, 130, 220);
        const QColor computedBorder = QColor(255, 200, 60);
        const QFont  smallFont      = QFont(font().family(), 7);
        const QFont  valFont        = QFont(font().family(), 9, QFont::Bold);
        const QColor bgBase         = palette().color(QPalette::Base);
        const QColor midColor       = palette().color(QPalette::Mid);
        const QColor dimText        = palette().color(QPalette::Disabled, QPalette::Text);
        const QColor brightText     = palette().color(QPalette::BrightText);

        for (int i = 0; i < 512; ++i) {
            const int col = i % COLS;
            const int row = i / COLS;
            const QRect cell(col * cellW, row * CELL_H, cellW, CELL_H);

            if (!ev->rect().intersects(cell)) continue;

            const int  raw      = (i < frame_.size()) ? quint8(frame_[i]) : 0;
            const bool patched  = patchMap_.contains(i);
            const bool computed = patched && patchMap_[i].isComputed;
            const QString& fixName    = patched ? patchMap_[i].fixtureName : QString();
            const int      fixBase    = patched ? patchMap_[i].fixtureBaseAddr : -1;
            const bool     hovered    = hoveredBase_ >= 0 && fixBase == hoveredBase_;

            // ── Background ────────────────────────────────────────────────────
            QColor bg = patched ? bgBase.darker(112) : bgBase;
            p.fillRect(cell, bg);
            if (raw > 0) {
                QColor fill = baseColor;
                fill.setAlpha(int(30 + 185 * raw / 255.0));
                p.fillRect(cell, fill);
            }
            if (hovered)
                p.fillRect(cell, QColor(255, 255, 255, 22));

            // ── Grid line ─────────────────────────────────────────────────────
            p.setPen(QPen(computed ? computedBorder
                                   : (patched ? midColor : midColor.darker(115)), 1));
            p.drawRect(cell.adjusted(0, 0, -1, -1));

            // ── Fixture top line ──────────────────────────────────────────────
            if (patched) {
                p.fillRect(QRect(cell.left(), cell.top(), cell.width(), LINE_H),
                           fixtureLineColor(fixName));
                // Extra border highlight for computed channels
                if (computed) {
                    p.setPen(QPen(computedBorder, 2));
                    p.drawRect(cell.adjusted(1, 1, -1, -1));
                }
            }

            // ── Channel number ────────────────────────────────────────────────
            p.setFont(smallFont);
            p.setPen(raw > 0 ? brightText : dimText);
            p.drawText(cell.adjusted(3, LINE_H + 1, -1, -CELL_H / 2),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number(i + 1));

            // ── Value ─────────────────────────────────────────────────────────
            if (raw > 0) {
                p.setFont(valFont);
                p.setPen(brightText);
                p.drawText(cell.adjusted(0, CELL_H / 3, -2, -2),
                           Qt::AlignHCenter | Qt::AlignBottom, QString::number(raw));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        const int cellW = width() / COLS;
        if (cellW <= 0) return;
        const int col = ev->pos().x() / cellW;
        const int row = ev->pos().y() / CELL_H;

        int newBase = -1;
        int hovCh = -1;
        if (col >= 0 && col < COLS && row >= 0 && row < ROWS) {
            hovCh = row * COLS + col;
            if (hovCh >= 0 && hovCh < 512 && patchMap_.contains(hovCh))
                newBase = patchMap_[hovCh].fixtureBaseAddr;
        }

        if (newBase != hoveredBase_) {
            hoveredBase_ = newBase;
            update();
        }

        // Tooltip
        if (hovCh < 0 || hovCh >= 512) { QToolTip::hideText(); return; }
        const int raw = (hovCh < frame_.size()) ? quint8(frame_[hovCh]) : 0;
        QString tip = QString("Ch %1  —  Value: %2").arg(hovCh + 1).arg(raw);
        if (patchMap_.contains(hovCh)) {
            const auto& info = patchMap_[hovCh];
            if (!info.fixtureName.isEmpty()) tip += "\n" + info.fixtureName;
            if (!info.function.isEmpty())    tip += "  [" + info.function + "]";
            if (info.isComputed)             tip += "\n★ computed by OnPoint";
        }
        QToolTip::showText(ev->globalPosition().toPoint(), tip, this);
    }

    void leaveEvent(QEvent*) override {
        if (hoveredBase_ >= 0) {
            hoveredBase_ = -1;
            update();
        }
    }

private:
    QByteArray  frame_;
    QMap<int, DmxMonitorPanel::ChannelInfo> patchMap_;
    bool isOut_       = true;
    int  hoveredBase_ = -1;  // fixtureBaseAddr of the currently hovered fixture instance
};

// ── DmxMonitorPanel ───────────────────────────────────────────────────────────

DmxMonitorPanel::DmxMonitorPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // ── Top bar ───────────────────────────────────────────────────────────────
    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel("Universe:"));
    universeCombo_ = new QComboBox;
    universeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topRow->addWidget(universeCombo_, 1);

    directionBadge_ = new QLabel;
    directionBadge_->setFixedWidth(70);
    directionBadge_->setAlignment(Qt::AlignCenter);
    topRow->addWidget(directionBadge_);

    topRow->addSpacing(8);

    auto* tableBtn = new QPushButton("Table");
    auto* gridBtn  = new QPushButton("Grid");
    tableBtn->setCheckable(true);
    gridBtn->setCheckable(true);
    tableBtn->setChecked(true);
    tableBtn->setFixedHeight(24);
    gridBtn->setFixedHeight(24);
    topRow->addWidget(tableBtn);
    topRow->addWidget(gridBtn);
    lay->addLayout(topRow);

    // ── Stack: table / grid ───────────────────────────────────────────────────
    stack_ = new QStackedWidget;

    // Table view
    table_ = new QTableWidget(512, 5);
    table_->setHorizontalHeaderLabels({"Ch", "Value", "Bar", "Fixture", "Function"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setItemDelegateForColumn(2, new DmxBarDelegate(this));

    for (int i = 0; i < 512; ++i) {
        auto* chItem = new QTableWidgetItem(QString::number(i + 1));
        chItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 0, chItem);
        table_->setItem(i, 1, new QTableWidgetItem("0"));
        table_->setItem(i, 2, new QTableWidgetItem());
        table_->setItem(i, 3, new QTableWidgetItem());
        table_->setItem(i, 4, new QTableWidgetItem());
        table_->setRowHeight(i, 18);
    }
    stack_->addWidget(table_);   // index 0

    // Grid view (scroll area wrapping the grid widget)
    auto* gridScroll = new QScrollArea;
    gridScroll->setWidgetResizable(true);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridWidget_ = new DmxGridWidget;
    gridScroll->setWidget(gridWidget_);
    stack_->addWidget(gridScroll);  // index 1

    lay->addWidget(stack_, 1);

    // ── Refresh timer ─────────────────────────────────────────────────────────
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(80);
    connect(refreshTimer_, &QTimer::timeout, this, &DmxMonitorPanel::refreshValues);
    refreshTimer_->start();

    // ── Connections ───────────────────────────────────────────────────────────
    connect(universeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DmxMonitorPanel::onUniverseSelected);

    connect(tableBtn, &QPushButton::clicked, this, [=]() {
        tableBtn->setChecked(true);
        gridBtn->setChecked(false);
        stack_->setCurrentIndex(0);
    });
    connect(gridBtn, &QPushButton::clicked, this, [=]() {
        gridBtn->setChecked(true);
        tableBtn->setChecked(false);
        stack_->setCurrentIndex(1);
        refreshValues();
    });
}

void DmxMonitorPanel::setUniverses(const QList<DmxUniverseEntry>& universes) {
    universes_ = universes;
    rebuildCombo();
}

void DmxMonitorPanel::setMvrImports(const QList<MvrImportData>& imports) {
    mvrImports_ = imports;
    // Defer so we don't block the caller (applyProject) — the 80ms timer picks it up anyway.
    QTimer::singleShot(0, this, &DmxMonitorPanel::refreshValues);
}

void DmxMonitorPanel::rebuildCombo() {
    const QSignalBlocker blocker(universeCombo_);
    quint16 prevUni = 0;
    if (universeCombo_->currentIndex() >= 0)
        prevUni = quint16(universeCombo_->currentData().toInt());

    universeCombo_->clear();
    for (const auto& e : universes_) {
        const QString prefix =
            (e.role == DmxUniverseRole::OutFixtures) ? "[OUT]" :
            (e.role == DmxUniverseRole::InFixtures)  ? "[IN Fix]" : "[IN Ctrl]";
        universeCombo_->addItem(
            QString("%1 %2 (U%3)").arg(prefix, e.name).arg(e.number),
            int(e.number));
    }

    int idx = universeCombo_->findData(int(prevUni));
    universeCombo_->setCurrentIndex(idx >= 0 ? idx : (universeCombo_->count() > 0 ? 0 : -1));
    // Call directly so the badge updates immediately; refreshValues() inside is
    // now fast thanks to the QSignalBlocker fix in the table update loop.
    onUniverseSelected(universeCombo_->currentIndex());
}

void DmxMonitorPanel::onUniverseSelected(int index) {
    if (index < 0 || index >= universes_.size()) {
        directionBadge_->setText("");
        return;
    }
    const auto& e    = universes_[index];
    const bool  isOut = (e.role == DmxUniverseRole::OutFixtures);
    directionBadge_->setText(isOut ? "OUT" : "IN");
    directionBadge_->setStyleSheet(isOut
        ? "border: 1px solid #d08030; border-radius: 4px; padding: 1px 4px; color: #d08030;"
        : "border: 1px solid #3070c0; border-radius: 4px; padding: 1px 4px; color: #3070c0;");
    refreshValues();
}

QMap<int, DmxMonitorPanel::ChannelInfo> DmxMonitorPanel::buildPatchMap(quint16 universe) const {
    QMap<int, ChannelInfo> map;

    for (const auto& imp : mvrImports_) {
        if (!imp.enabled) continue;
        for (const auto& layer : imp.layers) {
            if (!layer.enabled) continue;
            for (const auto& obj : layer.objects) {
                if (!obj.enabled) continue;
                if (obj.type != MvrObjectData::Type::Fixture) continue;
                if (quint16(obj.universe) != universe) continue;

                const int base     = obj.dmxAddress - 1;   // 0-based
                const int footprint = obj.gdtfProfile.valid ? obj.gdtfProfile.footprint : 0;

                // Mark all channels within the footprint as belonging to this fixture
                for (int rel = 0; rel < footprint && base + rel < 512; ++rel) {
                    if (!map.contains(base + rel)) {
                        const int addr1 = rel + 1;
                        const QString fn = obj.gdtfProfile.channelNames.value(addr1, QString("Ch %1").arg(addr1));
                        map[base + rel] = {obj.name, fn, false, base};
                    }
                }

                // Mark computed channels (pan/tilt overridden by OnPoint)
                auto markCh = [&](int addr1based, const QString& fn, bool computed) {
                    if (addr1based <= 0) return;
                    const int abs = base + addr1based - 1;
                    if (abs < 0 || abs >= 512) return;
                    map[abs] = {obj.name, fn, computed, base};
                };

                if (obj.gdtfProfile.valid) {
                    markCh(obj.gdtfProfile.pan.address,   "Pan",      true);
                    markCh(obj.gdtfProfile.pan.address2,  "Pan Fine", true);
                    markCh(obj.gdtfProfile.tilt.address,  "Tilt",     true);
                    markCh(obj.gdtfProfile.tilt.address2, "Tilt Fine",true);
                }
            }
        }
    }
    return map;
}

void DmxMonitorPanel::updateOutFrame(quint16 universe, const QByteArray& frame) {
    outFrames_[universe] = frame;
    int idx = universeCombo_->currentIndex();
    if (idx >= 0 && idx < universes_.size() && universes_[idx].number == universe)
        refreshValues();
}

void DmxMonitorPanel::updateInFrame(quint16 universe, const QByteArray& frame) {
    inFrames_[universe] = frame;
    int idx = universeCombo_->currentIndex();
    if (idx >= 0 && idx < universes_.size() && universes_[idx].number == universe)
        refreshValues();
}

void DmxMonitorPanel::refreshValues() {
    int idx = universeCombo_->currentIndex();
    if (idx < 0 || idx >= universes_.size()) return;

    const auto&    e      = universes_[idx];
    const quint16  uni    = e.number;
    const bool     isOut  = (e.role == DmxUniverseRole::OutFixtures);
    const QByteArray frame = isOut
        ? outFrames_.value(uni, QByteArray(512, '\0'))
        :  inFrames_.value(uni, QByteArray(512, '\0'));

    const auto patchMap = buildPatchMap(uni);
    const QColor barColor = isOut ? QColor(220, 140, 40, 180) : QColor(60, 130, 220, 180);

    // ── Update grid (always, cheap) ───────────────────────────────────────────
    gridWidget_->setData(frame, patchMap, isOut);

    // ── Update table ──────────────────────────────────────────────────────────
    if (stack_->currentIndex() != 0) return;

    // Block model signals to suppress per-row ResizeToContents column-width
    // recalculations (each dataChanged would otherwise re-measure all visible
    // rows for columns 0, 1, 4 — O(512 * visible_rows) per refresh call).
    {
        const QSignalBlocker modelBlocker(table_->model());
        for (int i = 0; i < 512; ++i) {
            const int raw = (i < frame.size()) ? quint8(frame[i]) : 0;

            if (auto* v = table_->item(i, 1)) {
                v->setText(QString::number(raw));
                v->setForeground(raw > 0 ? (isOut ? QColor("#d08030") : QColor("#3070c0"))
                                         : QColor(Qt::gray));
            }
            if (auto* b = table_->item(i, 2)) {
                b->setData(Qt::UserRole, raw);
                b->setData(Qt::UserRole + 1, barColor);
            }

            if (patchMap.contains(i)) {
                const auto& info = patchMap[i];
                const QColor txtColor = info.isComputed ? QColor("#e8c050") : QColor();
                if (auto* fi = table_->item(i, 3)) {
                    fi->setText(info.fixtureName);
                    fi->setForeground(info.isComputed ? QBrush(txtColor) : QBrush());
                }
                if (auto* fn = table_->item(i, 4)) {
                    fn->setText(info.function);
                    fn->setForeground(info.isComputed ? QBrush(txtColor) : QBrush());
                }
            } else {
                if (auto* fi = table_->item(i, 3)) { fi->setText({}); fi->setForeground({}); }
                if (auto* fn = table_->item(i, 4)) { fn->setText({}); fn->setForeground({}); }
            }
        }
    } // modelBlocker destructor unblocks signals
    table_->viewport()->update();
}
