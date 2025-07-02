// gst_server.cpp
#include "gst_server.h"

GstStreamer::GstStreamer(QObject *parent) : QObject(parent)
{
    gst_init(nullptr, nullptr);
}

GstStreamer::~GstStreamer()
{
    stopStreaming();
}

void GstStreamer::startStreaming(const QString &host, int port, int deviceIndex)
{
    if (m_pipeline) {
        qWarning() << "Streaming already started";
        return;
    }

    // Проверяем доступность устройства
    QString devicePath = QString("/dev/video%1").arg(deviceIndex);
    if (!QFileInfo::exists(devicePath)) {
        qCritical() << "Video device" << devicePath << "not found";
        emit errorOccurred(QString("Device %1 not available").arg(devicePath));
        return;
    }

    // Формируем pipeline с выбранным устройством
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
                              ).arg(devicePath).arg(host).arg(port);

    qDebug() << "Starting pipeline:" << pipelineStr;

    GError *error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

    if (error) {
        qCritical() << "Failed to create pipeline:" << error->message;
        g_error_free(error);
        return;
    }

    // Установка callback для сообщений от GStreamer
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)onBusMessage, this);
    gst_object_unref(bus);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    qDebug() << "Streaming started to" << host << ":" << port;
}

void GstStreamer::stopStreaming()
{
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        qDebug() << "Streaming stopped";
    }
}

void GstStreamer::onBusMessage(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus);
    GstStreamer *self = static_cast<GstStreamer*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err;
        gchar *debug;
        gst_message_parse_error(msg, &err, &debug);
        qCritical() << "GStreamer error:" << err->message;
        if (debug) qCritical() << "Debug info:" << debug;
        g_error_free(err);
        g_free(debug);
        self->stopStreaming();
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        self->stopStreaming();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
        qDebug() << "State changed from" << gst_element_state_get_name(old_state)
                 << "to" << gst_element_state_get_name(new_state);
        break;
    }
    default:
        break;
    }
}
