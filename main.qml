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
    minimumHeight: 550
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

                    delegate: RowLayout {
                        width: cameraList.width
                        spacing: 10

                        Label {
                            text: modelData
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Button {
                            text: streamer.activeStreams.indexOf(streamer.availableDevices[index]) >= 0 ?
                                  "Stop" : "Start"
                            onClicked: {
                                if (text === "Start") {
                                    streamer.startStreaming(index)
                                } else {
                                    streamer.stopStreaming(index)
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
                    model: streamer.activeStreams
                    height: 100
                    width: parent.width
                    clip: true

                    delegate: Label {
                        text: modelData
                        width: parent.width
                        elide: Text.ElideRight
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
