// gst_server.h
#pragma once

#include <QObject>
#include <gst/gst.h>

class GstStreamer : public QObject
{
    Q_OBJECT
public:
    explicit GstStreamer(QObject *parent = nullptr);
    ~GstStreamer();

    void startStreaming(const QString &host, int port);
    void stopStreaming();

private:
    GstElement *m_pipeline = nullptr;
    static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer data);
};
