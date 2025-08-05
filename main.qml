import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import gst_server 1.0

Window {
    id: window
    width: 600
    minimumWidth: 550
    height: 550
    minimumHeight: 600
    visible: true
    title: qsTr("Multi-Camera Streamer")

    GstStreamer {
        id: streamer
        onErrorOccurred: errorDialog.show(message)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 10

        GroupBox {
            title: "Stream Settings"
            Layout.fillWidth: true
            width: parent.width

            GridLayout {
                columns: 2
                width: parent.width

                Label { text: "Host:"; Layout.minimumWidth: 100 }
                TextField {
                    id: hostField
                    text: streamer.host
                    onTextChanged: streamer.host = text
                    Layout.fillWidth: true
                }

                Label { text: "Base Port:"; Layout.minimumWidth: 100 }
                SpinBox {
                    id: portField
                    from: 1024
                    to: 65535
                    value: streamer.port
                    onValueChanged: streamer.port = value
                    Layout.fillWidth: true
                }
            }
        }

        GroupBox {
            title: "Cameras"
            Layout.fillWidth: true
            width: parent.width

            ColumnLayout {
                width: parent.width

                ListView {
                    id: cameraList
                    model: streamer.availableDevices
                    height: 150
                    width: parent.width
                    clip: true

                    delegate: ColumnLayout {
                        width: cameraList.width
                        spacing: 5

                        RowLayout {
                        width: cameraList.width
                        spacing: 10

                        Label {
                            text: modelData
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Button {
                            id: streamButton
                            text: streamer.isCameraActive(index) ? "Stop" : "Start"
                            onClicked: {
                                if (streamer.isCameraActive(index)) {
                                    streamer.stopStreaming(index)
                                } else {
                                    streamer.startStreaming(index)
                                }
                            }
                            Connections {
                                target: streamer
                                function onCameraStateChanged(deviceIndex, isActive) {
                                    if (deviceIndex === index) {
                                        streamButton.text = isActive ? "Stop" : "Start"
                                    }
                                }
                            }
                        }

                        Button {
                            text: "Capture"
                            onClicked: {
                                streamer.captureCameraImage(index, ".")
                            }
                        }
                    }

                    // YOLO controls for each camera
                    RowLayout {
                        id: yoloControls
                        Layout.fillWidth: true
                        spacing: 10
                        visible: streamer.isCameraActive(index)

                        Label {
                            text: "YOLO:"
                            Layout.minimumWidth: 50
                        }

                        Button {
                                text: "Load Model"
                                onClicked: streamer.setYoloModelPath("yolo11n.rknn")
                                Layout.fillWidth: true
                            }

                        Switch {
                            id: yoloEnabledSwitch
                            enabled: streamer.yoloModelPath !== "" && streamer.isCameraActive(index)
                            checked: streamer.yoloEnabled && streamer.isCameraActive(index)
                            onCheckedChanged: {
                                if (enabled) {
                                    streamer.setYoloEnabled(checked)
                                } else {
                                    checked = false
                                }
                            }

                            // Добавляем привязку к изменению состояния камеры
                            Connections {
                                target: streamer
                                function onCameraStateChanged(deviceIndex, isActive) {
                                    if (deviceIndex === index) {
                                        yoloEnabledSwitch.enabled = isActive && streamer.yoloModelPath !== ""
                                        if (!isActive) {
                                            yoloEnabledSwitch.checked = false
                                        }
                                    }
                                }
                            }
                            // Добавляем привязку к изменению пути модели YOLO
                            Connections {
                                target: streamer
                                function onYoloModelPathChanged() {
                                    yoloEnabledSwitch.enabled = streamer.isCameraActive(index) && streamer.yoloModelPath !== ""
                                }
                            }
                        }

                        // Добавляем привязку к изменению состояния камеры
                        Connections {
                            target: streamer
                            function onCameraStateChanged(deviceIndex, isActive) {
                                if (deviceIndex === index) {
                                    yoloControls.visible = isActive
                                }
                            }
                        }
                    }
                    }
                }

                Button {
                    text: "Refresh Cameras"
                    onClicked: streamer.refreshAvailableDevices()
                    Layout.fillWidth: true
                }
            }
        }

        GroupBox {
            title: "Active Streams"
            Layout.fillWidth: true
            width: parent.width

            ColumnLayout {
                width: parent.width
                spacing: 10

                ListView {
                    id: activeStreamsList
                    model: Object.keys(streamer.activeStreams).map(function(key) {
                        return streamer.activeStreams[key]
                    })
                    height: 150  // Увеличим высоту для дополнительной информации
                    width: parent.width
                    clip: true

                    delegate: Label {
                        text: qsTr("Camera: %1\nAddress: %2\nDevice ID: %3")
                              .arg(modelData.name)
                              .arg(modelData.address)
                              .arg(modelData.deviceId)
                        width: parent.width
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Stop All Streams"
                        onClicked: {
                            if (streamer) {
                                streamer.stopAllStreams()
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: errorDialog
        title: "Error"
        standardButtons: Dialog.Ok
        modal: true
        width: Math.min(window.width * 0.8, 400)
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2

        property alias text: errorLabel.text

        function show(message) {
            text = message;
            open();
        }

        Label {
            id: errorLabel
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
