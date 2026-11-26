import QtQuick
import QtWebEngine
import QtWebChannel

// Markdown from the AI, drawn visually: typeset formulas, diagrams, charts,
// callouts and numbered steps (src/web/rich). It sizes itself to its
// content like a Label, so it sits in a column; the wheel scrolls
// `flickable` (the ScrollView around it) rather than the page.
Item {
    id: rich

    property string text: ""
    // Still being written: diagrams wait until it's finished.
    property bool partial: false
    // Shown under the text: [{n, label, text}], linked as "source:n".
    property var sources: []
    property int fontSize: 13
    property color background: "#18181d"
    property Flickable flickable: null
    signal linkActivated(string link)

    implicitHeight: Math.max(20, contentHeight)
    property real contentHeight: 0
    property bool pageReady: false

    function update() {
        if (!pageReady)
            return
        web.runJavaScript("Rich.show(" + JSON.stringify(text) + "," + JSON.stringify({ partial: partial, sources: sources || [] }) + ")")
    }
    function style() {
        if (!pageReady)
            return
        web.runJavaScript("Rich.setStyle(" + JSON.stringify({
            accent: String(theme.accent), background: String(background), size: fontSize,
            strings: {
                "SOURCES": qsTr("SOURCES"),
                "Drawing the diagram…": qsTr("Drawing the diagram…"),
                "This diagram could not be drawn. What the AI wrote:": qsTr("This diagram could not be drawn. What the AI wrote:")
            }
        }) + ")")
    }

    // While an answer streams in, redraw a few times a second rather than
    // for every word, to keep the processor quiet.
    Timer {
        id: throttle
        interval: 180
        onTriggered: rich.update()
    }
    onTextChanged: partial ? (throttle.running || throttle.start()) : update()
    onPartialChanged: { throttle.stop(); update() }
    onSourcesChanged: update()
    onFontSizeChanged: { style(); update() }
    Connections {
        target: theme
        function onAccentChanged() { rich.style(); rich.update() }
    }

    QtObject {
        id: host
        WebChannel.id: "host"
        function ready() {
            rich.pageReady = true
            rich.style()
            rich.update()
        }
        function resized(height) { rich.contentHeight = height }
        function link(href) { rich.linkActivated(href) }
        function wheel(dy) {
            const f = rich.flickable
            if (!f)
                return
            const max = Math.max(0, f.contentHeight - f.height)
            f.contentY = Math.max(0, Math.min(max, f.contentY + dy))
        }
    }
    WebChannel {
        id: channel
        registeredObjects: [host]
    }
    WebEngineView {
        id: web
        anchors.fill: parent
        webChannel: channel
        backgroundColor: rich.background
        url: "shelf://app/rich/rich.html"
        onContextMenuRequested: request => request.accepted = true
        onNavigationRequested: request => {
            if (!request.url.toString().startsWith("shelf://app/"))
                request.reject()
        }
        onJavaScriptConsoleMessage: (level, message, line, source) => {
            if (level === WebEngineView.ErrorMessageLevel)
                console.warn("rich:", message, source + ":" + line)
        }
    }
}
