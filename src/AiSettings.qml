import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Local AI settings: whether Ollama is there and running, which model, where
// it runs, and a test that proves it answers.
Popup {
    id: settings
    anchors.centerIn: Overlay.overlay
    width: Math.min(500, (parent ? parent.width : 520) - 40)
    height: Math.min(scroller.contentHeight + topPadding + bottomPadding, (parent ? parent.height : 800) - 40)
    modal: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#1b1b20"; radius: theme.radius; border.color: "#2c2c33" }

    onOpened: ollama.refresh()

    readonly property bool linux: Qt.platform.os === "linux"
    readonly property var result: ollama.testResult

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

    ColumnLayout {
        width: scroller.availableWidth
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Local AI")
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            BusyIndicator {
                visible: ollama.busy
                running: visible
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: -8
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            opacity: 0.6
            text: qsTr("Shelf's AI features run on this computer through Ollama. Nothing in your books leaves it.")
        }

        // Where things stand, and the one thing to do next.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 5
                width: 10
                height: 10
                radius: 5
                color: ollama.status === "ready" ? "#6fd08c" : ollama.status === "checking" ? "#8a8a92" : "#f5b347"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                linkColor: theme.accent
                onLinkActivated: link => Qt.openUrlExternally(link)
                text: {
                    switch (ollama.status) {
                    case "notInstalled":
                        return qsTr("Ollama is not installed. Get it from <a href=\"https://ollama.com/download\">ollama.com</a>.")
                    case "notRunning":
                        return settings.linux
                            ? qsTr("Ollama is installed but not running. Start it with <tt>sudo systemctl enable --now ollama</tt>.")
                            : qsTr("Ollama is installed but not running. Open Ollama from the Start menu.")
                    case "noModels":
                        return qsTr("Ollama is running but has no model yet. Install one below.")
                    case "ready":
                        return qsTr("Ollama is running with %n model(s).", "", ollama.models.length)
                    default:
                        return qsTr("Looking for Ollama…")
                    }
                }
            }
        }

        ColumnLayout {
            visible: ollama.status === "ready"
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                SectionLabel { text: qsTr("Model") }
                ComboBox {
                    Layout.fillWidth: true
                    model: ollama.models
                    currentIndex: Math.max(0, ollama.models.indexOf(ollama.model))
                    onActivated: index => ollama.model = ollama.models[index]
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                SectionLabel { text: qsTr("Run on") }
                Segmented {
                    Layout.fillWidth: true
                    options: [
                        { key: "gpu", label: qsTr("Graphics card") },
                        { key: "cpu", label: qsTr("Processor only") }
                    ]
                    value: ollama.useGpu ? "gpu" : "cpu"
                    onPicked: key => ollama.useGpu = key === "gpu"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    opacity: 0.6
                    text: ollama.useGpu
                          ? qsTr("Fastest. Ollama uses the graphics card when it has one, and the processor for whatever does not fit.")
                          : qsTr("Several times slower, but leaves the graphics card free for games or other apps.")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Button {
                    text: ollama.testing ? qsTr("Testing…") : qsTr("Test model")
                    enabled: !ollama.testing
                    onClicked: ollama.test()
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    visible: settings.result.ok !== undefined
                    color: settings.result.ok ? "#6fd08c" : "#ff8a80"
                    text: {
                        const r = settings.result
                        if (!r.ok)
                            return r.error || ""
                        const secs = (r.seconds || 0).toLocaleString(Qt.locale(), 'f', 1)
                        return r.processor === "gpu" ? qsTr("Working: answered in %1 s on the graphics card.").arg(secs)
                             : r.processor === "cpu" ? qsTr("Working: answered in %1 s on the processor.").arg(secs)
                             : r.processor === "mixed" ? qsTr("Working: answered in %1 s, partly on the processor because the model does not fit on the graphics card.").arg(secs)
                             : qsTr("Working: answered in %1 s.").arg(secs)
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: -8
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.45
                text: qsTr("The first answer after starting takes longer while the model loads.")
            }
        }

        // Recommended models, installed from here.
        ColumnLayout {
            visible: ollama.status === "ready" || ollama.status === "noModels"
            Layout.fillWidth: true
            spacing: 8
            SectionLabel { text: qsTr("Recommended models") }
            Repeater {
                model: ollama.recommended
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool installed: ollama.models.indexOf(modelData.name) >= 0
                    readonly property bool downloading: ollama.pulling === modelData.name
                    Layout.fillWidth: true
                    implicitHeight: rowContent.implicitHeight + 16
                    radius: Math.min(6, theme.radius)
                    color: "#15151a"
                    ColumnLayout {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: row.modelData.name + "  ·  " + row.modelData.size
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.about
                                    font.pixelSize: 11
                                    opacity: 0.6
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Label {
                                visible: row.installed && ollama.model === row.modelData.name
                                text: qsTr("In use")
                                font.pixelSize: 12
                                color: "#6fd08c"
                            }
                            Button {
                                visible: row.installed && ollama.model !== row.modelData.name
                                text: qsTr("Use")
                                flat: true
                                onClicked: ollama.model = row.modelData.name
                            }
                            Button {
                                visible: !row.installed && !row.downloading
                                enabled: ollama.pulling === ""
                                text: qsTr("Install")
                                onClicked: ollama.pull(row.modelData.name)
                            }
                            Button {
                                visible: row.downloading
                                text: qsTr("Stop")
                                flat: true
                                onClicked: ollama.cancelPull()
                            }
                        }
                        ProgressBar {
                            visible: row.downloading
                            Layout.fillWidth: true
                            indeterminate: ollama.pullProgress < 0
                            value: Math.max(0, ollama.pullProgress)
                        }
                        Label {
                            visible: row.downloading
                            text: ollama.pullStatus
                            font.pixelSize: 11
                            opacity: 0.6
                        }
                    }
                }
            }
            Label {
                visible: ollama.pullError !== ""
                Layout.fillWidth: true
                text: ollama.pullError
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#ff8a80"
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Models download once and stay on this computer. You can keep reading while one downloads.")
                font.pixelSize: 11
                opacity: 0.45
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                visible: ollama.status !== "ready"
                text: qsTr("Check again")
                flat: true
                onClicked: ollama.refresh()
            }
            Item { Layout.fillWidth: true }
            AccentButton {
                text: qsTr("Done")
                onClicked: settings.close()
            }
        }
    }

    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        opacity: 0.55
    }
}
