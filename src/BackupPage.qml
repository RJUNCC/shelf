import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings › Backup and export: one file with everything Shelf knows about
// your reading, made by hand or every week; restoring one; and flashcards
// for Anki or Quizlet.
Item {
    id: page

    readonly property bool busy: backup.state === "working"

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 14

            Label { text: qsTr("Backup and export"); font.pixelSize: 18; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.65
                text: qsTr("A backup is one file with your library, reading positions, highlights, notes, flashcards, workspaces and settings. Keep it somewhere safe, like a OneDrive or Google Drive folder, or use it to move to a new computer.")
            }

            SectionLabel { text: qsTr("Where backups go") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: backup.folder
                    elide: Text.ElideMiddle
                    font.pixelSize: 13
                    ToolTip.visible: folderHover.hovered
                    ToolTip.text: backup.folder
                    HoverHandler { id: folderHover }
                }
                Button { flat: true; text: qsTr("Change…"); onClicked: backup.chooseFolder() }
                Button { flat: true; text: qsTr("Open"); onClicked: backup.openFolder() }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AccentButton {
                    text: qsTr("Back up now")
                    enabled: !page.busy
                    onClicked: backup.create(withBooks.checked)
                }
                CheckBox {
                    id: withBooks
                    text: qsTr("Include the books themselves")
                    checked: library.setting("backupBooks", false) === true
                    onToggled: library.setSetting("backupBooks", checked)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Makes the backup as big as your books, but a new computer gets them too.")
                }
            }
            Label {
                Layout.fillWidth: true
                font.pixelSize: 12
                opacity: 0.55
                text: backup.lastBackup === "" ? qsTr("No backup yet.")
                      : qsTr("Last backup: %1").arg(new Date(backup.lastBackup).toLocaleString(Qt.locale(), Locale.ShortFormat))
            }
            Switch {
                text: qsTr("Back up automatically every week")
                checked: library.setting("backupAuto", false) === true
                onToggled: {
                    library.setSetting("backupAuto", checked)
                    if (checked)
                        backup.createIfDue()
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: -10
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.5
                text: qsTr("Weekly backups leave out the books' own files and keep the newest five.")
            }

            // What happened, and what to do next.
            Rectangle {
                visible: backup.message !== "" && backup.state !== "idle"
                Layout.fillWidth: true
                implicitHeight: statusColumn.implicitHeight + 20
                radius: Math.min(8, theme.radius)
                color: backup.state === "failed" ? "#2a1c1c" : backup.state === "restart" ? "#1c2430" : "#1f1f25"
                border.color: backup.state === "failed" ? "#7a3a3a" : "#2c2c33"
                ColumnLayout {
                    id: statusColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        BusyIndicator {
                            visible: page.busy
                            running: visible
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: backup.state === "failed" ? "#ff8a80" : backup.state === "done" || backup.state === "restored" ? "#6fd08c" : "#e0e0e4"
                            text: backup.message
                        }
                    }
                    AccentButton {
                        visible: backup.state === "restart"
                        text: qsTr("Restart Shelf now")
                        onClicked: backup.restart()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2c2c33" }

            SectionLabel { text: qsTr("Restore") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.65
                text: qsTr("Replaces what Shelf has now with a backup. Nothing is deleted: what you have now is set aside in a folder beside Shelf's data.")
            }
            Button {
                text: qsTr("Restore a backup…")
                enabled: !page.busy
                onClicked: backup.chooseRestore()
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2c2c33" }

            SectionLabel { text: qsTr("Flashcards to other apps") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.65
                text: qsTr("Study your %n card(s) on your phone too. The file is saved in the backup folder.", "", cards.totalCount)
            }
            RowLayout {
                spacing: 8
                Button {
                    text: qsTr("Export for Anki")
                    enabled: cards.totalCount > 0 && !page.busy
                    onClicked: backup.exportCards("anki")
                }
                Button {
                    text: qsTr("Export for Quizlet")
                    enabled: cards.totalCount > 0 && !page.busy
                    onClicked: backup.exportCards("quizlet")
                }
            }
            Item { implicitHeight: 4 }
        }
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        opacity: 0.55
    }
}
