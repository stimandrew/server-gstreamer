// gst_server.cpp
#include "gst_server.h"

GstStreamer::GstStreamer(QObject *parent) : QObject(parent)
{
    gst_init(nullptr, nullptr);
    updateAvailableDevices();
}

GstStreamer::~GstStreamer()
{
    stopStreaming();
}

void GstStreamer::updateAvailableDevices()
{
    m_availableDevices.clear();
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    for (const QCameraDevice &camera : cameras) {
        m_availableDevices.append(camera.description());
    }

    if (m_availableDevices.isEmpty()) {
        m_availableDevices.append("No cameras found");
    }

    emit availableDevicesChanged();
}

void GstStreamer::refreshAvailableDevices()
{
    updateAvailableDevices();
}

void GstStreamer::startStreaming()
{
    if (m_pipeline) {
        qWarning() << "Streaming already started";
        return;
    }

    // Check if the selected device index is valid
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (m_deviceIndex < 0 || m_deviceIndex >= cameras.size()) {
        qCritical() << "Invalid camera index:" << m_deviceIndex;
        emit errorOccurred(QString("Invalid camera index: %1").arg(m_deviceIndex));
        return;
    }

    // Get the actual device name from QCamera
    QString deviceName = cameras.at(m_deviceIndex).id();

    // Формируем pipeline
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
                              ).arg(deviceName).arg(m_host).arg(m_port);

    qDebug() << "Starting pipeline:" << pipelineStr;

    GError *error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

    if (error) {
        qCritical() << "Failed to create pipeline:" << error->message;
        g_error_free(error);
        emit errorOccurred(QString("Pipeline error: %1").arg(error->message));
        return;
    }

    // Установка callback для сообщений от GStreamer
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)onBusMessage, this);
    gst_object_unref(bus);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    emit streamingStateChanged();
    qDebug() << "Streaming started to" << m_host << ":" << m_port;
}

void GstStreamer::stopStreaming()
{
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        emit streamingStateChanged();
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
        emit self->errorOccurred(QString("GStreamer error: %1").arg(err->message));
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


