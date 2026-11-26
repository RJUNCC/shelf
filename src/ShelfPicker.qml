import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which books to show: a built-in shelf or one of the user's collections.
// Each collection in the list has its own delete button (and a right-click
// menu), so deleting one takes a single click; the toast offers Undo.
ComboBox {
    id: picker

    readonly property var shelves: [
        { key: "all", label: qsTr("All books") },
        { key: "reading", label: qsTr("Reading") },
        { key: "finished", label: qsTr("Finished") },
        { key: "epub", label: qsTr("EPUB") },
        { key: "pdf", label: qsTr("PDF") }
    ]
    readonly property var filters: shelves.concat(library.collections.map(c => ({ key: "collection:" + c, label: "◆  " + c, name: c })))

    model: filters
    textRole: "label"
    currentIndex: Math.max(0, filters.findIndex(f => f.key === library.shelf))
    onActivated: library.shelf = filters[currentIndex].key

    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : picker.width
        highlighted: picker.highlightedIndex === index
        font.pixelSize: picker.font.pixelSize
        rightPadding: modelData.name ? 44 : 16
        contentItem: Label {
            text: row.modelData.label
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.weight: library.shelf === row.modelData.key ? Font.DemiBold : Font.Normal
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: !!row.modelData.name
            onTapped: {
                rowMenu.name = row.modelData.name
                rowMenu.popup()
            }
        }

        ToolButton {
            visible: !!row.modelData.name
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 32
            implicitHeight: 32
            focusPolicy: Qt.NoFocus
            opacity: row.hovered || row.highlighted || hovered ? 1 : 0.45
            icon.source: "image://icon/trash?color=" + encodeURIComponent(hovered ? "#ff8a80" : "#c8c8ce")
            icon.width: 15
            icon.height: 15
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Delete collection (its books stay)")
            onClicked: {
                picker.popup.close()
                win.deleteCollection(row.modelData.name)
            }
        }
    }

    Menu {
        id: rowMenu
        property string name
        Material.roundedScale: theme.radius
        MenuItem {
            text: qsTr("Delete collection")
            onTriggered: {
                picker.popup.close()
                win.deleteCollection(rowMenu.name)
            }
        }
    }
}
