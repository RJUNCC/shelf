import QtQuick
import QtWebEngine
import QtWebChannel

// Plays an animated lesson (src/web/rich/lesson.html): scenes that build as
// they're narrated, with captions, and the voice when one is installed.
Item {
    id: player

    property var lesson: null
    property bool autoplay: true
    // Follow-up questions need the material the lesson came from.
    property bool canAsk: true
    property string closeLabel: qsTr("Back to the slide")
    property bool pageReady: false
    // {playing, scene, sentence, scenes, done}
    property var playback: ({})
    signal askRequested(string question)
    signal closeRequested()

    function load() {
        if (!pageReady || !lesson)
            return
        const options = {
            autoplay: autoplay,
            ask: canAsk,
            accent: String(theme.accent),
            speed: Number(library.setting("lessonSpeed", 1)),
            captions: library.setting("lessonCaptions", true) === true,
            reduced: library.setting("animations", "full") === "reduced",
            voice: typeof voice !== "undefined" && voice.ready && library.setting("lessonVoice", true) === true,
            voiceRate: typeof voice !== "undefined" && voice.playbackPace,
            volume: Number(library.setting("lessonVolume", 1)),
            board: String(library.setting("lessonBoard", "whiteboard")),
            strings: {
                "Ask about this…": qsTr("Ask about this…"),
                "Captions": qsTr("Captions"),
                "Start again": qsTr("Start again"),
                "Previous scene": qsTr("Previous scene"),
                "Next scene": qsTr("Next scene"),
                "Back to the slide": closeLabel,
                "Pause (Space)": qsTr("Pause (Space)"),
                "Play (Space)": qsTr("Play (Space)"),
                "Volume": qsTr("Volume"),
                "Speed": qsTr("Speed"),
                "Whiteboard": qsTr("Whiteboard"),
                "Chalkboard": qsTr("Chalkboard"),
                "Notebook": qsTr("Notebook"),
                "Clean": qsTr("Clean"),
                "Getting the voice ready…": qsTr("Getting the voice ready…")
            }
        }
        web.runJavaScript("Stage.load(" + JSON.stringify(lesson) + "," + JSON.stringify(options) + ")")
    }
    onLessonChanged: load()
    function pause() { if (pageReady) web.runJavaScript("Stage.pause()") }
    function toggle() { if (pageReady) web.runJavaScript("Stage.toggle()") }
    function setStatus(text) { if (pageReady) web.runJavaScript("Stage.setStatus(" + JSON.stringify(text) + ")") }
    // A sentence's voice is ready (or "" when it failed).
    function voiceReady(scene, sentence, url) {
        if (pageReady)
            web.runJavaScript("Stage.voiceReady(" + scene + "," + sentence + "," + JSON.stringify(url) + ")")
    }
    signal speakRequested(int scene, int sentence, string text)
    signal prepareRequested(int scene, int sentence, string text)

    // Stops talking when hidden.
    onVisibleChanged: if (!visible) pause()

    QtObject {
        id: host
        WebChannel.id: "host"
        function ready() {
            player.pageReady = true
            player.load()
        }
        function state(json) { player.playback = JSON.parse(json) }
        function speak(scene, sentence, text) { player.speakRequested(scene, sentence, text) }
        function prepare(scene, sentence, text) { player.prepareRequested(scene, sentence, text) }
        function ask(question) { player.askRequested(question) }
        function setting(key, value) {
            if (key === "speed")
                library.setSetting("lessonSpeed", value)
            else if (key === "captions")
                library.setSetting("lessonCaptions", value)
            else if (key === "volume")
                library.setSetting("lessonVolume", value)
            else if (key === "board")
                library.setSetting("lessonBoard", value)
        }
        function close() { player.closeRequested() }
    }
    WebChannel {
        id: channel
        registeredObjects: [host]
    }
    WebEngineView {
        id: web
        anchors.fill: parent
        webChannel: channel
        activeFocusOnPress: true
        backgroundColor: "#141418"
        url: "shelf://app/rich/lesson.html"
        onContextMenuRequested: request => request.accepted = true
        onNavigationRequested: request => {
            if (!request.url.toString().startsWith("shelf://app/"))
                request.reject()
        }
        onJavaScriptConsoleMessage: (level, message, line, source) => {
            if (level === WebEngineView.ErrorMessageLevel)
                console.warn("lesson:", message, source + ":" + line)
        }
    }
}
