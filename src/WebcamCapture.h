#pragma once

#include <QObject>
#include <QImage>
#include <QString>

#if WEBCAM_AVAILABLE
class QCamera;
class QVideoSink;
class QMediaCaptureSession;
#endif

class WebcamCapture : public QObject {
    Q_OBJECT
public:
    explicit WebcamCapture(QObject* parent = nullptr);
    ~WebcamCapture() override;

    void    setDeviceDescription(const QString& description);
    QString deviceDescription() const;

    void start();
    void stop();

signals:
    void frameReady(const QImage& frame);
    void errorChanged(const QString& error);

private:
    QString deviceDescription_;

#if WEBCAM_AVAILABLE
    QCamera*              camera_ = nullptr;
    QMediaCaptureSession* session_ = nullptr;
    QVideoSink*           sink_ = nullptr;

    qint64 lastFrameMs_ = -1;

    void rebuildCamera();
#endif
};
