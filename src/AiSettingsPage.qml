import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings › Local AI: whether Ollama is there and running, which model, where
// it runs, and a test that proves it answers.
Item {
    id: settings
    // Done, or a sample lesson started: the settings window closes.
    signal closeRequested()
    function refresh() { ollama.refresh() }

    readonly property bool linux: Qt.platform.os === "linux"

    // A voice already downloaded is switched to at once; one that isn't
    // waits for its Download button.
    function pickVoice() {
        const id = speakerPicker.voiceId(speakerPicker.currentIndex)
        if (id && voice.installed(id))
            voice.install(id)
    }
    function voiceName() {
        for (const m of voice.models)
            for (const sp of m.speakers)
                if (m.id + "/" + sp.voice === voice.current)
                    return m.label + ": " + sp.label
        return ""
    }
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

        // Getting Ollama: the steps, with Shelf noticing each one done.
        Rectangle {
            visible: ollama.status === "notInstalled" || ollama.status === "notRunning"
            Layout.fillWidth: true
            implicitHeight: setupColumn.implicitHeight + 24
            radius: Math.min(8, theme.radius)
            color: "#1f1f25"
            border.color: "#2c2c33"
            ColumnLayout {
                id: setupColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                Label {
                    text: qsTr("Set up the local AI")
                    font.weight: Font.DemiBold
                }
                SetupStep {
                    number: 1
                    done: ollama.status !== "notInstalled"
                    text: settings.linux
                          ? qsTr("Install Ollama. On Arch or Omarchy: <tt>sudo pacman -S ollama-cuda</tt> (NVIDIA) or <tt>ollama</tt>; elsewhere see <a href=\"https://ollama.com/download/linux\">ollama.com</a>.")
                          : qsTr("Download Ollama and run its installer. It's free and takes a minute.")
                    AccentButton {
                        visible: !settings.linux && ollama.status === "notInstalled"
                        text: qsTr("Download Ollama")
                        onClicked: Qt.openUrlExternally(Qt.platform.os === "osx" ? "https://ollama.com/download/mac"
                                                                                  : "https://ollama.com/download/windows")
                    }
                }
                SetupStep {
                    number: 2
                    done: false
                    text: settings.linux
                          ? qsTr("Start it: <tt>sudo systemctl enable --now ollama</tt>, or the button here for now.")
                          : qsTr("Start Ollama. The installer usually starts it; if not, use the button.")
                    Button {
                        visible: ollama.status === "notRunning"
                        text: qsTr("Start Ollama")
                        onClicked: ollama.start()
                    }
                }
                SetupStep {
                    number: 3
                    done: false
                    text: qsTr("Pick a model here. Shelf suggests one that fits your computer.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    opacity: 0.5
                    text: qsTr("Shelf checks every few seconds, so each step ticks itself off.")
                }
            }
            // Noticing the install or start without a click.
            Timer {
                running: parent.visible && settings.visible
                interval: 4000
                repeat: true
                onTriggered: ollama.refresh()
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

        // Animated lessons: their voice and their motion.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            SectionLabel { text: qsTr("Lessons") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.7
                text: qsTr("Explanations can be read aloud by a voice that runs on this computer. Pick a voice model, then one of its voices; each model downloads once. Kokoro HD sounds the most like a person; Lessac is lighter, for older computers.")
            }
            // A voice model, then one of its speakers.
            GridLayout {
                Layout.fillWidth: true
                visible: voice.status !== "unsupported"
                columns: 2
                columnSpacing: 8
                rowSpacing: 6
                ComboBox {
                    id: modelPicker
                    Layout.fillWidth: true
                    model: voice.models
                    textRole: "label"
                    enabled: voice.status !== "downloading"
                    readonly property var chosen: voice.models[currentIndex] || ({ speakers: [] })
                    Component.onCompleted: currentIndex = Math.max(0, voice.models.findIndex(m => m.id === voice.currentModel))
                    // A change to the list keeps the model in view and picks
                    // the voice in use.
                    Connections {
                        target: voice
                        function onModelsChanged() {
                            Qt.callLater(() => {
                                modelPicker.currentIndex = Math.max(0, voice.models.findIndex(m => m.id === voice.currentModel))
                                speakerPicker.currentIndex = speakerPicker.indexOfCurrent()
                            })
                        }
                    }
                    onActivated: {
                        // The voice in use if it's this model's, else its first.
                        const mine = voice.currentModel === chosen.id
                        speakerPicker.currentIndex = mine ? speakerPicker.indexOfCurrent() : 0
                        settings.pickVoice()
                    }
                }
                Label {
                    text: !modelPicker.chosen.size ? "" : modelPicker.chosen.size >= 1000
                          ? qsTr("%1 GB").arg(Math.round(modelPicker.chosen.size / 1000)) : qsTr("%1 MB").arg(modelPicker.chosen.size)
                    opacity: 0.55
                    font.pixelSize: 12
                }
                ComboBox {
                    id: speakerPicker
                    Layout.fillWidth: true
                    model: modelPicker.chosen.speakers
                    textRole: "label"
                    enabled: voice.status !== "downloading"
                    function voiceId(index) {
                        const sp = modelPicker.chosen.speakers[index]
                        if (!sp)
                            return ""
                        return modelPicker.chosen.id + "/" + sp.voice
                    }
                    function indexOfCurrent() {
                        for (let i = 0; i < modelPicker.chosen.speakers.length; ++i)
                            if (voiceId(i) === voice.current)
                                return i
                        return 0
                    }
                    Component.onCompleted: currentIndex = indexOfCurrent()
                    onActivated: settings.pickVoice()
                }
                Button {
                    text: qsTr("Try")
                    enabled: voice.ready && voice.current === speakerPicker.voiceId(speakerPicker.currentIndex)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Hear this voice")
                    onClicked: win.previewVoice(speakerPicker.currentText)
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.6
                text: modelPicker.chosen.about || ""
            }
            RowLayout {
                Layout.fillWidth: true
                visible: voice.status !== "unsupported"
                AccentButton {
                    visible: voice.status !== "downloading" && !voice.installed(modelPicker.chosen.id || "")
                    text: qsTr("Download %1 (%2 MB)").arg(modelPicker.chosen.label || "").arg(modelPicker.chosen.size || 0)
                    onClicked: voice.install(speakerPicker.voiceId(speakerPicker.currentIndex))
                }
                Button {
                    visible: voice.status === "downloading"
                    flat: true
                    text: qsTr("Stop")
                    onClicked: voice.cancelInstall()
                }
                Label {
                    visible: voice.ready
                    text: qsTr("In use: %1").arg(settings.voiceName())
                    font.pixelSize: 12
                    color: "#6fd08c"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            ProgressBar {
                visible: voice.status === "downloading"
                Layout.fillWidth: true
                value: Math.max(0, voice.progress)
                indeterminate: voice.progress < 0
            }
            Label {
                visible: voice.status === "failed" || voice.status === "unsupported"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#ff8a80"
                text: voice.status === "unsupported" ? qsTr("The voice isn't available for this kind of computer; lessons show captions instead.")
                                                     : voice.error
            }
            Switch {
                visible: voice.ready
                text: qsTr("Read lessons aloud")
                checked: library.setting("lessonVoice", true) === true
                onToggled: library.setSetting("lessonVoice", checked)
            }
            Switch {
                text: qsTr("Animate lessons")
                checked: library.setting("animations", "full") !== "reduced"
                onToggled: library.setSetting("animations", checked ? "full" : "reduced")
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    flat: true
                    text: qsTr("Play a sample lesson")
                    icon.source: "image://icon/sparkle?color=" + encodeURIComponent(theme.accent)
                    onClicked: { settings.closeRequested(); win.playSample() }
                }
                Item { Layout.fillWidth: true }
                Button {
                    visible: voice.ready
                    flat: true
                    text: qsTr("Remove voices")
                    onClicked: voice.remove()
                }
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
                onClicked: settings.closeRequested()
            }
        }
    }

    }

    // A numbered step of the Ollama setup, with room for a button.
    component SetupStep: RowLayout {
        id: step
        property int number
        property bool done
        property string text
        default property alias extra: buttons.data
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 22; height: 22; radius: 11
            color: step.done ? "#2f5d3c" : "#2c2c33"
            Label {
                anchors.centerIn: parent
                text: step.done ? "✓" : step.number
                font.pixelSize: 12
                color: step.done ? "#6fd08c" : "#c8c8ce"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                linkColor: theme.accent
                onLinkActivated: link => Qt.openUrlExternally(link)
                font.pixelSize: 13
                opacity: step.done ? 0.55 : 0.9
                text: step.text
            }
            RowLayout { id: buttons }
        }
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        opacity: 0.55
    }
}
