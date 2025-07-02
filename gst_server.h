// gst_server.h
#pragma once

#include <QObject>
#include <QDebug>
#include <QFileInfo>
#include <gst/gst.h>

class GstStreamer : public QObject
{
    Q_OBJECT
public:
    explicit GstStreamer(QObject *parent = nullptr);
    ~GstStreamer();

    void startStreaming(const QString &host, int port, int deviceIndex = 0);
    void stopStreaming();

signals: // Добавляем секцию signals
    void errorOccurred(const QString& message); // Объявляем сигнал

private:
    GstElement *m_pipeline = nullptr;
    static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer data);
};
