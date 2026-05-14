#pragma once
#include <QWidget>
#include <QString>

class NdiReceiver;
class QTabWidget;
class VideoSourceTab;

class StreamSourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit StreamSourcePanel(NdiReceiver* ndi, QWidget* parent = nullptr);

    QString selectedNdiSource() const;
    void    setCurrentNdiSource(const QString& source);

signals:
    void ndiSourceSelected(const QString& source);
    void webcamSourceSelected(const QString& device);
    void decklinkSourceSelected(const QString& device);

private:
    QTabWidget*     tabs_;
    VideoSourceTab* ndiTab_;
    VideoSourceTab* webcamTab_;
    VideoSourceTab* decklinkTab_;
};
