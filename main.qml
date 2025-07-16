import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import gst_server 1.0

Window {
    id: window
    width: 400
    height: 300
    visible: true
    title: qsTr("GStreamer Video Server")

    GstStreamer {
            id: streamer
            onErrorOccurred: errorDialog.show(message)
        }

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            GroupBox {
                title: "Stream Settings"
                Layout.fillWidth: true

                Grid {
                    columns: 2
                    anchors.fill: parent
                    Label { text: "Host:" }
                    TextField {
                        id: hostField
                        text: streamer.host
                        onTextChanged: streamer.host = text
                        Layout.fillWidth: true
                    }

                    Label { text: "Port:" }
                    SpinBox {
                        id: portField
                        from: 1024
                        to: 65535
                        value: streamer.port
                        onValueChanged: streamer.port = value
                        Layout.fillWidth: true
                    }

                    Label { text: "Device Index:" }
                    SpinBox {
                        id: deviceField
                        from: 0
                        to: 10
                        value: streamer.deviceIndex
                        onValueChanged: streamer.deviceIndex = value
                        Layout.fillWidth: true
                    }
                }
            }

            Button {
                id: streamButton
                text: streamer.isStreaming ? "Stop Streaming" : "Start Streaming"
                onClicked: streamer.isStreaming ? streamer.stopStreaming() : streamer.startStreaming()
                Layout.fillWidth: true
            }

            Label {
                text: streamer.isStreaming ?
                      `Streaming to ${streamer.host}:${streamer.port} (device ${streamer.deviceIndex})` :
                      "Streaming stopped"
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }

        Dialog {
            id: errorDialog
            title: "Error"
            standardButtons: Dialog.Ok
            modal: true
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: Math.min(window.width * 0.8, 400)

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
