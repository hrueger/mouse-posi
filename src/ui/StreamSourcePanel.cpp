#include "StreamSourcePanel.h"
#include "../NdiReceiver.h"
#include "../DeckLinkCapture.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QApplication>
#include <QPalette>
#if WEBCAM_AVAILABLE
#  include <QMediaDevices>
#  include <QCameraDevice>
#endif

static constexpr int TypeRole = Qt::UserRole;
static constexpr int IdRole   = Qt::UserRole + 1;

StreamSourcePanel::StreamSourcePanel(NdiReceiver* ndi, QWidget* parent)
    : QWidget(parent), ndi_(ndi)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(6);

    masterCombo_ = new QComboBox;
    masterCombo_->setMinimumContentsLength(16);
    masterCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    comboModel_ = new QStandardItemModel(this);
    masterCombo_->setModel(comboModel_);
    layout->addWidget(masterCombo_);

    propsStack_ = new QStackedWidget;
    propsStack_->setVisible(false);
    layout->addWidget(propsStack_);
    layout->addStretch();

    // ── NDI properties page (stack index 0) ──────────────────────────────
    {
        auto* page = new QWidget;
        auto* pl   = new QVBoxLayout(page);
        pl->setContentsMargins(0, 4, 0, 0);
        pl->setSpacing(4);
        ndiRefreshBtn_ = new QPushButton("Refresh NDI sources");
        pl->addWidget(ndiRefreshBtn_);
        pl->addStretch();
        propsStack_->addWidget(page);
    }

    // ── DeckLink properties page (stack index 1) ─────────────────────────
    {
        auto* page = new QWidget;
        auto* pl   = new QVBoxLayout(page);
        pl->setContentsMargins(0, 4, 0, 0);
        pl->setSpacing(4);

        dlWarning_ = new QLabel;
        dlWarning_->setWordWrap(true);
        dlWarning_->setVisible(false);
        dlWarning_->setStyleSheet(
            "color: #cc9900; font-size: 11px;"
            "padding: 8px 10px; border: 1px solid palette(mid); border-radius: 4px;");
        pl->addWidget(dlWarning_);

        dlConnLabel_ = new QLabel("Input:");
        pl->addWidget(dlConnLabel_);
        dlConnCombo_ = new QComboBox;
        dlConnCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pl->addWidget(dlConnCombo_);

        dlModeLabel_ = new QLabel("Mode:");
        pl->addWidget(dlModeLabel_);
        dlModeCombo_ = new QComboBox;
        dlModeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pl->addWidget(dlModeCombo_);

        dlAllow10Bit_ = new QCheckBox("Allow 10-bit capture");
        dlAllow10Bit_->setChecked(true);
        dlAllow10Bit_->setToolTip(
            "Use 10-bit YUV capture format when the signal supports it.\n"
            "Disable for 8-bit-only capture cards (Intensity Shuttle, older Mini Recorder).");
        pl->addWidget(dlAllow10Bit_);

        auto* btnRow = new QHBoxLayout;
        dlRefreshBtn_ = new QPushButton("Refresh");
        btnRow->addWidget(dlRefreshBtn_);
        btnRow->addStretch();
        pl->addLayout(btnRow);

        pl->addStretch();
        propsStack_->addWidget(page);
    }

    // ── Signal connections ────────────────────────────────────────────────

    connect(masterCombo_, &QComboBox::currentIndexChanged,
            this,         &StreamSourcePanel::onSourceSelected);

    connect(ndiRefreshBtn_, &QPushButton::clicked, this, [this]() {
        if (ndi_) ndi_->discoverSources();
    });

    if (ndi_) {
        connect(ndi_, &NdiReceiver::sourcesChanged, this, [this](QStringList sources) {
            ndiSources_ = sources;
            rebuildCombo();
        });
    }

#if WEBCAM_AVAILABLE
    auto* mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::videoInputsChanged,
            this,         &StreamSourcePanel::rebuildCombo);
#endif

    connect(dlRefreshBtn_, &QPushButton::clicked, this, &StreamSourcePanel::rebuildCombo);

    connect(dlConnCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (settingSource_) return;
        populateDecklinkModes(currentId());
        emitDecklinkSelection();
    });
    connect(dlModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (settingSource_) return;
        emitDecklinkSelection();
    });
    connect(dlAllow10Bit_, &QCheckBox::toggled, this, [this](bool) {
        if (settingSource_) return;
        emitDecklinkSelection();
    });

    rebuildCombo();
    QTimer::singleShot(0, this, [this]() {
        if (ndi_) ndi_->discoverSources();
    });
}

// ── Private helpers ───────────────────────────────────────────────────────────

void StreamSourcePanel::rebuildCombo() {
    settingSource_ = true;

    const SrcType prevType = currentType();
    const QString prevId   = currentId();

    comboModel_->clear();

    const QColor dimColor = qApp->palette().color(QPalette::PlaceholderText);

    auto addHeader = [&](const QString& label) {
        auto* item = new QStandardItem("── " + label + " ──");
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(dimColor);
        comboModel_->appendRow(item);
    };

    auto addEntry = [&](SrcType type, const QString& display, const QString& id) {
        auto* item = new QStandardItem(display);
        item->setData(static_cast<int>(type), TypeRole);
        item->setData(id, IdRole);
        comboModel_->appendRow(item);
    };

    auto addPlaceholder = [&](const QString& text) {
        auto* item = new QStandardItem(text);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(dimColor);
        comboModel_->appendRow(item);
    };

    // NDI section — always shown
    addHeader("NDI");
    if (ndiSources_.isEmpty())
        addPlaceholder("    No NDI sources found");
    else
        for (const auto& src : ndiSources_)
            addEntry(SrcType::Ndi, src, src);

    // Webcam section — always shown
    addHeader("Webcam");
#if WEBCAM_AVAILABLE
    const auto cams = QMediaDevices::videoInputs();
    if (cams.isEmpty())
        addPlaceholder("    No cameras found");
    else
        for (const auto& dev : cams)
            addEntry(SrcType::Webcam, dev.description(), dev.description());
#else
    addPlaceholder("    Not available on this platform");
#endif

    // DeckLink section — always shown
    addHeader("DeckLink");
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    {
        QString err;
        const auto devs = DeckLinkCapture::listDeviceInfos(&err);
        if (dlWarning_) { dlWarning_->setVisible(!err.isEmpty()); dlWarning_->setText(err); }
        if (devs.isEmpty())
            addPlaceholder("    No DeckLink devices found");
        else
            for (const auto& dev : devs)
                addEntry(SrcType::DeckLink, dev.displayName, dev.persistentId);
    }
#else
    addPlaceholder("    Not available on this platform");
#endif

    // Restore previous selection, else pick first selectable item
    bool restored = false;
    if (prevType != SrcType::None) {
        for (int i = 0; i < comboModel_->rowCount(); ++i) {
            auto* item = comboModel_->item(i);
            if (item
                && item->data(TypeRole).toInt() == static_cast<int>(prevType)
                && item->data(IdRole).toString() == prevId) {
                masterCombo_->setCurrentIndex(i);
                restored = true;
                break;
            }
        }
    }
    if (!restored) {
        for (int i = 0; i < comboModel_->rowCount(); ++i) {
            if (comboModel_->item(i)->flags() & Qt::ItemIsEnabled) {
                masterCombo_->setCurrentIndex(i);
                break;
            }
        }
    }

    settingSource_ = false;
    onSourceSelected(masterCombo_->currentIndex());
}

void StreamSourcePanel::onSourceSelected(int index) {
    if (settingSource_) return;
    if (index < 0 || index >= comboModel_->rowCount()) {
        propsStack_->setVisible(false);
        return;
    }
    auto* item = comboModel_->item(index);
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        propsStack_->setVisible(false);
        return;
    }

    const auto    type = static_cast<SrcType>(item->data(TypeRole).toInt());
    const QString id   = item->data(IdRole).toString();

    switch (type) {
    case SrcType::Ndi:
        propsStack_->setCurrentIndex(0);
        propsStack_->setVisible(true);
        if (!id.isEmpty()) emit ndiSourceSelected(id);
        break;

    case SrcType::Webcam:
        propsStack_->setVisible(false);
        if (!id.isEmpty()) emit webcamSourceSelected(id);
        break;

    case SrcType::DeckLink:
        settingSource_ = true;
        populateDecklinkConnections(id);
        settingSource_ = false;
        propsStack_->setCurrentIndex(1);
        propsStack_->setVisible(true);
        emitDecklinkSelection();
        break;

    default:
        propsStack_->setVisible(false);
        break;
    }
}

void StreamSourcePanel::emitDecklinkSelection() {
    const QString   pid  = currentId();
    const QString   conn = dlConnCombo_->currentData().toString();
    const uint32_t  mode = static_cast<uint32_t>(dlModeCombo_->currentData().toUInt());
    const bool      b10  = dlAllow10Bit_->isChecked();
    if (!pid.isEmpty())
        emit decklinkSourceSelected(pid, conn, mode, b10);
}

void StreamSourcePanel::populateDecklinkConnections(const QString& pid) {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    const QString prev = dlConnCombo_->currentData().toString();
    dlConnCombo_->clear();

    const auto conns = DeckLinkCapture::supportedConnections(pid);
    const bool multi = conns.size() > 1;
    dlConnLabel_->setVisible(multi);
    dlConnCombo_->setVisible(multi);

    for (const auto& c : conns) {
        const QString n = DeckLinkCapture::connectionName(c);
        dlConnCombo_->addItem(n, n);
    }
    const int idx = dlConnCombo_->findData(prev);
    dlConnCombo_->setCurrentIndex(idx >= 0 ? idx : 0);

    populateDecklinkModes(pid);
#else
    (void)pid;
#endif
}

void StreamSourcePanel::populateDecklinkModes(const QString& pid) {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    const uint32_t prev = static_cast<uint32_t>(dlModeCombo_->currentData().toUInt());
    dlModeCombo_->clear();

    const auto modes = DeckLinkCapture::listDisplayModes(pid);
    const bool multi = modes.size() > 1;
    dlModeLabel_->setVisible(multi);
    dlModeCombo_->setVisible(multi);

    for (const auto& m : modes)
        dlModeCombo_->addItem(m.name, static_cast<uint>(m.mode));

    int idx = -1;
    for (int i = 0; i < dlModeCombo_->count(); ++i) {
        if (static_cast<uint32_t>(dlModeCombo_->itemData(i).toUInt()) == prev) { idx = i; break; }
    }
    dlModeCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
#else
    (void)pid;
#endif
}

// ── Public API ────────────────────────────────────────────────────────────────

StreamSourcePanel::SrcType StreamSourcePanel::currentType() const {
    const int idx = masterCombo_->currentIndex();
    if (idx < 0 || idx >= comboModel_->rowCount()) return SrcType::None;
    auto* item = comboModel_->item(idx);
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return SrcType::None;
    return static_cast<SrcType>(item->data(TypeRole).toInt());
}

QString StreamSourcePanel::currentId() const {
    const int idx = masterCombo_->currentIndex();
    if (idx < 0 || idx >= comboModel_->rowCount()) return {};
    auto* item = comboModel_->item(idx);
    return item ? item->data(IdRole).toString() : QString{};
}

QString StreamSourcePanel::selectedNdiSource() const {
    return currentType() == SrcType::Ndi ? currentId() : QString{};
}

void StreamSourcePanel::setCurrentNdiSource(const QString& source) {
    if (source.isEmpty()) return;
    for (int i = 0; i < comboModel_->rowCount(); ++i) {
        auto* item = comboModel_->item(i);
        if (item
            && item->data(TypeRole).toInt() == static_cast<int>(SrcType::Ndi)
            && item->data(IdRole).toString() == source) {
            settingSource_ = true;
            masterCombo_->setCurrentIndex(i);
            settingSource_ = false;
            return;
        }
    }
    // Source not yet discovered — add it as a placeholder so the project remembers it
    if (!ndiSources_.contains(source)) {
        ndiSources_.prepend(source);
        rebuildCombo();
        setCurrentNdiSource(source);
    }
}

void StreamSourcePanel::setCurrentDecklinkSource(const QString& deviceId,
                                                   const QString& connection,
                                                   uint32_t displayMode, bool allow10Bit) {
    for (int i = 0; i < comboModel_->rowCount(); ++i) {
        auto* item = comboModel_->item(i);
        if (!item || item->data(TypeRole).toInt() != static_cast<int>(SrcType::DeckLink)) continue;
        if (item->data(IdRole).toString() != deviceId) continue;

        settingSource_ = true;
        masterCombo_->setCurrentIndex(i);
        populateDecklinkConnections(deviceId);
        const int ci = dlConnCombo_->findData(connection);
        if (ci >= 0) dlConnCombo_->setCurrentIndex(ci);
        populateDecklinkModes(deviceId);
        for (int m = 0; m < dlModeCombo_->count(); ++m) {
            if (static_cast<uint32_t>(dlModeCombo_->itemData(m).toUInt()) == displayMode) {
                dlModeCombo_->setCurrentIndex(m);
                break;
            }
        }
        dlAllow10Bit_->setChecked(allow10Bit);
        settingSource_ = false;
        propsStack_->setCurrentIndex(1);
        propsStack_->setVisible(true);
        return;
    }
}
