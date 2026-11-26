import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings › Help: where to start, the shortcuts, and what to send when
// something goes wrong.
Item {
    id: page
    signal closeRequested()

    property bool copied: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label { text: qsTr("Help"); font.pixelSize: 18; font.weight: Font.DemiBold }

        HelpRow {
            title: qsTr("Learn Shelf")
            about: qsTr("A short course, inside Shelf, on reading, notes, flashcards, practice tests and workspaces.")
            action: qsTr("Open")
            onActivated: { page.closeRequested(); win.openLearn() }
        }
        HelpRow {
            title: qsTr("Tips")
            about: qsTr("Each part of Shelf shows a short tip the first time you use it. Bring them back to see them again.")
            action: qsTr("Show tips again")
            onActivated: { library.resetSettings("tip:"); page.closeRequested() }
        }
        HelpRow {
            title: qsTr("Keyboard shortcuts")
            about: qsTr("Press ? anywhere to see them.")
            action: qsTr("Show")
            onActivated: { page.closeRequested(); win.helpVisible = true }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2c2c33" }

        HelpRow {
            title: qsTr("Something went wrong?")
            about: qsTr("Copy a short report (Shelf's version, your system, the local AI and voice, and Shelf's recent warnings) and send it to whoever helps you with Shelf. Shelf never sends it anywhere itself.")
            action: page.copied ? qsTr("Copied") : qsTr("Copy diagnostic info")
            onActivated: {
                diagnostics.copyReport()
                page.copied = true
                copiedTimer.restart()
            }
        }
        Button {
            flat: true
            text: qsTr("Open the log folder")
            onClicked: diagnostics.openLogFolder()
        }
        Item { Layout.fillHeight: true }
    }

    Timer { id: copiedTimer; interval: 2500; onTriggered: page.copied = false }

    component HelpRow: RowLayout {
        id: row
        property string title
        property string about
        property string action
        signal activated()
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { text: row.title; font.pixelSize: 14 }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.6
                text: row.about
            }
        }
        Button {
            Layout.alignment: Qt.AlignTop
            text: row.action
            onClicked: row.activated()
        }
    }
}
