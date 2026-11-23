import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A toolbar button with an icon and, when there is room, a label, so tools
// like "Outline" are obvious rather than hidden behind a glyph.
AbstractButton {
    id: button
    property string iconName
    property string label
    property string tip
    property bool showLabel: label !== ""

    implicitHeight: 34
    implicitWidth: row.implicitWidth + 20
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 500

    background: Rectangle {
        radius: Math.min(6, theme.radius)
        color: button.checked ? Qt.alpha(theme.accent, 0.18)
             : button.down ? "#2a2a31" : button.hovered ? "#222228" : "transparent"
    }

    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 7
            Icon {
                visible: button.iconName !== ""
                name: button.iconName
                size: 17
                color: button.checked ? theme.accent : "#e2e2e6"
            }
            Label {
                visible: button.showLabel
                text: button.label
                font.pixelSize: 13
                color: button.checked ? theme.accent : "#e2e2e6"
            }
        }
    }
}
