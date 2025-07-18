// cameraworker.cpp
#include "cameraworker.h"

CameraWorker::CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent)
    : QObject(parent), m_deviceId(deviceId), m_host(host), m_port(port)
{
    m_videoSink = new QVideoSink(this); // Создаем новый QVideoSink
    connect(m_videoSink, &QVideoSink::videoFrameChanged,
            this, &CameraWorker::handleFrame, Qt::DirectConnection);
}

CameraWorker::~CameraWorker()
{
    stopStreaming();
    if (m_videoSink) {
        delete m_videoSink;
        m_videoSink = nullptr;
    }
}

void CameraWorker::startStreaming()
{
    ensureInWorkerThread();
    QMutexLocker locker(&m_mutex);

    if (m_isStreaming) {
        emit errorOccurred("Streaming already started");
        return;
    }

    cleanupPipeline();
    cleanupCamera();

    if (!setupCamera() || !setupPipeline()) {
        cleanupPipeline();
        cleanupCamera();
        return;
    }

    m_isStreaming = true;
    emit streamingStateChanged(true);
}

void CameraWorker::stopStreaming()
{
    ensureInWorkerThread();
    QMutexLocker locker(&m_mutex);

    if (!m_isStreaming) return;

    m_isStreaming = false;

    // Отключаем обработчик кадров
    if (m_videoSink) {
        disconnect(m_videoSink, &QVideoSink::videoFrameChanged, this, &CameraWorker::handleFrame);
    }

    // Останавливаем камеру
    if (m_camera && m_camera->isActive()) {
        m_camera->stop();
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
    }

    // Останавливаем GStreamer pipeline
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_element_get_state(m_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    }

    cleanupPipeline();
    cleanupCamera();

    emit streamingStateChanged(false);
}

bool CameraWorker::setupCamera()
{
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (const QCameraDevice& camera : cameras) {
        if (camera.id() == m_deviceId) {
            m_camera = new QCamera(camera);
            m_captureSession.setCamera(m_camera);
            m_captureSession.setVideoSink(m_videoSink);
            connect(m_videoSink, &QVideoSink::videoFrameChanged,
                    this, &CameraWorker::handleFrame, Qt::DirectConnection);
            return true;
        }
    }

    emit errorOccurred("Camera not found");
    return false;
}

bool CameraWorker::setupPipeline()
{
    QString pipelineStr = QString(
                              "appsrc name=source is-live=true format=time do-timestamp=true "
                              "caps=video/x-raw,format=RGBA,width=1280,height=720,framerate=30/1 ! "
                              "videoconvert ! "
                              "video/x-raw,format=NV12 ! "  // Изменено на NV12 для лучшей совместимости с RGA
                              "mpph264enc gop=10 bps=3000000 ! "
                              "h264parse config-interval=-1 ! "
                              "rtph264pay pt=96 mtu=1400 ! "
                              "queue max-size-buffers=0 max-size-bytes=0 max-size-time=2000000000 leaky=downstream ! "
                              "udpsink host=%1 port=%2 sync=false async=false"
                              ).arg(m_host).arg(m_port);

    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

    if (error) {
        emit errorOccurred(QString("Pipeline error: %1").arg(error->message));
        g_error_free(error);
        return false;
    }

    m_appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "source");
    if (!m_appsrc) {
        emit errorOccurred("Failed to get appsrc element");
        return false;
    }

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGBA",
                                        "width", G_TYPE_INT, 1280,
                                        "height", G_TYPE_INT, 720,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        nullptr);
    gst_app_src_set_caps(GST_APP_SRC(m_appsrc), caps);
    gst_caps_unref(caps);

    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(onNeedData), this);
    g_signal_connect(m_appsrc, "enough-data", G_CALLBACK(onEnoughData), this);

    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)onBusMessage, this);
    gst_object_unref(bus);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_camera->start();

    return true;
}

void CameraWorker::cleanupPipeline()
{
    if (m_appsrc) {
        g_signal_handlers_disconnect_by_data(m_appsrc, this);
        gst_object_unref(m_appsrc);
        m_appsrc = nullptr;
    }

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

void CameraWorker::cleanupCamera()
{
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
}

void CameraWorker::handleFrame(const QVideoFrame& frame)
{
    QMutexLocker locker(&m_mutex);

    if (!m_isStreaming || !m_appsrc || !frame.isValid()) return;

    QImage image = frame.toImage().convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return;

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, image.sizeInBytes(), nullptr);
    GstMapInfo map;

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        memcpy(map.data, image.constBits(), image.sizeInBytes());
        gst_buffer_unmap(buffer, &map);

        GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(m_frameCount, GST_SECOND, 30);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, 30);
        m_frameCount++;

        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
        if (ret != GST_FLOW_OK) {
            gst_buffer_unref(buffer);
            qWarning() << "Failed to push buffer to appsrc:" << ret;
        }
    } else {
        gst_buffer_unref(buffer);
    }
}

void CameraWorker::captureFrame(const QString& savePath) {
    QMutexLocker locker(&m_mutex);
    if (!m_isStreaming || !m_camera || !m_camera->isActive()) {
        emit errorOccurred("Camera is not active");
        return;
    }

    // Сохраняем текущий кадр - используем -> вместо . для указателя
    QVideoFrame currentFrame = m_videoSink->videoFrame();
    if (!currentFrame.isValid()) {
        emit errorOccurred("No valid frame available");
        return;
    }

    QImage image = currentFrame.toImage();
    if (image.isNull()) {
        emit errorOccurred("Failed to convert frame to image");
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz");
    QString fileName = QString("%1/capture_%2.jpg").arg(savePath).arg(timestamp);

    if (!image.save(fileName, "JPEG", 90)) {
        emit errorOccurred(QString("Failed to save image to %1").arg(fileName));
        return;
    }

    emit errorOccurred(QString("Image captured: %1").arg(fileName));
}

void CameraWorker::ensureInWorkerThread() {
    if (QThread::currentThread() != this->thread()) {
        qCritical() << "Method called from wrong thread!";
        Q_ASSERT(false);
    }
}

void CameraWorker::onBusMessage(GstBus* bus, GstMessage* msg, gpointer data) {
    Q_UNUSED(bus);
    CameraWorker* self = static_cast<CameraWorker*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err;
        gchar* debug;
        gst_message_parse_error(msg, &err, &debug);
        emit self->errorOccurred(QString("GStreamer error: %1").arg(err->message));
        g_error_free(err);
        g_free(debug);
        emit self->streamingStateChanged(false);
        break;
    }
    case GST_MESSAGE_EOS:
        emit self->streamingStateChanged(false);
        break;
    default:
        break;
    }
}

void CameraWorker::onNeedData(GstElement* appsrc, guint size, gpointer data) {
    Q_UNUSED(appsrc);
    Q_UNUSED(size);
    CameraWorker* self = static_cast<CameraWorker*>(data);
    // Обработка запроса данных - данные будут отправлены в handleFrame
}

void CameraWorker::onEnoughData(GstElement* appsrc, gpointer data) {
    Q_UNUSED(appsrc);
    CameraWorker* self = static_cast<CameraWorker*>(data);
    // Обработка сигнала о достаточности данных
}
