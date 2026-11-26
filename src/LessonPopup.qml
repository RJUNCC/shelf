import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// An animated lesson about a passage or a practice question, over whatever
// is open. explain(input) takes what Explainer::explain does.
Popup {
    id: popup
    anchors.centerIn: Overlay.overlay
    width: Math.min(1100, (parent ? parent.width : 1200) - 60)
    height: Math.min(760, (parent ? parent.height : 800) - 60)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#141418"; radius: theme.radius; border.color: "#2c2c33" }

    property var input: ({})
    property string key: ""
    property var lesson: null

    function explain(what) {
        input = what
        key = what.key
        lesson = null
        open()
        const known = explainer.cached(what)
        if (Object.keys(known).length > 0)
            lesson = known
        else
            explainer.explain(what)
    }
    function follow(question) {
        const next = Object.assign({}, input, { key: input.key + "?" + question, question: question })
        key = next.key
        lesson = null
        explainer.explain(next)
    }
    // A lesson already in hand (the sample in Local AI's settings).
    function show(what, key) {
        input = {}
        popup.key = key
        lesson = what
        open()
    }
    Connections {
        target: voice
        function onSpoken(k, scene, sentence, url) {
            if (k === popup.key && popup.opened)
                player.voiceReady(scene, sentence, url)
        }
    }
    onClosed: {
        voice.stop()
        if (explainer.busy && explainer.pending === key)
            explainer.cancel()
        lesson = null
    }
    Connections {
        target: explainer
        function onReady(k, lesson) {
            if (k === popup.key && popup.opened)
                popup.lesson = lesson
        }
    }

    LessonPlayer {
        id: player
        anchors.fill: parent
        anchors.margins: 1
        visible: popup.lesson !== null
        lesson: popup.lesson
        closeLabel: qsTr("Close")
        canAsk: popup.input.text !== undefined && ollama.status === "ready"
        onAskRequested: question => popup.follow(question)
        onCloseRequested: popup.close()
        onSpeakRequested: (scene, sentence, text) => win.speakFor(player, popup.key, scene, sentence, text, true)
        onPrepareRequested: (scene, sentence, text) => win.speakFor(player, popup.key, scene, sentence, text, false)
    }

    ColumnLayout {
        visible: popup.lesson === null
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 40)
        spacing: 12
        BusyIndicator {
            visible: explainer.busy
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: explainer.busy ? qsTr("Writing the lesson…") : explainer.error
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            color: explainer.busy ? Material.foreground : "#ff8a80"
        }
        Label {
            visible: explainer.busy
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("The local AI usually takes under a minute.")
            opacity: 0.6
            font.pixelSize: 12
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            flat: true
            text: explainer.busy ? qsTr("Stop") : qsTr("Close")
            onClicked: popup.close()
        }
    }
}
