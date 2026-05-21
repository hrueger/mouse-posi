#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>

class NdiReceiver;
class QTabWidget;
class VideoSourceTab;
class DecklinkSourceTab;

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
    QTabWidget*        tabs_;
    VideoSourceTab*    ndiTab_;
    VideoSourceTab*    webcamTab_;
    DecklinkSourceTab* decklinkTab_;
};
