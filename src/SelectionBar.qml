import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Floats over a selected passage or a clicked highlight: highlight colors,
// a note, copy, and (for an existing highlight) delete.
Rectangle {
    id: bar
    property string currentColor: ""      // set when editing an existing highlight
    property bool existing: false
    signal colorPicked(string color)
    signal noteRequested()
    signal aiRequested()
    signal copyRequested()
    signal deleteRequested()

    // Places the bar above the box (x, y, w, h), or below it near the top,
    // kept inside `area`.
    function placeAt(x, y, w, h, area) {
        const above = y - height - 10
        bar.y = above >= 6 ? above : Math.min(area.height - height - 6, y + h + 10)
        bar.x = Math.max(6, Math.min(area.width - width - 6, x + w / 2 - width / 2))
    }

    readonly property var swatches: [
        { key: "yellow", color: "#f5d547" },
        { key: "green", color: "#6fd08c" },
        { key: "blue", color: "#64b0f4" },
        { key: "pink", color: "#f584b0" }
    ]

    width: row.implicitWidth + 16
    height: 44
    radius: Math.min(10, theme.radius + 4)
    color: "#202026"
    border.color: "#34343c"

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Repeater {
            model: bar.swatches
            delegate: Rectangle {
                required property var modelData
                width: 24
                height: 24
                radius: 12
                color: modelData.color
                border.width: bar.currentColor === modelData.key ? 3 : swatchMouse.containsMouse ? 2 : 0
                border.color: "white"
                MouseArea {
                    id: swatchMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.colorPicked(modelData.key)
                }
                ToolTip.visible: swatchMouse.containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("Highlight")
            }
        }

        Rectangle { width: 1; height: 22; color: "#3a3a42"; Layout.leftMargin: 4; Layout.rightMargin: 2 }

        ToolbarButton {
            iconName: "note"
            label: bar.existing ? qsTr("Edit note") : qsTr("Note")
            onClicked: bar.noteRequested()
        }
        ToolbarButton {
            label: "✦ " + qsTr("Ask AI")
            tip: qsTr("Explain, summarize, or quiz with local AI")
            onClicked: bar.aiRequested()
        }
        ToolbarButton {
            iconName: "copy"
            tip: qsTr("Copy")
            onClicked: bar.copyRequested()
        }
        ToolbarButton {
            visible: bar.existing
            iconName: "trash"
            tip: qsTr("Remove highlight")
            onClicked: bar.deleteRequested()
        }
    }
}
