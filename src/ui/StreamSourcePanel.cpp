#include "StreamSourcePanel.h"
#include "../NdiReceiver.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QFrame>
#include <QMap>
#include <QSizePolicy>
#include "../DeckLinkCapture.h"
#if WEBCAM_AVAILABLE
#  include <QMediaDevices>
#  include <QCameraDevice>
#endif

// ── Abstract base for all source tabs ────────────────────────────────────────

class VideoSourceTab : public QWidget {
    Q_OBJECT
public:
    explicit VideoSourceTab(QWidget* parent = nullptr) : QWidget(parent) {}
    ~VideoSourceTab() override = default;

    virtual void    refreshSources() = 0;
    virtual QString selectedSource() const = 0;
    virtual void    setCurrentSource(const QString& name) = 0;

signals:
    void sourceActivated(const QString& source);
};

// ── NDI tab ───────────────────────────────────────────────────────────────────

class NdiSourceTab : public VideoSourceTab {
    Q_OBJECT
public:
    explicit NdiSourceTab(NdiReceiver* ndi, QWidget* parent = nullptr)
        : VideoSourceTab(parent), ndi_(ndi)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        combo_ = new QComboBox;
        combo_->setMinimumContentsLength(0);
        combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo_->setPlaceholderText("No sources found");
        layout->addWidget(combo_);

        auto* btnRow = new QHBoxLayout;
        refreshBtn_ = new QPushButton("Refresh");
        btnRow->addWidget(refreshBtn_);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        layout->addStretch();

        emitCurrentSourceEndpoint();

        connect(refreshBtn_, &QPushButton::clicked, this, &NdiSourceTab::refreshSources);
        connect(combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            emitCurrentSourceEndpoint();
            if (!settingSource_ && !text.isEmpty())
                emit sourceActivated(text);
        });

        if (ndi_) {
            connect(ndi_, &NdiReceiver::sourcesChanged, this, [this](QStringList sources) {
                settingSource_ = true;

                const QString previous = combo_->currentText();
                combo_->clear();
                combo_->addItems(sources);

                int nextIndex = -1;
                if (!previous.isEmpty())
                    nextIndex = combo_->findText(previous);
                if (nextIndex < 0 && combo_->count() > 0)
                    nextIndex = 0;

                bool shouldActivate = false;
                if (nextIndex >= 0) {
                    combo_->setCurrentIndex(nextIndex);
                    shouldActivate = (previous.isEmpty() || combo_->currentText() != previous);
                }

                settingSource_ = false;

                if (shouldActivate && !combo_->currentText().isEmpty())
                    emit sourceActivated(combo_->currentText());
                emitCurrentSourceEndpoint();
            });
            connect(ndi_, &NdiReceiver::sourceEndpointChanged,
                    this, [this](const QString& sourceName, const QString& urlAddress) {
                sourceEndpoints_[sourceName] = urlAddress;
                if (sourceName == combo_->currentText())
                    emitCurrentSourceEndpoint();
            });
        }

        QTimer::singleShot(0, this, [this]() { refreshSources(); });
    }

    void refreshSources() override {
        if (ndi_) ndi_->discoverSources();
    }

    QString selectedSource() const override {
        return combo_->currentText();
    }

    void setCurrentSource(const QString& name) override {
        settingSource_ = true;
        const int idx = combo_->findText(name);
        if (idx >= 0) {
            combo_->setCurrentIndex(idx);
        } else if (!name.isEmpty()) {
            combo_->addItem(name);
            combo_->setCurrentIndex(combo_->count() - 1);
        }
        settingSource_ = false;
        emitCurrentSourceEndpoint();
    }


signals:
    void sourceEndpointChanged(const QString& sourceName, const QString& urlAddress);

private:
    void emitCurrentSourceEndpoint() {
        const QString source = combo_->currentText();
        emit sourceEndpointChanged(source, sourceEndpoints_.value(source));
    }

    NdiReceiver* ndi_;
    QComboBox*   combo_;
    QPushButton* refreshBtn_;
    QMap<QString, QString> sourceEndpoints_;
    bool         settingSource_ = false;
};

// ── Webcam tab ────────────────────────────────────────────────────────────────

class WebcamSourceTab : public VideoSourceTab {
    Q_OBJECT
public:
    explicit WebcamSourceTab(QWidget* parent = nullptr) : VideoSourceTab(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        combo_ = new QComboBox;
        combo_->setMinimumContentsLength(0);
        combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo_->setPlaceholderText("No cameras found");
        layout->addWidget(combo_);

        auto* btnRow = new QHBoxLayout;
        refreshBtn_ = new QPushButton("Refresh");
        btnRow->addWidget(refreshBtn_);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        layout->addStretch();

        connect(refreshBtn_, &QPushButton::clicked, this, &WebcamSourceTab::refreshSources);
        connect(combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            if (!settingSource_ && !text.isEmpty())
                emit sourceActivated(text);
        });

    #if WEBCAM_AVAILABLE
        mediaDevices_ = new QMediaDevices(this);
        connect(mediaDevices_, &QMediaDevices::videoInputsChanged,
            this,         &WebcamSourceTab::refreshSources);
    #endif

        refreshSources();
    }

    void refreshSources() override {
        settingSource_ = true;

        const QString previous = combo_->currentText();
        combo_->clear();
#if WEBCAM_AVAILABLE
        for (const auto& dev : QMediaDevices::videoInputs())
            combo_->addItem(dev.description());
#endif

        int nextIndex = -1;
        if (!previous.isEmpty())
            nextIndex = combo_->findText(previous);
        if (nextIndex < 0 && combo_->count() > 0)
            nextIndex = 0;

        bool shouldActivate = false;
        if (nextIndex >= 0) {
            combo_->setCurrentIndex(nextIndex);
            shouldActivate = (previous.isEmpty() || combo_->currentText() != previous);
        }

        settingSource_ = false;

        if (shouldActivate && !combo_->currentText().isEmpty())
            emit sourceActivated(combo_->currentText());
    }

    QString selectedSource() const override {
        return combo_->currentText();
    }

    void setCurrentSource(const QString& name) override {
        settingSource_ = true;
        const int idx = combo_->findText(name);
        if (idx >= 0) combo_->setCurrentIndex(idx);
        settingSource_ = false;
    }

private:
    QComboBox*   combo_;
    QPushButton* refreshBtn_;
    bool         settingSource_ = false;

#if WEBCAM_AVAILABLE
    QMediaDevices* mediaDevices_ = nullptr;
#endif
};

// ── DeckLink tab ──────────────────────────────────────────────────────────────

class DecklinkSourceTab : public VideoSourceTab {
    Q_OBJECT
public:
    explicit DecklinkSourceTab(QWidget* parent = nullptr) : VideoSourceTab(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        warningLabel_ = new QLabel;
        warningLabel_->setWordWrap(true);
        warningLabel_->setVisible(false);
        warningLabel_->setStyleSheet(
            "color: #cc9900;"
            "font-size: 11px;"
            "padding: 8px 10px;"
            "border: 1px solid palette(mid);"
            "border-radius: 4px;"
        );
        layout->addWidget(warningLabel_);

        combo_ = new QComboBox;
        combo_->setMinimumContentsLength(16);
        combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo_->setPlaceholderText("No DeckLink devices found");
        layout->addWidget(combo_);

        connLabel_ = new QLabel("Input:");
        layout->addWidget(connLabel_);

        connCombo_ = new QComboBox;
        connCombo_->setMinimumContentsLength(10);
        connCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        layout->addWidget(connCombo_);

        modeLabel_ = new QLabel("Mode:");
        layout->addWidget(modeLabel_);

        modeCombo_ = new QComboBox;
        modeCombo_->setMinimumContentsLength(10);
        modeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        layout->addWidget(modeCombo_);

        allow10BitCheck_ = new QCheckBox("Allow 10-bit capture");
        allow10BitCheck_->setChecked(true);
        allow10BitCheck_->setToolTip(
            "Use 10-bit YUV capture format when the signal supports it.\n"
            "Disable for 8-bit-only capture cards (Intensity Shuttle, older Mini Recorder).");
        layout->addWidget(allow10BitCheck_);

        auto* btnRow = new QHBoxLayout;
        refreshBtn_ = new QPushButton("Refresh");
        btnRow->addWidget(refreshBtn_);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        layout->addStretch();

        connect(refreshBtn_, &QPushButton::clicked, this, &DecklinkSourceTab::refreshSources);

        connect(combo_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (settingSource_) return;
            const QString pid = combo_->currentData().toString();
            if (pid.isEmpty()) return;
            populateConnections(pid);
            emit sourceActivated(pid);
        });

        connect(connCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (settingSource_) return;
            const QString pid = combo_->currentData().toString();
            populateDisplayModes(pid);
            if (!pid.isEmpty()) emit sourceActivated(pid);
        });

        connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (settingSource_) return;
            const QString pid = combo_->currentData().toString();
            if (!pid.isEmpty()) emit sourceActivated(pid);
        });

        connect(allow10BitCheck_, &QCheckBox::toggled, this, [this](bool) {
            if (settingSource_) return;
            const QString pid = combo_->currentData().toString();
            if (!pid.isEmpty()) emit sourceActivated(pid);
        });

        refreshSources();
#else
        auto* label = new QLabel("DeckLink is not available on this platform.");
        label->setWordWrap(true);
        label->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
        layout->addWidget(label);
        layout->addStretch();
#endif
    }

    // Returns the persistent ID of the selected device.
    QString selectedSource() const override {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        return combo_ ? combo_->currentData().toString() : QString{};
#else
        return {};
#endif
    }

    QString selectedConnection() const {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        return connCombo_ ? connCombo_->currentData().toString() : QString{};
#else
        return {};
#endif
    }

    uint32_t selectedDisplayMode() const {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        return modeCombo_ ? static_cast<uint32_t>(modeCombo_->currentData().toUInt()) : 0u;
#else
        return 0u;
#endif
    }

    bool selectedAllow10Bit() const {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        return allow10BitCheck_ ? allow10BitCheck_->isChecked() : true;
#else
        return true;
#endif
    }

    void setCurrentDecklinkDevice(const QString& persistentId, const QString& connection,
                                   uint32_t displayMode, bool allow10Bit) {
#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
        settingSource_ = true;

        int idx = -1;
        for (int i = 0; i < combo_->count(); ++i) {
            if (combo_->itemData(i).toString() == persistentId) { idx = i; break; }
        }
        if (idx >= 0) combo_->setCurrentIndex(idx);

        if (!combo_->currentData().toString().isEmpty())
            populateConnections(combo_->currentData().toString());

        const int connIdx = connCombo_->findData(connection);
        if (connIdx >= 0) connCombo_->setCurrentIndex(connIdx);

        populateDisplayModes(combo_->currentData().toString());

        // Restore mode
        for (int i = 0; i < modeCombo_->count(); ++i) {
            if (static_cast<uint32_t>(modeCombo_->itemData(i).toUInt()) == displayMode) {
                modeCombo_->setCurrentIndex(i);
                break;
            }
        }

        allow10BitCheck_->setChecked(allow10Bit);

        settingSource_ = false;
#else
        (void)persistentId; (void)connection; (void)displayMode; (void)allow10Bit;
#endif
    }

#if defined(DECKLINK_AVAILABLE) && DECKLINK_AVAILABLE
    void refreshSources() override {
        settingSource_ = true;

        const QString prevPid = combo_->currentData().toString();
        combo_->clear();

        QString err;
        const auto devs = DeckLinkCapture::listDeviceInfos(&err);
        const bool apiMissing = !err.isEmpty();
        warningLabel_->setVisible(apiMissing);
        warningLabel_->setText(err);
        combo_->setVisible(!apiMissing);
        connLabel_->setVisible(!apiMissing);
        connCombo_->setVisible(!apiMissing);
        modeLabel_->setVisible(!apiMissing);
        modeCombo_->setVisible(!apiMissing);
        allow10BitCheck_->setVisible(!apiMissing);
        refreshBtn_->setVisible(!apiMissing);

        if (apiMissing) {
            settingSource_ = false;
            return;
        }

        for (const auto& d : devs)
            combo_->addItem(d.displayName, d.persistentId);

        // Restore previous selection by persistent ID.
        int nextIndex = -1;
        for (int i = 0; i < combo_->count(); ++i) {
            if (combo_->itemData(i).toString() == prevPid) { nextIndex = i; break; }
        }
        if (nextIndex < 0 && combo_->count() > 0) nextIndex = 0;
        if (nextIndex >= 0) combo_->setCurrentIndex(nextIndex);

        const QString pid = combo_->currentData().toString();
        if (!pid.isEmpty()) {
            populateConnections(pid);
        }

        settingSource_ = false;

        if (!pid.isEmpty()) emit sourceActivated(pid);
    }

    void setCurrentSource(const QString& persistentId) override {
        settingSource_ = true;

        int idx = -1;
        for (int i = 0; i < combo_->count(); ++i) {
            if (combo_->itemData(i).toString() == persistentId) { idx = i; break; }
        }
        if (idx >= 0) combo_->setCurrentIndex(idx);

        const QString pid = combo_->currentData().toString();
        if (!pid.isEmpty()) populateConnections(pid);

        settingSource_ = false;
    }

private:
    void populateConnections(const QString& persistentId) {
        settingSource_ = true;

        const QString prevConn = connCombo_->currentData().toString();
        connCombo_->clear();

        const auto conns = DeckLinkCapture::supportedConnections(persistentId);

        if (conns.size() <= 1) {
            connLabel_->setVisible(false);
            connCombo_->setVisible(false);
            if (!conns.isEmpty()) {
                const QString n = DeckLinkCapture::connectionName(conns.first());
                connCombo_->addItem(n, n);
                connCombo_->setCurrentIndex(0);
            }
        } else {
            connLabel_->setVisible(true);
            connCombo_->setVisible(true);
            for (const auto& c : conns) {
                const QString n = DeckLinkCapture::connectionName(c);
                connCombo_->addItem(n, n);
            }
            const int idx = connCombo_->findData(prevConn);
            connCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
        }

        settingSource_ = false;
        populateDisplayModes(persistentId);
    }

    void populateDisplayModes(const QString& persistentId) {
        settingSource_ = true;

        const uint32_t prevMode = static_cast<uint32_t>(modeCombo_->currentData().toUInt());
        modeCombo_->clear();

        const auto modes = DeckLinkCapture::listDisplayModes(persistentId);

        if (modes.size() <= 1) {
            modeLabel_->setVisible(false);
            modeCombo_->setVisible(false);
            for (const auto& m : modes)
                modeCombo_->addItem(m.name, static_cast<uint>(m.mode));
            if (!modes.isEmpty()) modeCombo_->setCurrentIndex(0);
        } else {
            modeLabel_->setVisible(true);
            modeCombo_->setVisible(true);
            for (const auto& m : modes)
                modeCombo_->addItem(m.name, static_cast<uint>(m.mode));

            int idx = -1;
            for (int i = 0; i < modeCombo_->count(); ++i) {
                if (static_cast<uint32_t>(modeCombo_->itemData(i).toUInt()) == prevMode) {
                    idx = i; break;
                }
            }
            modeCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
        }

        settingSource_ = false;
    }

    QComboBox*   combo_          = nullptr;
    QLabel*      connLabel_      = nullptr;
    QComboBox*   connCombo_      = nullptr;
    QLabel*      modeLabel_      = nullptr;
    QComboBox*   modeCombo_      = nullptr;
    QCheckBox*   allow10BitCheck_ = nullptr;
    QPushButton* refreshBtn_     = nullptr;
    QLabel*      warningLabel_   = nullptr;
    bool         settingSource_  = false;
#else
    void    refreshSources() override {}
    void    setCurrentSource(const QString&) override {}
#endif
};

// ── StreamSourcePanel ─────────────────────────────────────────────────────────

StreamSourcePanel::StreamSourcePanel(NdiReceiver* ndi, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tabs_        = new QTabWidget;
    ndiTab_      = new NdiSourceTab(ndi);
    webcamTab_   = new WebcamSourceTab;
    decklinkTab_ = new DecklinkSourceTab;

    tabs_->addTab(ndiTab_,       "NDI");
    tabs_->addTab(webcamTab_,    "Webcam");
    tabs_->addTab(decklinkTab_,  "DeckLink");

    layout->addWidget(tabs_);

    connect(ndiTab_, &VideoSourceTab::sourceActivated,
            this,    &StreamSourcePanel::ndiSourceSelected);
    connect(ndiTab_, &NdiSourceTab::sourceEndpointChanged,
            this,    &StreamSourcePanel::ndiSourceEndpointChanged);

    connect(webcamTab_, &VideoSourceTab::sourceActivated,
            this,       &StreamSourcePanel::webcamSourceSelected);

    connect(decklinkTab_, &VideoSourceTab::sourceActivated,
            this, [this](const QString& devId) {
        emit decklinkSourceSelected(devId,
                                    decklinkTab_->selectedConnection(),
                                    decklinkTab_->selectedDisplayMode(),
                                    decklinkTab_->selectedAllow10Bit());
    });

    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        QWidget* w = tabs_->widget(idx);
        if (w == ndiTab_) {
            const QString src = ndiTab_->selectedSource();
            if (!src.isEmpty()) emit ndiSourceSelected(src);
        } else if (w == webcamTab_) {
            const QString dev = webcamTab_->selectedSource();
            if (!dev.isEmpty()) emit webcamSourceSelected(dev);
        } else if (w == decklinkTab_) {
            const QString dev = decklinkTab_->selectedSource();
            if (!dev.isEmpty())
                emit decklinkSourceSelected(dev,
                                            decklinkTab_->selectedConnection(),
                                            decklinkTab_->selectedDisplayMode(),
                                            decklinkTab_->selectedAllow10Bit());
        }
    });
}

QString StreamSourcePanel::selectedNdiSource() const {
    return ndiTab_->selectedSource();
}

void StreamSourcePanel::setCurrentNdiSource(const QString& source) {
    ndiTab_->setCurrentSource(source);
}

void StreamSourcePanel::setCurrentDecklinkSource(const QString& deviceId,
                                                  const QString& connection,
                                                  uint32_t displayMode, bool allow10Bit) {
    decklinkTab_->setCurrentDecklinkDevice(deviceId, connection, displayMode, allow10Bit);
}

#include "StreamSourcePanel.moc"
