#pragma once
#include <QWidget>
#include <QStringList>
#include <cstdint>

class NdiReceiver;
class QComboBox;
class QStackedWidget;
class QLabel;
class QCheckBox;
class QPushButton;
class QStandardItemModel;

class StreamSourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit StreamSourcePanel(NdiReceiver* ndi, QWidget* parent = nullptr);

    QString selectedNdiSource() const;
    void    setCurrentNdiSource(const QString& source);
    void    setCurrentDecklinkSource(const QString& deviceId, const QString& connection,
                                     uint32_t displayMode, bool allow10Bit);

signals:
    void ndiSourceSelected(const QString& source);
    void webcamSourceSelected(const QString& device);
    void decklinkSourceSelected(const QString& deviceId, const QString& connection,
                                uint32_t displayMode, bool allow10Bit);

private:
    enum class SrcType : int { None = 0, Ndi = 1, Webcam = 2, DeckLink = 3 };

    void rebuildCombo();
    void onSourceSelected(int index);
    void populateDecklinkConnections(const QString& pid);
    void populateDecklinkModes(const QString& pid);
    void emitDecklinkSelection();
    SrcType currentType() const;
    QString currentId()   const;

    NdiReceiver*        ndi_          = nullptr;
    QStringList         ndiSources_;
    QStringList         lastNdiSources_;  // Cache to detect changes

    QComboBox*          masterCombo_  = nullptr;
    QStandardItemModel* comboModel_   = nullptr;
    QStackedWidget*     propsStack_   = nullptr;

    // NDI props page (stack index 0)
    QPushButton* ndiRefreshBtn_ = nullptr;

    // DeckLink props page (stack index 1)
    QLabel*      dlWarning_     = nullptr;
    QLabel*      dlConnLabel_   = nullptr;
    QComboBox*   dlConnCombo_   = nullptr;
    QLabel*      dlModeLabel_   = nullptr;
    QComboBox*   dlModeCombo_   = nullptr;
    QCheckBox*   dlAllow10Bit_  = nullptr;
    QPushButton* dlRefreshBtn_  = nullptr;

    bool settingSource_ = false;
};
