import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// About Shelf, and updates: whether Shelf may check GitHub for a new
// version once a day, checking by hand, the release notes, and updating.
Popup {
    id: about
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, (parent ? parent.width : 660) - 40)
    height: Math.min(560, (parent ? parent.height : 600) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    readonly property string state: updater.state

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Image {
                source: "image://icon/library"
                sourceSize: Qt.size(28, 28)
            }
            ColumnLayout {
                spacing: 0
                Label { text: "Shelf"; font.pixelSize: 22; font.weight: Font.DemiBold }
                Label { text: qsTr("Version %1").arg(updater.version); opacity: 0.6 }
            }
            Item { Layout.fillWidth: true }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: about.close()
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.75
            text: qsTr("A library and reader for EPUB and PDF books, with notes, practice, and local AI that never leaves your computer.")
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2c2c33" }

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Check for updates automatically") }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    opacity: 0.6
                    text: qsTr("Once a day, Shelf asks GitHub whether there is a newer version. Nothing about you or your books is sent.")
                }
            }
            Switch {
                checked: updater.autoCheck
                onToggled: updater.autoCheck = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Button {
                text: qsTr("Check now")
                enabled: about.state !== "checking" && about.state !== "downloading" && about.state !== "installing"
                onClicked: updater.check(true)
            }
            BusyIndicator {
                visible: about.state === "checking" || about.state === "installing"
                running: visible
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: about.state === "failed" ? "#ff8a80" : about.state === "available" ? theme.accent : "#c9c7cd"
                text: about.state === "checking" ? qsTr("Checking…")
                    : about.state === "upToDate" ? qsTr("Shelf is up to date.")
                    : about.state === "available" ? qsTr("Shelf %1 is available.").arg(updater.latest)
                    : about.state === "downloading" ? qsTr("Downloading… %1%").arg(Math.round(updater.progress * 100))
                    : about.state === "installing" ? qsTr("Installing… your password may be asked for.")
                    : about.state === "installed" ? qsTr("Shelf %1 is installed. Restart to use it.").arg(updater.latest)
                    : about.state === "failed" ? updater.error : ""
            }
        }
        ProgressBar {
            visible: about.state === "downloading"
            Layout.fillWidth: true
            value: updater.progress
        }

        // What's new.
        Label {
            visible: notesView.visible
            text: qsTr("What's new in %1").arg(updater.latest)
            font.weight: Font.DemiBold
        }
        ScrollView {
            id: notesView
            visible: updater.notes !== "" && ["available", "downloading", "installing", "installed"].includes(about.state)
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            Label {
                width: notesView.availableWidth
                text: updater.notes
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
        Item { Layout.fillHeight: true; visible: !notesView.visible }

        RowLayout {
            Layout.fillWidth: true
            visible: about.state === "available" || about.state === "installed"
            Button {
                flat: true
                visible: about.state === "available"
                text: qsTr("Skip this version")
                onClicked: updater.skip()
            }
            Button {
                flat: true
                visible: updater.pageUrl !== ""
                text: qsTr("Release page")
                onClicked: Qt.openUrlExternally(updater.pageUrl)
            }
            Item { Layout.fillWidth: true }
            AccentButton {
                visible: about.state === "available"
                text: updater.canInstall ? qsTr("Update") : qsTr("Download")
                onClicked: updater.install()
            }
            AccentButton {
                visible: about.state === "installed"
                text: qsTr("Restart Shelf")
                onClicked: updater.restart()
            }
        }
    }
}
