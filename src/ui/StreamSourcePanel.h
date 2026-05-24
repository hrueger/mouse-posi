#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>
#include "../CameraControl.h"

class NdiReceiver;
class QTabWidget;
class VideoSourceTab;
class NdiSourceTab;
class DecklinkSourceTab;

class StreamSourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit StreamSourcePanel(NdiReceiver* ndi, QWidget* parent = nullptr);

    QString selectedNdiSource() const;
    void    setCurrentNdiSource(const QString& source);
    void    setCameraControlConfig(const CameraControlConfig& config);
    void    setCurrentDecklinkSource(const QString& deviceId, const QString& connection,
                                     uint32_t displayMode, bool allow10Bit);

signals:
    void ndiSourceSelected(const QString& source);
    void webcamSourceSelected(const QString& device);
    void decklinkSourceSelected(const QString& deviceId, const QString& connection,
                                uint32_t displayMode, bool allow10Bit);
    void cameraControlConfigChanged(const CameraControlConfig& config);

private:
    QTabWidget*        tabs_;
    NdiSourceTab*      ndiTab_;
    VideoSourceTab*    webcamTab_;
    DecklinkSourceTab* decklinkTab_;
};
