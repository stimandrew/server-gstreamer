// cameraworker.cpp
#include "cameraworker.h"

CameraWorker::CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent)
    : QObject(parent), m_deviceId(deviceId), m_host(host), m_port(port),
    m_lastFrameTime(std::chrono::steady_clock::now())
{
    m_thread = new QThread();
    moveToThread(m_thread);
    connect(m_thread, &QThread::started, this, &CameraWorker::init);
    m_thread->start();
}

CameraWorker::~CameraWorker()
{
    stopStreaming();

    // 2. Освободить ресурсы YOLO
    if (m_yoloInitialized) {
        release_yolo11_model(&m_rknnAppCtx);
    }

    // 3. Остановить и удалить таймер
    if (m_frameTimer) {
        m_frameTimer->stop();
        m_frameTimer->deleteLater();
    }
}

void CameraWorker::init()
{
    ensureInWorkerThread();

    // Инициализация объектов в правильном потоке
    m_videoSink = new QVideoSink(this);

    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(33); // ~30 FPS
    connect(m_frameTimer, &QTimer::timeout, this, &CameraWorker::requestFrame);
    m_frameTimer->start();
}

void CameraWorker::startStreaming()
{
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
    qDebug() << "void CameraWorker::stopStreaming()";
    ensureInWorkerThread();
    QMutexLocker locker(&m_mutex);

    if (!m_isStreaming) return;

    m_isStreaming = false;

    QMutexLocker queueLocker(&queueMutex);
    frameQueue.clear();

    // Отключаем обработчик кадров и удаляем QVideoSink
    if (m_videoSink) {
        disconnect(m_frameTimer, &QTimer::timeout, this, &CameraWorker::requestFrame);
        delete m_videoSink;
        m_videoSink = nullptr;
    }

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

            // Получаем список поддерживаемых форматов
            const auto formats = camera.videoFormats();

            // Ищем формат с разрешением 1280x720
            QCameraFormat selectedFormat;
            bool formatFound = false;
            for (const auto& format : formats) {
                if (format.resolution() == QSize(1280, 720)) {
                    selectedFormat = format;
                    formatFound = true;
                    qDebug() << "selectedFormat.resolution().height() = " << selectedFormat.resolution().height();
                    qDebug() << "selectedFormat.resolution().width() = " << selectedFormat.resolution().width();
                    break;
                }
            }

            // Если нужный формат не найден, используем первый доступный
            if (!formatFound && !formats.isEmpty()) {
                selectedFormat = formats.first();
                qWarning() << "Desired format 1280x720 not found, using"
                           << selectedFormat.resolution() << "instead";
            }

            // Устанавливаем формат для камеры
            if (!selectedFormat.isNull()) {
                m_camera->setCameraFormat(selectedFormat);
            }

            m_captureSession.setCamera(m_camera);
            m_captureSession.setVideoSink(m_videoSink);
            return true;
        }
    }

    emit errorOccurred("Camera not found");
    return false;
}

void CameraWorker::requestFrame() {
    if (m_videoSink) {
        QVideoFrame frame = m_videoSink->videoFrame();
        if (frame.isValid()) {
            handleFrame(frame);
        }
    }
}

bool CameraWorker::setupPipeline()
{
    QString pipelineStr = QString(
                              "appsrc name=source is-live=true format=time do-timestamp=true "
                              "caps=video/x-raw,format=RGBA,width=1280,height=720,framerate=30/1 ! "
                              "videoconvert ! "
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
        m_camera->deleteLater();
        m_camera = nullptr;
    }
}

void CameraWorker::handleFrame(const QVideoFrame& frame)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isStreaming || !m_appsrc || !frame.isValid()) return;

    // Конвертируем в RGBA8888 для pipeline и RGB888 для YOLO
    QImage rgbaImage = frame.toImage().convertToFormat(QImage::Format_RGBA8888);
    if (rgbaImage.isNull()) return;

    // Если YOLO включен, обрабатываем кадр
    if (m_yoloEnabled && m_yoloInitialized) {
        QImage rgbImage = frame.toImage().convertToFormat(QImage::Format_RGB888);
        if (!rgbImage.isNull() && !rgbImage.size().isEmpty()) {
            if (frameQueue.size() < 3) {
                FrameData data;
                data.frame = rgbaImage; // Сохраняем RGBA для последующей отрисовки
                data.deviceId = m_deviceId;
                frameQueue.enqueue(data);
                // Немедленно обрабатываем кадр вместо ожидания таймера
                QMetaObject::invokeMethod(this, "processNextFrame", Qt::QueuedConnection);
            }
        }
    } else {
        // Если YOLO выключен, просто передаем кадр в pipeline
        pushFrameToPipeline(rgbaImage);
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

void CameraWorker::setYoloEnabled(bool enabled)
{
    QMutexLocker locker(&m_cameraMutex);
    m_yoloEnabled = enabled && m_yoloInitialized;
    if (!m_yoloEnabled) {
        queueMutex.lock();
        frameQueue.clear();
        queueMutex.unlock();
    }
}

void CameraWorker::setYoloModelPath(const QString &path)
{
    QMutexLocker locker(&m_cameraMutex);
    if (!path.isEmpty()) {
        init_post_process();
        int ret = init_yolo11_model(path.toStdString().c_str(), &m_rknnAppCtx);
        if (ret != 0) {
            qWarning() << "Failed to initialize YOLO model";
            m_yoloInitialized = false;
            m_yoloEnabled = false;
        } else {
            m_yoloInitialized = true;
        }
    }
}

void CameraWorker::ensureInWorkerThread() {
    qDebug() << "Current thread:" << QThread::currentThread();
    qDebug() << "Worker thread:" << this->thread();
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

void CameraWorker::processNextFrame() {
    QMutexLocker locker(&m_yoloMutex);
    if (!frameQueue.isEmpty()) {
        FrameData data = frameQueue.dequeue();
        frameQueue.clear();
        processFrameWithRGA(data.frame, data.deviceId); // Передаем кадр и deviceId
    }
}

void CameraWorker::processFrameWithRGA(const QImage &frame, const QString &sourceDeviceId) {
    qDebug() << "void CameraWorker::processFrameWithRGA(const QImage &frame, const QString &sourceDeviceId)";

    if (!m_yoloInitialized) {
        qWarning() << "YOLO model not initialized";
        return;
    }

    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(image_buffer_t));

    QImage converted = frame.convertToFormat(QImage::Format_RGB888);
    src_image.width = converted.width();
    src_image.height = converted.height();
    src_image.format = IMAGE_FORMAT_RGB888;
    src_image.size = converted.width() * converted.height() * 3;
    src_image.virt_addr = (unsigned char*)malloc(src_image.size);
    memcpy(src_image.virt_addr, converted.bits(), src_image.size);

    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));

    int ret = inference_yolo11_model(&m_rknnAppCtx, &src_image, &od_results);
    if (ret != 0) {
        qWarning() << "YOLO inference failed";
        free(src_image.virt_addr);
        return;
    }

    QList<QPair<QRect, QString>> objects;
    for (int i = 0; i < od_results.count; i++) {
        object_detect_result *det_result = &(od_results.results[i]);
        QRect rect(
            det_result->box.left,
            det_result->box.top,
            det_result->box.right - det_result->box.left,
            det_result->box.bottom - det_result->box.top
            );

        const char* cls_name = coco_cls_to_name(det_result->cls_id);
        // QString label = QString("%1 %2% (Camera: %3)")
        //                     .arg(QString::fromUtf8(cls_name))
        //                     .arg(QString::number(det_result->prop * 100, 'f', 0))
        //                     .arg(sourceDeviceId);
        QString label = QString("%1 %2%")
                            .arg(QString::fromUtf8(cls_name))
                            .arg(QString::number(det_result->prop * 100, 'f', 0));

        objects.append(qMakePair(rect, label));
    }

    // Рисуем результаты детекции на кадре
    QImage frameWithDetection = drawDetectionResults(frame, objects);

    // Отправляем кадр с детекцией в pipeline
    QMetaObject::invokeMethod(this, [this, frameWithDetection]() {
        QMutexLocker locker(&m_mutex);
        if (m_isStreaming && m_appsrc) {
            pushFrameToPipeline(frameWithDetection);
        }
    }, Qt::QueuedConnection);

    emit newObjects(objects);
    free(src_image.virt_addr);
    if(src_image.virt_addr){
        ensureInWorkerThread();
        qDebug() << "src_image.virt_addr = " << src_image.virt_addr;
    } else {
        ensureInWorkerThread();
        qDebug() << "!src_image.virt_addr = " << src_image.virt_addr;
    }
}

QImage CameraWorker::drawDetectionResults(const QImage& frame, const QList<QPair<QRect, QString>>& objects) {
    QImage imageWithDetection = frame.copy();
    QPainter painter(&imageWithDetection);

    QFont font = painter.font();
    font.setPointSize(20);
    painter.setFont(font);

    QPen pen(Qt::green, 3);
    painter.setPen(pen);

    for (const auto& obj : objects) {
        const QRect& rect = obj.first;
        const QString& label = obj.second;

        // Рисуем прямоугольник
        painter.drawRect(rect);

        // Рисуем текст с фоном
        painter.save();
        QRect textRect = QRect(rect.x(), rect.y() - 30, rect.width(), 30);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 150));
        painter.drawRect(textRect);

        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, label);
        painter.restore();
    }

    return imageWithDetection;
}

void CameraWorker::pushFrameToPipeline(const QImage& frame)
{

    qDebug() << "void CameraWorker::pushFrameToPipeline(const QImage& frame)";
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, frame.sizeInBytes(), nullptr);
    GstMapInfo map;

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        memcpy(map.data, frame.constBits(), frame.sizeInBytes());
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

