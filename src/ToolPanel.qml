import QtQuick
import QtWebEngine
import QtWebChannel

// A hands-on question's tool (code editor, Markov chain, molecule
// builder, scenario simulator), drawn by a web
// page in Chromium's sandbox (src/web/tools). load() shows a question;
// check() marks the answer and the result arrives in checked(result):
// {correct, usable, summary, answer}. A tool that can't check its question
// says usable: false, and the AI marks `answer` instead.
Item {
    id: panel

    property var question: null
    property bool pageReady: false
    signal checked(var result)

    function check() { web.runJavaScript("Tool.check()") }
    function reveal() { web.runJavaScript("Tool.reveal()") }

    function load() {
        if (!pageReady || !question)
            return
        const options = {
            question: question,
            accent: String(theme.accent),
            strings: {
                "Run examples": qsTr("Run examples"),
                "Start over": qsTr("Start over"),
                "Show solution": qsTr("Show solution"),
                "Hide solution": qsTr("Hide solution"),
                "Examples": qsTr("Examples"),
                "Printed": qsTr("Printed"),
                "Running…": qsTr("Running…"),
                "Loading Python…": qsTr("Loading Python…"),
                "Python is not installed with Shelf.": qsTr("Python is not installed with Shelf."),
                "It took too long and was stopped. Is there a loop that never ends?":
                    qsTr("It took too long and was stopped. Is there a loop that never ends?"),
                "Hidden test %1": qsTr("Hidden test %1"),
                "gave the wrong result": qsTr("gave the wrong result"),
                "got %1, expected %2": qsTr("got %1, expected %2"),
                "%1 of %2 tests pass": qsTr("%1 of %2 tests pass"),
                "Drag states to move them. Click one state, then another, to set the chance of moving between them.":
                    qsTr("Drag states to move them. Click one state, then another, to set the chance of moving between them."),
                "Transition probabilities (row: from, column: to)": qsTr("Transition probabilities (row: from, column: to)"),
                "The chain": qsTr("The chain"),
                "Long-run share of time in each state": qsTr("Long-run share of time in each state"),
                "Chance of being in each state after %1 steps from %2": qsTr("Chance of being in each state after %1 steps from %2"),
                "Decimals, percentages, or fractions like 1/3.": qsTr("Decimals, percentages, or fractions like 1/3."),
                "Sum": qsTr("Sum"),
                "Total: %1": qsTr("Total: %1"),
                "“%1” is not a number.": qsTr("“%1” is not a number."),
                "The chances out of %1 add up to %2; they must add up to 1.":
                    qsTr("The chances out of %1 add up to %2; they must add up to 1."),
                "Every transition is right.": qsTr("Every transition is right."),
                "%1 of %2 transitions are off; the right values are under the red ones.":
                    qsTr("%1 of %2 transitions are off; the right values are under the red ones."),
                "All right: %1.": qsTr("All right: %1."),
                "Draw %1": qsTr("Draw %1"),
                "Single bond": qsTr("Single bond"),
                "Double bond": qsTr("Double bond"),
                "Triple bond": qsTr("Triple bond"),
                "Move": qsTr("Move"),
                "Drag atoms to move them": qsTr("Drag atoms to move them"),
                "Click an atom to change its charge": qsTr("Click an atom to change its charge"),
                "Erase": qsTr("Erase"),
                "Click an atom or bond to remove it": qsTr("Click an atom or bond to remove it"),
                "Undo": qsTr("Undo"),
                "Clear": qsTr("Clear"),
                "Click to place an atom. Drag from an atom to bond a new one, or click it to grow a chain. Click a bond to change it. Hydrogens are added for you.": qsTr("Click to place an atom. Drag from an atom to bond a new one, or click it to grow a chain. Click a bond to change it. Hydrogens are added for you."),
                "Answer: %1": qsTr("Answer: %1"),
                "Draw the molecule first.": qsTr("Draw the molecule first."),
                "%1 has more bonds than it can make (shown in red).": qsTr("%1 has more bonds than it can make (shown in red)."),
                "That is %1.": qsTr("That is %1."),
                "Right formula (%1), but the atoms are connected differently.": qsTr("Right formula (%1), but the atoms are connected differently."),
                "Your drawing is %1; the answer is %2.": qsTr("Your drawing is %1; the answer is %2."),
                "%1 that makes %2 = %3": qsTr("%1 that makes %2 = %3"),
                "%1 that makes %2 as large as it can be": qsTr("%1 that makes %2 as large as it can be"),
                "%1 that makes %2 as small as it can be": qsTr("%1 that makes %2 as small as it can be"),
                "%1 for these values": qsTr("%1 for these values"),
                "%1 against": qsTr("%1 against"),
                "Work it out from the values given. Once you check, the simulator opens so you can see how the answer changes.": qsTr("Work it out from the values given. Once you check, the simulator opens so you can see how the answer changes."),
                "Enter a number first.": qsTr("Enter a number first."),
                "%1 is right.": qsTr("%1 is right."),
                "Benzene ring": qsTr("Benzene ring"),
                "Six-membered ring": qsTr("Six-membered ring"),
                "Five-membered ring": qsTr("Five-membered ring"),
                "click empty space, an atom, or a bond": qsTr("click empty space, an atom, or a bond"),
                "The answer is %1.": qsTr("The answer is %1.")
            }
        }
        web.runJavaScript("Tool.load(" + JSON.stringify(options) + ")")
    }
    onQuestionChanged: load()

    QtObject {
        id: host
        WebChannel.id: "host"
        function ready() {
            panel.pageReady = true
            panel.load()
        }
        function checked(json) { panel.checked(JSON.parse(json)) }
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
        url: "shelf://app/tools/tool.html"
        onContextMenuRequested: request => request.accepted = true
        // The tools never leave Shelf.
        onNavigationRequested: request => {
            if (!request.url.toString().startsWith("shelf://app/"))
                request.reject()
        }
        onJavaScriptConsoleMessage: (level, message, line, source) => {
            if (level === WebEngineView.ErrorMessageLevel)
                console.warn("tool:", message, source + ":" + line)
        }
    }
}
