#include "WebcamCapture.h"

#if WEBCAM_AVAILABLE
#  include <QMediaDevices>
#  include <QCamera>
#  include <QCameraDevice>
#  include <QCameraFormat>
#  include <QMediaCaptureSession>
#  include <QVideoSink>
#  include <QVideoFrame>
#  include <QDateTime>
#  include <QCoreApplication>
#  include <QPermission>
#endif

WebcamCapture::WebcamCapture(QObject* parent) : QObject(parent) {}

WebcamCapture::~WebcamCapture() {
    stop();
}

void WebcamCapture::setDeviceDescription(const QString& description) {
    if (deviceDescription_ == description)
        return;

    deviceDescription_ = description;

#if WEBCAM_AVAILABLE
    if (camera_) {
        rebuildCamera();
        start();
    }
#endif
}

QString WebcamCapture::deviceDescription() const {
    return deviceDescription_;
}

void WebcamCapture::start() {
#if WEBCAM_AVAILABLE
    QCameraPermission perm;
    switch (qApp->checkPermission(perm)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(perm, this, &WebcamCapture::start);
        return;
    case Qt::PermissionStatus::Denied:
        emit errorChanged(QStringLiteral("Camera permission denied"));
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }

    if (!camera_)
        rebuildCamera();

    if (!camera_) {
        emit errorChanged(QStringLiteral("No camera available"));
        return;
    }

    emit errorChanged(QString());
    camera_->start();
#else
    emit errorChanged(QStringLiteral("Webcam support is not available (Qt Multimedia missing)"));
#endif
}

void WebcamCapture::stop() {
#if WEBCAM_AVAILABLE
    if (camera_)
        camera_->stop();
#endif
}

#if WEBCAM_AVAILABLE
static QCameraDevice findDeviceByDescription(const QString& description) {
    const auto devices = QMediaDevices::videoInputs();
    if (devices.isEmpty())
        return {};

    if (!description.isEmpty()) {
        for (const auto& dev : devices) {
            if (dev.description() == description)
                return dev;
        }
    }

    return devices.first();
}

static QCameraFormat choosePreferredFormat(const QCameraDevice& device) {
    const auto formats = device.videoFormats();
    if (formats.isEmpty())
        return {};

    auto aspectScore = [](const QSize& r) {
        if (r.height() <= 0) return 0.0;
        const double ar = double(r.width()) / double(r.height());
        const double target = 16.0 / 9.0;
        return 1.0 / (1.0 + std::abs(ar - target));
    };

    auto exactPref = [](const QSize& r) {
        if (r == QSize(1920, 1080)) return 3;
        if (r == QSize(1280, 720))  return 2;
        if (r == QSize(640,  480))  return 1;
        return 0;
    };

    QCameraFormat best = formats.first();
    double bestScore = -1.0;

    for (const auto& f : formats) {
        const QSize r = f.resolution();
        const double arS = aspectScore(r);
        const double pref = double(exactPref(r));
        const double pixels = double(r.width()) * double(r.height());
        const double fps = f.maxFrameRate();

        // Prioritize 16:9 strongly, then known-good resolutions, then pixels & fps.
        const double score = arS * 1000.0 + pref * 100.0 + pixels / 1e6 + fps;
        if (score > bestScore) {
            bestScore = score;
            best = f;
        }
    }

    return best;
}

void WebcamCapture::rebuildCamera() {
    const QCameraDevice device = findDeviceByDescription(deviceDescription_);
    if (device.isNull()) {
        if (camera_) {
            camera_->stop();
            camera_->deleteLater();
            camera_ = nullptr;
        }
        if (session_) {
            session_->deleteLater();
            session_ = nullptr;
        }
        if (sink_) {
            sink_->deleteLater();
            sink_ = nullptr;
        }
        return;
    }

    if (!sink_) {
        sink_ = new QVideoSink(this);
        connect(sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
            if (!frame.isValid())
                return;

            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (lastFrameMs_ >= 0 && now - lastFrameMs_ < 33)
                return;
            lastFrameMs_ = now;

            QVideoFrame f(frame);
            QImage img = f.toImage();
            if (img.isNull())
                return;

            emit frameReady(img.copy());
        });
    }

    if (!session_)
        session_ = new QMediaCaptureSession(this);

    if (camera_) {
        camera_->stop();
        camera_->deleteLater();
        camera_ = nullptr;
    }

    camera_ = new QCamera(device, this);

    const QCameraFormat fmt = choosePreferredFormat(device);
    if (!fmt.isNull())
        camera_->setCameraFormat(fmt);

    session_->setCamera(camera_);
    session_->setVideoSink(sink_);
}
#endif
