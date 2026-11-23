import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import QtQuick.Shapes

// A PDF's pages, either scrolled continuously or turned one at a time, side
// to side or top to bottom. Text can be selected on any page, and saved
// highlights and search results are drawn over it.
//
// The list never flicks on its own: a mouse drag selects text, and the wheel
// is handled here so the scroll speed is the user's to choose.
Item {
    id: root
    required property PdfDocument document
    // The file, for selecting text by character.
    property string path: ""
    // "" shows pages as they are; "clean" whitens the paper and darkens the
    // ink; "sharp" also sharpens blurry scans (see CleanPageProvider).
    property string cleanMode: ""
    readonly property bool cleanPages: cleanMode !== ""
    // False until the reader has gone to the saved page, so nothing is
    // cleaned for the pages passed on the way there.
    property bool ready: true
    property PdfSearchModel searchModel: null
    // "scroll", "horizontal" (side to side), or "vertical" (top to bottom).
    property string mode: "scroll"
    // How far one wheel notch or touchpad swipe moves, as a multiple.
    property real scrollSpeed: 1
    // 1 fits the page: its width when scrolling, the whole page otherwise.
    property real zoom: 1
    // Highlights by page: { "12": [{ id, color, hasNote, paths }] }, with
    // paths as lists of points in PDF points.
    property var highlights: ({})

    readonly property bool paged: mode !== "scroll"
    readonly property int pageCount: document.status === PdfDocument.Ready ? document.pageCount : 0
    property int currentPage: 0
    // The scale of the page on screen, for the zoom label.
    readonly property real currentScale: pageCount > 0 ? scaleFor(document.pagePointSize(currentPage)) : 1

    signal textSelected(int page, string text, var paths, rect box)
    signal selectionCleared()
    signal highlightClicked(string id, rect box)

    readonly property var colors: ({ yellow: "#f5d547", green: "#6fd08c", blue: "#64b0f4", pink: "#f584b0" })

    function scaleFor(size) {
        if (size.width <= 0 || size.height <= 0 || width <= 0)
            return 1
        const fit = paged ? Math.min((width - 32) / size.width, (height - 32) / size.height)
                          : Math.min(width - 32, 1000) / size.width
        return Math.max(0.1, fit * zoom)
    }

    function goToPage(page, animate) {
        page = Math.max(0, Math.min(pageCount - 1, page))
        if (pageCount === 0)
            return
        scrollAnimation.stop()
        if (paged) {
            // Neighbours slide in; long jumps land at once.
            if (!animate || Math.abs(page - list.currentIndex) > 1)
                list.positionViewAtIndex(page, ListView.Beginning)
            list.currentIndex = page
        } else {
            list.positionViewAtIndex(page, ListView.Beginning)
            list.contentY -= 8
        }
        currentPage = page
    }
    function next() { goToPage(currentPage + 1, true) }
    function prev() { goToPage(currentPage - 1, true) }

    // Moves by pixels: along the list when scrolling, inside a zoomed page
    // otherwise, turning the page once there is no more of it to show.
    function scrollBy(dx, dy, smooth, notch) {
        if (!paged) {
            const maxY = list.originY + list.contentHeight + list.bottomMargin - list.height
            const minY = list.originY - list.topMargin
            const from = scrollAnimation.running ? scrollAnimation.to : list.contentY
            const y = Math.max(minY, Math.min(maxY, from + dy))
            const maxX = Math.max(0, list.contentWidth - list.width)
            list.contentX = Math.max(0, Math.min(maxX, list.contentX + dx))
            if (smooth) {
                scrollAnimation.stop()
                scrollAnimation.from = list.contentY
                scrollAnimation.to = y
                scrollAnimation.start()
            } else {
                scrollAnimation.stop()
                list.contentY = y
            }
            return
        }
        const flick = list.currentItem ? list.currentItem.flick : null
        if (flick) {
            const along = mode === "vertical" ? dy : (dx !== 0 ? dx : dy)
            const maxY = flick.contentHeight - flick.height
            const maxX = flick.contentWidth - flick.width
            const canY = (dy > 0 && flick.contentY < maxY - 0.5) || (dy < 0 && flick.contentY > 0.5)
            if (canY) {
                flick.contentY = Math.max(0, Math.min(maxY, flick.contentY + dy))
                return
            }
            const canX = (dx > 0 && flick.contentX < maxX - 0.5) || (dx < 0 && flick.contentX > 0.5)
            if (canX) {
                flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + dx))
                return
            }
            turnBy(along, notch === true)
        }
    }
    // A wheel notch turns one page. Touchpad movement adds up until it is
    // worth a page and then turns once, so a long swipe does not skip a
    // chapter.
    property real turnAccumulator: 0
    function turnBy(amount, notch) {
        if (notch) {
            amount > 0 ? next() : prev()
            turnAccumulator = 0
            return
        }
        if (turnCooldown.running)
            return
        turnAccumulator += amount
        if (Math.abs(turnAccumulator) < 60)
            return
        turnAccumulator > 0 ? next() : prev()
        turnAccumulator = 0
        turnCooldown.start()
    }
    Timer { id: turnCooldown; interval: 260 }

    function clearSelection() {
        if (activeSelection)
            activeSelection.clearChosen()
        activeSelection = null
    }
    property var activeSelection: null
    // Counting clicks for double (word) and triple (sentence) selection.
    property real lastTapTime: 0
    property point lastTapPoint
    property int lastTapSlot: -1
    property int tapRun: 0

    // With clean-up on, the pages just ahead (and a couple behind) are
    // cleaned in the background once the reader pauses on a page.
    onCurrentPageChanged: if (cleanPages && ready) aheadTimer.restart()
    onCleanModeChanged: if (cleanPages && ready) aheadTimer.restart()
    onReadyChanged: if (cleanPages && ready) aheadTimer.restart()
    Timer {
        id: aheadTimer
        interval: 400
        onTriggered: {
            const size = root.document.pagePointSize(root.currentPage)
            const width = Math.round(size.width * root.scaleFor(size) * Screen.devicePixelRatio)
            pdfGeometry.cleanAhead(root.path, root.currentPage - 2, root.currentPage + 14, root.cleanMode, width)
        }
    }

    NumberAnimation {
        id: scrollAnimation
        target: list
        property: "contentY"
        duration: 160
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        target: null
        acceptedModifiers: Qt.KeyboardModifierMask
        // On Wayland a mouse can arrive as a touchpad, so take every device
        // and tell wheels from touchpads by the event itself.
        acceptedDevices: PointerDevice.AllDevices
        property real notchRemainder: 0
        onWheel: event => {
            if (event.modifiers & Qt.ControlModifier) {
                root.zoom = Math.max(0.3, Math.min(6, root.zoom * (event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)))
                return
            }
            // A touchpad scrolls in phases and reports pixels; a wheel
            // reports notches of 120 (or parts of one on smooth wheels),
            // each notch worth 100 pixels at normal speed. Some wheels send
            // pixels too, which are ignored in favour of the notches.
            const hasPixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
            const wholeNotch = (event.angleDelta.y !== 0 && event.angleDelta.y % 120 === 0)
                            || (event.angleDelta.x !== 0 && event.angleDelta.x % 120 === 0)
            const touchpad = hasPixels && (event.phase !== Qt.NoScrollPhase || !wholeNotch)
            let dx = touchpad ? -event.pixelDelta.x : -event.angleDelta.x / 120 * 100
            let dy = touchpad ? -event.pixelDelta.y : -event.angleDelta.y / 120 * 100
            if ((event.modifiers & Qt.ShiftModifier) && dx === 0) {
                dx = dy
                dy = 0
            }
            // Turning pages: one page per whole notch, however fast.
            let notch = false
            if (!touchpad && root.paged) {
                notchRemainder += dx !== 0 && root.mode === "horizontal" ? dx : dy
                if (Math.abs(notchRemainder) < 100)
                    return
                notch = true
                dy = dx = 0
                if (root.mode === "horizontal") dx = notchRemainder; else dy = notchRemainder
                notchRemainder = 0
            }
            root.scrollBy(dx * root.scrollSpeed, dy * root.scrollSpeed, !touchpad, notch)
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        interactive: false
        model: root.pageCount
        orientation: root.mode === "horizontal" ? ListView.Horizontal : ListView.Vertical
        spacing: root.paged ? 0 : 14
        topMargin: root.paged ? 0 : 16
        bottomMargin: root.paged ? 0 : 16
        // Wide enough for the widest page when zoomed in past the window.
        contentWidth: root.paged ? -1 : Math.max(width, widest + 32)
        property real widest: 0
        // Pages just out of view are prepared ahead (cleaned, if clean-up is
        // on), so scrolling reaches them ready.
        cacheBuffer: root.mode === "horizontal" ? Math.max(0, width * 2) : root.paged ? Math.max(0, height * 2) : Math.max(0, height * 3)
        snapMode: root.paged ? ListView.SnapOneItem : ListView.NoSnap
        highlightRangeMode: root.paged ? ListView.StrictlyEnforceRange : ListView.NoHighlightRange
        preferredHighlightBegin: 0
        preferredHighlightEnd: root.paged ? (orientation === ListView.Horizontal ? width : height) : 0
        highlightMoveDuration: 240
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { visible: !root.paged }

        onCurrentIndexChanged: if (root.paged) root.currentPage = currentIndex
        onContentYChanged: if (!root.paged) findCurrentPage()
        onHeightChanged: if (!root.paged) findCurrentPage()
        function findCurrentPage() {
            // The page under a point a third of the way down, or the one
            // above the gap if the point falls between pages.
            for (const f of [0.35, 0.3, 0.45]) {
                const i = indexAt(contentX + width / 2, contentY + height * f)
                if (i >= 0) {
                    root.currentPage = i
                    return
                }
            }
        }

        delegate: Item {
            id: slot
            required property int index
            readonly property size pointSize: root.document.pagePointSize(index)
            readonly property real pageScale: root.scaleFor(pointSize)
            readonly property real pageWidth: pointSize.width * pageScale
            readonly property real pageHeight: pointSize.height * pageScale
            property alias flick: flick

            width: root.paged ? list.width : list.contentWidth
            height: root.paged ? list.height : pageHeight
            onPageWidthChanged: {
                if (!root.paged && pageWidth > list.widest)
                    list.widest = pageWidth
                // Re-render sharp only once resizing (opening the outline,
                // dragging the window) has settled.
                renderTimer.restart()
            }
            // Set once, then only by the timer: a binding would follow every
            // step of a resize and re-render each time.
            property real renderWidth: 0
            Component.onCompleted: renderWidth = pageWidth
            Timer {
                id: renderTimer
                interval: 220
                onTriggered: slot.renderWidth = slot.pageWidth
            }

            // Pans a page zoomed past the window when turning pages; when
            // scrolling, the page is exactly this size and never moves.
            Flickable {
                id: flick
                anchors.fill: parent
                interactive: false
                clip: root.paged
                contentWidth: Math.max(width, slot.pageWidth + (root.paged ? 32 : 0))
                contentHeight: Math.max(height, slot.pageHeight + (root.paged ? 32 : 0))
                ScrollBar.vertical: ScrollBar { visible: root.paged && flick.contentHeight > flick.height }
                ScrollBar.horizontal: ScrollBar { visible: flick.contentWidth > flick.width }

                Rectangle {
                    id: paper
                    x: (flick.contentWidth - width) / 2
                    y: (flick.contentHeight - height) / 2
                    width: slot.pageWidth
                    height: slot.pageHeight
                    color: "white"

                    // Cleaned-up rendering of the page, when asked for.
                    Image {
                        id: cleanImage
                        anchors.fill: parent
                        visible: root.cleanPages
                        source: root.cleanPages && root.ready && root.path !== ""
                                ? "image://cleanpage/" + encodeURIComponent(root.path) + "/" + slot.index + "/" + root.cleanMode : ""
                        asynchronous: true
                        retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: Math.round(slot.renderWidth * Screen.devicePixelRatio)
                    }
                    PdfPageImage {
                        id: image
                        visible: !root.cleanPages || cleanImage.status !== Image.Ready
                        anchors.fill: parent
                        document: root.document
                        currentFrame: slot.index
                        asynchronous: true
                        // The old rendering stays on screen, stretched, until
                        // the new one is ready, so resizing never flashes white.
                        retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                        // While a cleaned page is on its way, a quarter-size
                        // render is enough to show something at once.
                        sourceSize.width: Math.round(slot.renderWidth * Screen.devicePixelRatio / (root.cleanPages ? 4 : 1))
                        sourceSize.height: 0
                    }

                    // Saved highlights, then search results, then the
                    // selection being made, all in PDF points.
                    Repeater {
                        model: root.highlights[slot.index] || []
                        delegate: Shape {
                            required property var modelData
                            anchors.fill: parent
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeWidth: -1
                                fillColor: Qt.alpha(root.colors[modelData.color] || root.colors.yellow, 0.4)
                                scale: Qt.size(slot.pageScale, slot.pageScale)
                                PathMultiline { paths: modelData.paths }
                            }
                            // A highlight with a note gets a marker in the margin beside it.
                            Rectangle {
                                visible: modelData.hasNote
                                x: modelData.box.x * slot.pageScale - width - 4
                                y: modelData.box.y * slot.pageScale
                                width: 4
                                height: Math.max(8, modelData.box.height * slot.pageScale)
                                radius: 2
                                color: root.colors[modelData.color] || root.colors.yellow
                            }
                        }
                    }
                    Shape {
                        anchors.fill: parent
                        visible: root.searchModel !== null && image.status === Image.Ready
                        ShapePath {
                            strokeWidth: -1
                            fillColor: "#5564b0f4"
                            scale: Qt.size(slot.pageScale, slot.pageScale)
                            PathMultiline {
                                paths: root.searchModel && root.searchModel.count >= 0 && root.searchModel.searchString !== ""
                                       ? root.searchModel.boundingPolygonsOnPage(slot.index) : []
                            }
                        }
                    }
                    Shape {
                        anchors.fill: parent
                        visible: root.searchModel !== null && root.searchModel.currentPage === slot.index
                        ShapePath {
                            strokeWidth: 2
                            strokeColor: "#1a5fb4"
                            fillColor: "transparent"
                            scale: Qt.size(slot.pageScale, slot.pageScale)
                            PathMultiline { paths: root.searchModel ? root.searchModel.currentResultBoundingPolygons : [] }
                        }
                    }
                    Shape {
                        anchors.fill: parent
                        ShapePath {
                            strokeWidth: -1
                            fillColor: "#6064b0f4"
                            scale: Qt.size(slot.pageScale, slot.pageScale)
                            PathMultiline { paths: slot.chosenPaths }
                        }
                    }

                    HoverHandler { cursorShape: Qt.IBeamCursor }
                    DragHandler {
                        id: drag
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        // Only while the button is down: the release itself
                        // moves the centroid. A drag that starts in a margin
                        // moves onto the nearest text beside it, and one that
                        // wanders into blank space keeps its last end on text.
                        property point start
                        property bool startFound: false
                        onCentroidChanged: if (active && centroid.pressedButtons !== Qt.NoButton) {
                            const scale = slot.pageScale
                            const to = Qt.point(centroid.position.x / scale, centroid.position.y / scale)
                            if (!startFound) {
                                const press = Qt.point(centroid.pressPosition.x / scale, centroid.pressPosition.y / scale)
                                const found = slot.nearestStart(press, to)
                                if (!found)
                                    return
                                start = found
                                startFound = true
                            }
                            slot.choose(pdfGeometry.select(root.path, slot.index, start, to, "drag"))
                        }
                        onActiveChanged: {
                            if (active) {
                                startFound = false
                                // One selection at a time, across pages.
                                if (root.activeSelection && root.activeSelection !== slot)
                                    root.activeSelection.clearChosen()
                                root.activeSelection = slot
                                slot.clearChosen()
                                root.selectionCleared()
                            } else if (slot.chosenText !== "") {
                                slot.report()
                            }
                        }
                    }
                    TapHandler {
                        id: tap
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: (eventPoint, button) => {
                            const p = eventPoint.position
                            const pt = Qt.point(p.x / slot.pageScale, p.y / slot.pageScale)
                            // Double click: the word. Triple click: the sentence.
                            // Clicks are counted here, close together in time
                            // and place, which is steadier than tapCount.
                            const now = Date.now()
                            const close = now - root.lastTapTime < 450 && Math.abs(p.x - root.lastTapPoint.x) < 8
                                       && Math.abs(p.y - root.lastTapPoint.y) < 8 && root.lastTapSlot === slot.index
                            root.tapRun = close ? root.tapRun + 1 : 1
                            root.lastTapTime = now
                            root.lastTapPoint = p
                            root.lastTapSlot = slot.index
                            const clicks = root.tapRun
                            if (clicks >= 2) {
                                if (root.activeSelection && root.activeSelection !== slot)
                                    root.activeSelection.clearChosen()
                                root.activeSelection = slot
                                slot.clearChosen()
                                slot.choose(pdfGeometry.select(root.path, slot.index, pt, pt, clicks >= 3 ? "sentence" : "word"))
                                if (slot.chosenText !== "")
                                    slot.report()
                                return
                            }
                            root.clearSelection()
                            root.selectionCleared()
                            for (const h of root.highlights[slot.index] || []) {
                                const b = h.box
                                if (pt.x >= b.x - 2 && pt.x <= b.x + b.width + 2 && pt.y >= b.y - 2 && pt.y <= b.y + b.height + 2
                                        && h.paths.some(path => slot.pathContains(path, pt))) {
                                    root.highlightClicked(h.id, slot.boxInRoot(b))
                                    return
                                }
                            }
                        }
                    }
                }
            }

            // The selection on this page, in PDF points.
            property string chosenText: ""
            property var chosenPaths: []
            function choose(result) {
                if (!result || !result.text)
                    return   // off the text: keep what was chosen
                chosenText = result.text
                chosenPaths = toPaths(result.rects)
            }
            function clearChosen() {
                chosenText = ""
                chosenPaths = []
            }
            function report() {
                root.textSelected(slot.index, chosenText, chosenPaths, slot.boxInRoot(slot.bounds(chosenPaths)))
            }
            // Points beside a press that lands in a margin, toward the drag
            // first, until one is on text.
            function nearestStart(from, to) {
                const toward = to.x >= from.x ? 1 : -1
                for (let d = 0; d <= 90; d += 3) {
                    for (const [dx, dy] of [[d * toward, 0], [-d * toward, 0], [0, d / 2], [0, -d / 2]]) {
                        const p = Qt.point(from.x + dx, from.y + dy)
                        if (pdfGeometry.select(root.path, slot.index, p, p, "word").text)
                            return p
                    }
                }
                return null
            }

            // Flat corner lists, as stored, into point lists to draw.
            function toPaths(rects) {
                return rects.map(flat => {
                    const points = []
                    for (let k = 0; k + 1 < flat.length; k += 2)
                        points.push(Qt.point(flat[k], flat[k + 1]))
                    return points
                })
            }
            function bounds(paths) {
                let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
                for (const path of paths)
                    for (const p of path) {
                        x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y)
                        x1 = Math.max(x1, p.x); y1 = Math.max(y1, p.y)
                    }
                return x0 === Infinity ? Qt.rect(0, 0, 0, 0) : Qt.rect(x0, y0, x1 - x0, y1 - y0)
            }
            function pathContains(path, pt) {
                // Even–odd ray casting; highlight polygons are simple.
                let inside = false
                for (let i = 0, j = path.length - 1; i < path.length; j = i++) {
                    const a = path[i], b = path[j]
                    if ((a.y > pt.y) !== (b.y > pt.y) && pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x)
                        inside = !inside
                }
                return inside
            }
            // A box in PDF points, in this view's pixels.
            function boxInRoot(b) {
                const p = paper.mapToItem(root, b.x * pageScale, b.y * pageScale)
                return Qt.rect(p.x, p.y, b.width * pageScale, b.height * pageScale)
            }
        }
    }
}
