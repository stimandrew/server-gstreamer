#include "cameraworker.h"

CameraWorker::CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent)
    : QObject(parent), m_deviceId(deviceId), m_host(host), m_port(port)
{
    gst_init(nullptr, nullptr);
}

CameraWorker::~CameraWorker()
{
    QMutexLocker locker(&m_mutex);
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

void CameraWorker::startStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (m_pipeline) {
        emit errorOccurred("Streaming already started");
        return;
    }

    QString pipelineStr = QString(
                              "v4l2src device=%1 ! "
                              "image/jpeg,width=1280,height=720,framerate=30/1 ! "
                              "mppjpegdec ! "
                              "videoconvert ! "
                              "video/x-raw,format=RGBA ! "
                              "mpph264enc gop=10 bps=3000000 ! "
                              "h264parse config-interval=-1 ! "
                              "rtph264pay pt=96 mtu=1400 ! "
                              "queue max-size-buffers=0 max-size-bytes=0 max-size-time=2000000000 ! "
                              "udpsink host=%2 port=%3 sync=false async=false"
                              ).arg(m_deviceId).arg(m_host).arg(m_port);

    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

    if (error) {
        emit errorOccurred(QString("Pipeline error: %1").arg(error->message));
        g_error_free(error);
        return;
    }

    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)onBusMessage, this);
    gst_object_unref(bus);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    emit streamingStateChanged(true);
}

void CameraWorker::stopStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        emit streamingStateChanged(false);
    }
}

void CameraWorker::onBusMessage(GstBus* bus, GstMessage* msg, gpointer data)
{
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
