import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A short message at the bottom of the window: what went wrong, or what happened.
Rectangle {
    id: toast
    signal dismissed()

    // An optional action ("Undo") runs `callback` when clicked.
    property var callback: null
    function show(message, actionLabel, run) {
        label.text = message
        action.text = actionLabel || ""
        toast.callback = run || null
        visible = true
        timer.restart()
    }

    visible: false
    width: Math.min(row.implicitWidth + 32, parent ? parent.width - 48 : 600)
    height: row.implicitHeight + 20
    radius: Math.min(8, theme.radius)
    color: "#26262c"
    border.color: "#3a3a42"

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        spacing: 12
        Label {
            id: label
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Button {
            id: action
            visible: text !== ""
            flat: true
            Material.foreground: theme.accent
            onClicked: {
                const run = toast.callback
                toast.visible = false
                toast.dismissed()
                if (run)
                    run()
            }
        }
        ToolButton {
            text: "✕"
            implicitWidth: 32
            implicitHeight: 32
            onClicked: { toast.visible = false; toast.dismissed() }
        }
    }

    Timer {
        id: timer
        interval: 6000
        onTriggered: { toast.visible = false; toast.dismissed() }
    }
}
