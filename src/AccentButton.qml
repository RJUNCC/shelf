import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The main action in a dialog or toolbar, filled with the theme accent.
// Drawn here because Material's "highlighted" fill does not follow the accent
// inside popups.
Button {
    id: button
    implicitHeight: 38
    leftPadding: 18
    rightPadding: 18
    hoverEnabled: true

    background: Rectangle {
        radius: Math.min(8, theme.radius + 2)
        color: button.down ? Qt.darker(theme.accent, 1.2) : button.hovered ? Qt.lighter(theme.accent, 1.1) : theme.accent
    }
    contentItem: Label {
        text: button.text
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: theme.accentForeground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
