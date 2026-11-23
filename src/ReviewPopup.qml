import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Reviews the flashcards that are due: the question, then the answer on
// Space, then how well it was remembered (1–4), which sets when it comes
// back. Cards forgotten come back later in the same session.
Popup {
    id: review
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, (parent ? parent.width : 760) - 40)
    height: Math.min(520, (parent ? parent.height : 560) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    property var queue: []
    property int done: 0
    property bool revealed: false
    readonly property var card: queue.length > 0 ? queue[0] : null

    function refill() {
        queue = cards.due()
        revealed = false
    }
    onOpened: {
        done = 0
        refill()
        forceActiveFocus()
    }

    function grade(value) {
        if (!card || !revealed)
            return
        cards.grade(card.id, value)
        done += 1
        // Again brings it back in minutes; the rest of the queue goes first.
        const rest = queue.slice(1)
        queue = rest.length > 0 ? rest : cards.due()
        revealed = false
    }
    function bookTitle(id) {
        const b = library.book(id)
        return b && b.title ? b.title : ""
    }

    Shortcut { enabled: review.opened; sequences: ["Space", "Return"]; onActivated: review.revealed ? review.grade(2) : review.revealed = true }
    Shortcut { enabled: review.opened && review.revealed; sequence: "1"; onActivated: review.grade(0) }
    Shortcut { enabled: review.opened && review.revealed; sequence: "2"; onActivated: review.grade(1) }
    Shortcut { enabled: review.opened && review.revealed; sequence: "3"; onActivated: review.grade(2) }
    Shortcut { enabled: review.opened && review.revealed; sequence: "4"; onActivated: review.grade(3) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Review")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                Layout.leftMargin: 8
                visible: review.card !== null
                text: qsTr("%1 left").arg(review.queue.length.toLocaleString(Qt.locale(), 'f', 0))
                font.pixelSize: 13
                opacity: 0.55
            }
            Item { Layout.fillWidth: true }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: review.close()
            }
        }

        // Nothing due.
        ColumnLayout {
            visible: review.card === null
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Item { Layout.fillHeight: true }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: review.done > 0 ? qsTr("All done for now") : qsTr("Nothing to review")
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 60
                Layout.rightMargin: 60
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.6
                text: review.done > 0
                      ? qsTr("You reviewed %n card(s). The next ones come back when they are due.", "", review.done)
                      : cards.totalCount > 0
                        ? qsTr("Your %n card(s) are not due yet.", "", cards.totalCount)
                        : qsTr("Select a passage in a book, press Ask AI, then Quiz me, and add the flashcards it writes.")
            }
            Item { Layout.fillHeight: true }
        }

        // The card.
        Rectangle {
            visible: review.card !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius
            color: "#131316"
            border.color: "#26262c"

            Flickable {
                anchors.fill: parent
                anchors.margins: 22
                contentHeight: cardColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: cardColumn
                    width: parent.width
                    spacing: 16
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: review.card ? review.bookTitle(review.card.bookId) : ""
                        font.pixelSize: 12
                        opacity: 0.5
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: review.card ? review.card.front : ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: 19
                        lineHeight: 1.2
                    }
                    Rectangle {
                        visible: review.revealed
                        Layout.fillWidth: true
                        height: 1
                        color: "#2c2c33"
                    }
                    Label {
                        visible: review.revealed
                        Layout.fillWidth: true
                        text: review.card ? review.card.back : ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: 16
                        lineHeight: 1.25
                        color: "#d8d8de"
                    }
                }
            }
        }

        // Show the answer, then grade it.
        RowLayout {
            visible: review.card !== null
            Layout.fillWidth: true
            spacing: 8
            AccentButton {
                visible: !review.revealed
                Layout.fillWidth: true
                text: qsTr("Show answer (Space)")
                onClicked: review.revealed = true
            }
            Repeater {
                model: review.revealed && review.card
                       ? [{ g: 0, label: qsTr("Again") }, { g: 1, label: qsTr("Hard") },
                          { g: 2, label: qsTr("Good") }, { g: 3, label: qsTr("Easy") }]
                       : []
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    highlighted: modelData.g === 2
                    Material.accent: theme.accent
                    // The key, the grade, and when the card would come back.
                    text: "%1  %2 · %3".arg(modelData.g + 1).arg(modelData.label)
                                       .arg(review.card ? cards.preview(review.card.id, modelData.g) : "")
                    onClicked: review.grade(modelData.g)
                }
            }
        }
    }
}
