import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// All of Shelf's settings in one window: a list of sections on the left,
// the chosen one on the right. openAt("ai" | "backup" | "about" | "help").
Popup {
    id: settings
    anchors.centerIn: Overlay.overlay
    width: Math.min(780, (parent ? parent.width : 820) - 40)
    height: Math.min(640, (parent ? parent.height : 680) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    readonly property var sections: [
        { key: "ai", label: qsTr("Local AI and voices"), glyph: "✦" },
        { key: "backup", label: qsTr("Backup and export"), glyph: "⇪" },
        { key: "about", label: qsTr("Updates and about"), glyph: "ⓘ" },
        { key: "help", label: qsTr("Help"), glyph: "?" }
    ]
    property string current: "ai"

    function openAt(key) {
        current = key || current
        open()
    }
    onOpened: if (current === "ai") aiPage.refresh()
    onCurrentChanged: if (current === "ai" && visible) aiPage.refresh()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The sections.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 210
            color: "#141418"
            radius: theme.radius
            Rectangle {   // square off the right edge
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.radius
                color: parent.color
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 2
                Label {
                    text: qsTr("Settings")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    Layout.leftMargin: 8
                    Layout.topMargin: 6
                    Layout.bottomMargin: 10
                }
                Repeater {
                    model: settings.sections
                    delegate: SidebarItem {
                        required property var modelData
                        Layout.fillWidth: true
                        icon: modelData.glyph
                        text: modelData.label
                        selected: settings.current === modelData.key
                        onClicked: settings.current = modelData.key
                        // An update waiting, or the AI not set up yet.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 7; height: 7; radius: 3.5
                            visible: color !== Qt.color("transparent")
                            color: modelData.key === "about" && (updater.state === "available" || updater.state === "installed") ? theme.accent
                                 : modelData.key === "ai" && ollama.status !== "ready" && ollama.status !== "checking" ? "#f5b347"
                                 : "transparent"
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // The chosen section.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ToolbarButton {
                z: 2
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: settings.close()
            }
            StackLayout {
                anchors.fill: parent
                anchors.margins: 22
                anchors.rightMargin: 26
                currentIndex: Math.max(0, settings.sections.findIndex(s => s.key === settings.current))
                AiSettingsPage { id: aiPage; onCloseRequested: settings.close() }
                BackupPage {}
                AboutPage {}
                HelpPage { onCloseRequested: settings.close() }
            }
        }
    }
}
