import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// "What can I do here?": a small card shown the first few times a part of
// Shelf is used, with its main features in a line each. Once dismissed it
// stays away (setting "tip:<key>"); Settings › Help can bring tips back.
Rectangle {
    id: tip
    property string key
    property string title
    property var lines: []      // strings; StyledText is allowed
    // An extra condition, such as the tab being the one shown.
    property bool when: true
    property bool dismissed: library.setting("tip:" + key, false) === true
    readonly property bool shown: when && !dismissed

    Connections {
        target: library
        // Settings › Help › Show tips again.
        function onSettingsReset() { tip.dismissed = library.setting("tip:" + tip.key, false) === true }
    }

    function dismiss() {
        library.setSetting("tip:" + key, true)
        dismissed = true
    }

    visible: shown
    width: Math.min(340, parent ? parent.width - 32 : 340)
    implicitHeight: column.implicitHeight + 24
    radius: Math.min(10, theme.radius)
    color: "#1f1f26"
    border.color: Qt.darker(theme.accent, 1.6)
    opacity: visible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "✦  " + tip.title
                font.weight: Font.DemiBold
                color: theme.accent
                wrapMode: Text.WordWrap
            }
            ToolButton {
                text: "✕"
                implicitWidth: 28
                implicitHeight: 28
                onClicked: tip.dismiss()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Got it")
            }
        }
        Repeater {
            model: tip.lines
            delegate: Label {
                required property string modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                font.pixelSize: 12
                opacity: 0.85
                text: "•  " + modelData
            }
        }
        Button {
            Layout.alignment: Qt.AlignRight
            flat: true
            text: qsTr("Got it")
            onClicked: tip.dismiss()
        }
    }
}
