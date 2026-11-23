import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Writes or edits the note on a highlight (or a page, for PDFs).
Popup {
    id: editor
    property string quote: ""
    property string heading: qsTr("Note")
    property bool canDelete: false
    signal saved(string note)
    signal deleted()

    function openWith(text, existingNote) {
        quote = text
        noteArea.text = existingNote || ""
        open()
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min(560, (parent ? parent.width : 600) - 40)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#1b1b20"; radius: theme.radius; border.color: "#2c2c33" }

    onOpened: noteArea.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            text: editor.heading
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        // The passage, quoted, so the note has its context.
        Rectangle {
            visible: editor.quote !== ""
            Layout.fillWidth: true
            implicitHeight: quoteLabel.implicitHeight + 20
            radius: Math.min(6, theme.radius)
            color: "#15151a"
            Rectangle { width: 3; height: parent.height; color: theme.accent; radius: 1 }
            Label {
                id: quoteLabel
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 16
                text: editor.quote
                wrapMode: Text.WordWrap
                maximumLineCount: 6
                elide: Text.ElideRight
                font.italic: true
                opacity: 0.8
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            TextArea {
                id: noteArea
                placeholderText: qsTr("Write a note…")
                wrapMode: TextArea.Wrap
                selectByMouse: true
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return && (event.modifiers & Qt.ControlModifier)) {
                        editor.saved(noteArea.text.trim())
                        editor.close()
                        event.accepted = true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                visible: editor.canDelete
                text: qsTr("Delete")
                flat: true
                Material.foreground: "#ff8a80"
                onClicked: { editor.deleted(); editor.close() }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("Ctrl+Enter to save")
                font.pixelSize: 11
                opacity: 0.45
            }
            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: editor.close()
            }
            AccentButton {
                text: qsTr("Save")
                onClicked: { editor.saved(noteArea.text.trim()); editor.close() }
            }
        }
    }
}
