import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Offers to build an outline with local AI for a PDF that has none, shows
// how far along it is, and says plainly what is missing when it cannot.
Rectangle {
    id: card
    property var book: ({})
    property bool hasEntries: false

    readonly property var job: { outlines.revision; return outlines.status(book.id || "") }
    readonly property bool running: ["reading", "scanning", "thinking"].indexOf(job.state) >= 0
    readonly property bool failed: job.state === "failed"
    readonly property string phase: job.state === "reading" ? qsTr("Reading pages")
                                  : job.state === "scanning" ? qsTr("Reading pictures of pages")
                                  : qsTr("Finding chapters")

    function build() {
        outlines.generate(book.id, book.path, book.title || "")
    }

    implicitHeight: content.implicitHeight + 28
    radius: Math.min(8, theme.radius)
    color: "#18181d"
    border.color: "#26262d"

    // Ollama may have been started since Shelf opened; ask again whenever
    // the offer is shown.
    onVisibleChanged: if (visible && !running) ollama.refresh()
    Component.onCompleted: if (visible) ollama.refresh()

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: card.running ? qsTr("Building an outline…")
                : card.failed ? qsTr("The outline could not be finished")
                : qsTr("This PDF has no outline")
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        // While it works: what it is doing, and how far it has got.
        ColumnLayout {
            visible: card.running
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: card.job.total > 0
                      ? qsTr("%1 (%2 of %3)").arg(card.phase)
                            .arg(card.job.done.toLocaleString(Qt.locale(), 'f', 0))
                            .arg(card.job.total.toLocaleString(Qt.locale(), 'f', 0))
                      : card.phase + "…"
                font.pixelSize: 12
                opacity: 0.7
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: card.job.total <= 0
                value: card.job.total > 0 ? card.job.done / card.job.total : 0
            }
            Label {
                Layout.fillWidth: true
                visible: card.job.state === "thinking"
                text: qsTr("Chapters appear below as they are found. Keep reading; this carries on in the background.")
                font.pixelSize: 12
                opacity: 0.55
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("Stop")
                flat: true
                onClicked: outlines.cancel(card.book.id)
            }
        }

        Label {
            visible: card.failed
            Layout.fillWidth: true
            text: card.job.message || ""
            font.pixelSize: 12
            color: "#ff8a80"
            wrapMode: Text.WordWrap
        }

        // Before it starts (or after it failed): what it needs.
        ColumnLayout {
            visible: !card.running
            Layout.fillWidth: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                font.pixelSize: 12
                opacity: 0.7
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                linkColor: theme.accent
                onLinkActivated: link => Qt.openUrlExternally(link)
                text: ollama.status === "checking"
                      ? qsTr("Looking for Ollama, which runs AI on this computer…")
                      : ollama.status === "notInstalled"
                      ? qsTr("Shelf can build one with AI that runs on this computer, using Ollama. Install it from <a href=\"https://ollama.com/download\">ollama.com</a>, then check again.")
                      : ollama.status === "notRunning"
                      ? qsTr("Ollama is installed but not running. Start it, then check again.")
                      : ollama.status === "noModels"
                        ? qsTr("Ollama is running but has no model yet. Install one in Settings.")
                        : qsTr("Shelf can build one with AI that runs on this computer, so nothing leaves it. It reads every page, which takes a few minutes, and chapters fill in as they are found.")
            }

            ComboBox {
                visible: ollama.status === "ready" && ollama.models.length > 1
                Layout.fillWidth: true
                model: ollama.models
                currentIndex: Math.max(0, ollama.models.indexOf(ollama.model))
                onActivated: index => ollama.model = ollama.models[index]
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                BusyIndicator {
                    visible: ollama.status === "checking" || ollama.busy
                    running: visible
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                AccentButton {
                    visible: ollama.status === "ready"
                    text: card.failed || card.hasEntries ? qsTr("Try again") : qsTr("Build outline")
                    onClicked: card.build()
                }
                Button {
                    visible: ["notInstalled", "notRunning", "noModels"].indexOf(ollama.status) >= 0
                    text: qsTr("Check again")
                    flat: true
                    onClicked: ollama.refresh()
                }
                // So pressing Check again visibly did something, even when
                // the answer is the same as before.
                Label {
                    visible: (["notInstalled", "notRunning", "noModels"].indexOf(ollama.status) >= 0) && !ollama.busy
                    text: qsTr("Checked at %1").arg(Qt.formatTime(ollama.checked, Locale.ShortFormat))
                    font.pixelSize: 11
                    opacity: 0.45
                }
                Item { Layout.fillWidth: true }
                ToolbarButton {
                    label: qsTr("Settings")
                    tip: qsTr("Local AI settings")
                    onClicked: win.openAiSettings()
                }
                Label {
                    visible: false
                    text: ollama.model
                    font.pixelSize: 11
                    opacity: 0.45
                    elide: Text.ElideLeft
                    Layout.maximumWidth: 120
                }
            }
        }
    }
}
