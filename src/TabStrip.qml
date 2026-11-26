import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Browser-style tabs: the library first, then one tab per open book. Book
// tabs drag to a new place, close with ×, a middle click, or their
// right-click menu.
Rectangle {
    id: strip
    property var model          // ListModel of { bookId, title, format }
    property int currentIndex   // 0 is the library
    signal activated(int index)
    signal closeRequested(int index)
    signal closeOthersRequested(int index)
    signal closeRightRequested(int index)
    signal moveRequested(int from, int to)

    implicitHeight: 40
    color: "#0b0b0d"

    readonly property color activeColor: "#17171b"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 2

        // The library tab never closes: the whole library in a big view.
        // The panel beside the tabs (LibrarySidebar) shows it compactly too.
        TabButtonItem {
            Layout.preferredWidth: implicitWidth
            Layout.alignment: Qt.AlignBottom
            iconName: "library"
            title: qsTr("Library")
            active: strip.currentIndex === 0
            closable: false
            onClicked: strip.activated(0)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 2
            clip: true
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            model: strip.model
            // Tabs share the room like a browser's: wide when few, narrower
            // as more open, never smaller than a readable minimum.
            readonly property real tabWidth: count === 0 ? 0
                : Math.max(130, Math.min(240, (width - (count - 1) * spacing) / count))

            // Tabs slide aside as one is dragged past them.
            displaced: Transition { NumberAnimation { properties: "x"; duration: 140; easing.type: Easing.OutCubic } }
            move: Transition { NumberAnimation { properties: "x"; duration: 140; easing.type: Easing.OutCubic } }

            delegate: TabButtonItem {
                id: bookTab
                required property int index
                required property var model
                width: list.tabWidth
                y: list.height - height
                z: dragging ? 2 : 0
                iconName: model.format === "workspace" ? "slides" : model.format === "pdf" ? "pages" : "outline"
                title: model.title
                active: strip.currentIndex === index + 1
                draggable: true
                onClicked: strip.activated(index + 1)
                onCloseClicked: strip.closeRequested(index + 1)
                onMenuRequested: (x, y) => {
                    tabMenu.tabIndex = index + 1
                    tabMenu.popup(bookTab, x, y)
                }
                // Dragged over another tab's place, it takes that place.
                onDraggedTo: x => {
                    const slot = list.tabWidth + list.spacing
                    const p = bookTab.mapToItem(list.contentItem, x, 0)
                    const target = Math.max(0, Math.min(list.count - 1, Math.floor(p.x / slot)))
                    if (target !== index)
                        strip.moveRequested(index + 1, target + 1)
                }
            }

            onCountChanged: if (contentWidth > width) positionViewAtEnd()
        }
    }

    Menu {
        id: tabMenu
        property int tabIndex: 0
        Material.roundedScale: theme.radius
        MenuItem { text: qsTr("Close"); onTriggered: strip.closeRequested(tabMenu.tabIndex) }
        MenuItem {
            text: qsTr("Close other tabs")
            enabled: list.count > 1
            onTriggered: strip.closeOthersRequested(tabMenu.tabIndex)
        }
        MenuItem {
            text: qsTr("Close tabs to the right")
            enabled: tabMenu.tabIndex < list.count
            onTriggered: strip.closeRightRequested(tabMenu.tabIndex)
        }
    }

    component TabButtonItem: Rectangle {
        id: tab
        property string iconName
        property string title
        property bool active: false
        property bool closable: true
        property bool draggable: false
        property bool dragging: false
        signal clicked()
        signal closeClicked()
        signal menuRequested(real x, real y)
        signal draggedTo(real x)

        implicitWidth: tabRow.implicitWidth + 24
        implicitHeight: 34
        height: 34
        radius: Math.min(7, theme.radius)
        color: active ? strip.activeColor : tabMouse.containsMouse ? "#151519" : "transparent"

        // Square off the bottom so the active tab joins the toolbar below it.
        Rectangle {
            visible: tab.active
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: strip.activeColor
        }

        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            property real pressX: 0
            onPressed: mouse => {
                pressX = mouse.x
                // A tab shows as soon as it is pressed, as in a browser, so
                // dragging it also shows what is being moved.
                if (mouse.button === Qt.LeftButton)
                    tab.clicked()
            }
            onPositionChanged: mouse => {
                if (!tab.draggable || !(mouse.buttons & Qt.LeftButton))
                    return
                if (!tab.dragging && Math.abs(mouse.x - pressX) > 8)
                    tab.dragging = true
                if (tab.dragging)
                    tab.draggedTo(mouse.x)
            }
            onReleased: tab.dragging = false
            onCanceled: tab.dragging = false
            onClicked: mouse => {
                if (mouse.button === Qt.MiddleButton && tab.closable)
                    tab.closeClicked()
                else if (mouse.button === Qt.RightButton && tab.closable)
                    tab.menuRequested(mouse.x, mouse.y)
            }
        }

        RowLayout {
            id: tabRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: tab.closable ? 6 : 12
            spacing: 8
            Icon {
                name: tab.iconName
                size: 15
                color: tab.active ? theme.accent : "#9a9aa2"
            }
            Label {
                Layout.fillWidth: tab.closable
                text: tab.title
                elide: Text.ElideRight
                font.pixelSize: 13
                color: tab.active ? "#f0f0f2" : "#a8a8b0"
            }
            AbstractButton {
                visible: tab.closable
                implicitWidth: 22
                implicitHeight: 22
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                opacity: tab.active || tabMouse.containsMouse || hovered ? 1 : 0.35
                onClicked: tab.closeClicked()
                background: Rectangle {
                    radius: Math.min(5, theme.radius)
                    color: parent.hovered ? "#2c2c33" : "transparent"
                }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "close"; size: 13; color: "#c8c8ce" }
                }
            }
        }
    }
}
