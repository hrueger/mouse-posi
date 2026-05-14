#include "StreamSourcePanel.h"
#include "../NdiReceiver.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
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
        combo_->setMinimumContentsLength(16);
        combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo_->setPlaceholderText("No sources found");
        layout->addWidget(combo_);

        auto* btnRow = new QHBoxLayout;
        refreshBtn_ = new QPushButton("Refresh");
        btnRow->addWidget(refreshBtn_);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        layout->addStretch();

        connect(refreshBtn_, &QPushButton::clicked, this, &NdiSourceTab::refreshSources);
        connect(combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
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
    }

private:
    NdiReceiver* ndi_;
    QComboBox*   combo_;
    QPushButton* refreshBtn_;
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
        combo_->setMinimumContentsLength(16);
        combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
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
        layout->setContentsMargins(0, 8, 0, 0);
        auto* label = new QLabel("DeckLink support is coming soon.");
        label->setWordWrap(true);
        label->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
        layout->addWidget(label);
        layout->addStretch();
    }

    void    refreshSources() override {}
    QString selectedSource() const override { return {}; }
    void    setCurrentSource(const QString&) override {}
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

    tabs_->addTab(ndiTab_,      "NDI");
    tabs_->addTab(webcamTab_,   "Webcam");
    tabs_->addTab(decklinkTab_, "DeckLink");

    layout->addWidget(tabs_);

    connect(ndiTab_, &VideoSourceTab::sourceActivated,
            this,    &StreamSourcePanel::ndiSourceSelected);

    connect(webcamTab_, &VideoSourceTab::sourceActivated,
            this,       &StreamSourcePanel::webcamSourceSelected);

    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        QWidget* w = tabs_->widget(idx);
        if (w == ndiTab_) {
            const QString src = ndiTab_->selectedSource();
            if (!src.isEmpty()) emit ndiSourceSelected(src);
        } else if (w == webcamTab_) {
            const QString dev = webcamTab_->selectedSource();
            if (!dev.isEmpty()) emit webcamSourceSelected(dev);
        }
    });
}

QString StreamSourcePanel::selectedNdiSource() const {
    return ndiTab_->selectedSource();
}

void StreamSourcePanel::setCurrentNdiSource(const QString& source) {
    ndiTab_->setCurrentSource(source);
}

#include "StreamSourcePanel.moc"
