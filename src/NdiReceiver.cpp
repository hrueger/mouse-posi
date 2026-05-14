#include "NdiReceiver.h"
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>

#if NDI_AVAILABLE
#include <Processing.NDI.Lib.h>
#endif

NdiReceiver::NdiReceiver(QObject* parent) : QThread(parent) {}

NdiReceiver::~NdiReceiver() {
    stop();
    wait();
}

void NdiReceiver::stop() {
    running_ = false;
}

void NdiReceiver::discoverSources() {
#if NDI_AVAILABLE
    // Run discovery off the UI thread so the dialog stays responsive
    QtConcurrent::run([this]() {
        if (!NDIlib_initialize()) return;
        NDIlib_find_instance_t finder = NDIlib_find_create_v2(nullptr);
        if (!finder) return;
        NDIlib_find_wait_for_sources(finder, 2000);
        uint32_t num = 0;
        const NDIlib_source_t* srcs = NDIlib_find_get_current_sources(finder, &num);
        QStringList names;
        for (uint32_t i = 0; i < num; ++i)
            names << QString::fromUtf8(srcs[i].p_ndi_name);
        NDIlib_find_destroy(finder);
        emit sourcesChanged(names);
    });
#else
    emit sourcesChanged({"[No NDI SDK — demo mode]"});
#endif
}

void NdiReceiver::connectToSource(const QString& name) {
    {
        QMutexLocker lk(&sourceMutex_);
        targetSource_ = name;
    }
    reconnect_ = true;
    if (!isRunning()) {
        running_ = true;
        start();
    }
}

void NdiReceiver::disconnectFromSource() {
    {
        QMutexLocker lk(&sourceMutex_);
        targetSource_.clear();
    }
    reconnect_ = true;
}

void NdiReceiver::run() {
#if NDI_AVAILABLE
    if (!NDIlib_initialize()) {
        // NDI runtime missing at runtime even though SDK was linked
        QImage placeholder(1280, 720, QImage::Format_RGB32);
        placeholder.fill(Qt::darkGray);
        emit frameReady(placeholder);
        return;
    }

    while (running_) {
        reconnect_ = false;

        // Keep the QByteArray alive for the duration of recv creation
        QByteArray sourceName;
        {
            QMutexLocker lk(&sourceMutex_);
            sourceName = targetSource_.toUtf8();
        }

        if (sourceName.isEmpty()) {
            // No source selected — keep thread alive but idle.
            msleep(100);
            continue;
        }

        NDIlib_recv_create_v3_t desc = {};
        desc.source_to_connect_to.p_ndi_name = sourceName.constData();
        desc.color_format       = NDIlib_recv_color_format_BGRX_BGRA;
        desc.bandwidth          = NDIlib_recv_bandwidth_highest;
        desc.allow_video_fields = false;
        desc.p_ndi_recv_name    = "mouse-posi";

        NDIlib_recv_instance_t recv = NDIlib_recv_create_v3(&desc);
        if (!recv) { msleep(500); continue; }

        QElapsedTimer frameTimer;
        frameTimer.start();
        while (running_ && !reconnect_) {
            NDIlib_video_frame_v2_t vf;
            auto result = NDIlib_recv_capture_v3(recv, &vf, nullptr, nullptr, 20);
            if (result == NDIlib_frame_type_video) {
                // Throttle to ~30 fps — keeps NDI pipeline drained without
                // flooding the UI with full-resolution frame copies.
                if (frameTimer.elapsed() >= 33) {
                    QImage img(vf.p_data, vf.xres, vf.yres,
                               vf.line_stride_in_bytes, QImage::Format_ARGB32);
                    emit frameReady(img.copy());
                    frameTimer.restart();
                }
                NDIlib_recv_free_video_v2(recv, &vf);
            }
        }
        NDIlib_recv_destroy(recv);
    }
    NDIlib_destroy();
#else
    // Emit a placeholder gray frame at ~30fps
    while (running_) {
        QByteArray sourceName;
        {
            QMutexLocker lk(&sourceMutex_);
            sourceName = targetSource_.toUtf8();
        }
        if (sourceName.isEmpty()) {
            msleep(100);
            continue;
        }
        QImage img(1280, 720, QImage::Format_RGB32);
        img.fill(Qt::darkGray);
        emit frameReady(img);
        msleep(33);
    }
#endif
}
