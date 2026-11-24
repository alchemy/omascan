import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

// Laid out like signed, Omarchy's PDF signer, so the two feel like one
// family: a top bar of key-labelled buttons, page thumbnails down the left,
// every page in one scrolling column, and a status line along the bottom.
ApplicationWindow {
    id: win
    width: 1200
    height: 860
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "Omascan"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color accent: theme.accent
    readonly property color accentForeground: theme.accentForeground

    readonly property int count: backend.pages.count
    property int current: -1
    property real zoom: 1
    // Fit the widest page to the window, but no bigger than a comfortable
    // reading size on a wide screen.
    readonly property real fitScale: Math.max(0.1, Math.min(1.6, (pageList.width - 64) / backend.pages.widest))
    readonly property real viewScale: fitScale * zoom
    property bool helpVisible: false
    property bool quitting: false
    property string notice: ""
    // Set while the window scrolls to a page itself, so the scroll doesn't
    // pick a different current page on the way.
    property bool jumping: false

    readonly property bool menuOpen: deviceMenu.opened || sourceMenu.opened || modeMenu.opened
                                     || resolutionMenu.opened || paperMenu.opened || pageMenu.opened
    readonly property bool blocked: helpVisible || confirm.visible || menuOpen
    readonly property bool canEdit: count > 0 && !blocked && !backend.scanning
    readonly property bool canChange: !blocked && !backend.scanning
    // Narrow windows drop the key hints from the top bar first, then the
    // icons.
    readonly property bool compact: width < 1100
    readonly property bool tight: width < 860

    readonly property var icons: ({
        source: "M7 10 L7 4 L17 4 L17 10 M3 10 L21 10 L21 20 L3 20 Z",
        mode: "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M12 3 L12 21",
        resolution: "M4 4 L20 4 L20 20 L4 20 Z M4 12 L20 12 M12 4 L12 20",
        paper: "M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8",
        scan: "M4 8 L4 4 L8 4 M16 4 L20 4 L20 8 M20 16 L20 20 L16 20 M8 20 L4 20 L4 16 M3 12 L21 12",
        cancel: "M6 6 L18 18 M18 6 L6 18"
    })

    // A number in the language's separators and digits. It reads the language
    // so a binding that calls it re-renders when the language changes.
    function number(value) {
        translator.language;
        return Number(value).toLocaleString(Qt.locale(), 'f', 0);
    }
    function sourceName(value) { return qsTr(value); }
    function modeName(value) { return qsTr(value); }
    function paperName(id) {
        const size = backend.pageSizes.find(size => size.id === id);
        return size ? qsTr(size.name) : "—";
    }

    function select(index) {
        if (count === 0) {
            current = -1;
            return;
        }
        goToPage(Math.max(0, Math.min(count - 1, index)));
    }
    function goToPage(page) {
        if (page < 0 || page >= count)
            return;
        jumping = true;
        pageList.positionViewAtIndex(page, ListView.Contain);
        thumbs.positionViewAtIndex(page, ListView.Contain);
        current = page;
        jumping = false;
    }
    function updateCurrentPage() {
        if (jumping || count === 0)
            return;
        let index = pageList.indexAt(pageList.contentX + pageList.width / 2, pageList.contentY + pageList.height / 2);
        if (index < 0)
            index = pageList.indexAt(pageList.contentX + pageList.width / 2, pageList.contentY + pageList.height / 2 + 24);
        if (index >= 0 && index !== current) {
            current = index;
            thumbs.positionViewAtIndex(index, ListView.Contain);
        }
    }
    function setZoom(value) {
        zoom = Math.max(0.25, Math.min(4, value));
    }
    function movePage(step) {
        const to = current + step;
        if (current < 0 || to < 0 || to >= count)
            return;
        backend.pages.move(current, to);
        goToPage(to);
    }
    function rotate(degrees) {
        if (current >= 0)
            backend.pages.rotate(current, degrees);
    }
    function removePage() {
        if (current >= 0)
            backend.pages.remove(current);
    }
    function toggleScan() {
        if (backend.scanning)
            backend.cancelScan();
        else if (backend.ready)
            backend.scan();
    }
    function exportPdf() {
        if (count > 0 && !backend.scanning && !backend.exporting)
            backend.exportPdf();
    }
    // Throwing scanned pages away always asks first.
    function guard(title, action) {
        if (backend.dirty)
            confirm.ask(title, qsTr("The pages you scanned haven't been exported yet."), qsTr("Discard"), action);
        else
            action();
    }
    function newDocument() {
        if (backend.scanning)
            return;
        guard(qsTr("Start a new document?"), () => backend.newDocument());
    }
    function showNotice(text) {
        notice = text;
        noticeTimer.restart();
    }

    onClosing: (close) => {
        if (quitting || !backend.dirty) {
            if (backend.scanning)
                backend.cancelScan();
            return;
        }
        close.accepted = false;
        confirm.ask(qsTr("Quit without exporting?"), qsTr("The pages you scanned haven't been exported yet."), qsTr("Quit"), () => {
            quitting = true;
            win.close();
        });
    }

    Connections {
        target: backend
        function onPageAdded(row) { win.goToPage(row); }
        function onExported(path) { win.showNotice(qsTr("Saved %1").arg(path)); }
    }
    Connections {
        target: backend.pages
        function onCountChanged() { win.select(win.current < 0 ? 0 : win.current); }
        function onModelReset() { win.select(win.current); }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: win.notice = "" }

    // --- shortcuts ---
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !confirm.visible && !win.menuOpen; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.close() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; enabled: !confirm.visible; onActivated: win.close() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.toggleScan() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.blocked && backend.scanning; onActivated: backend.cancelScan() }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.exportPdf() }
    Shortcut { sequences: [StandardKey.New]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.newDocument() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.blocked && !backend.scanning; onActivated: backend.pages.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: !win.blocked && !backend.scanning; onActivated: backend.pages.redo() }
    // The pages run top to bottom, so up and down go through them; left and
    // right still work, as they did before the pages were stacked.
    Shortcut { sequences: ["Up", "PgUp", "Left"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.select(win.current - 1) }
    Shortcut { sequences: ["Down", "PgDown", "Right"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.select(win.current + 1) }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.select(0) }
    Shortcut { sequence: "End"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.select(win.count - 1) }
    Shortcut { sequences: ["Shift+Up", "Shift+Left"]; context: Qt.ApplicationShortcut; enabled: win.canEdit; onActivated: win.movePage(-1) }
    Shortcut { sequences: ["Shift+Down", "Shift+Right"]; context: Qt.ApplicationShortcut; enabled: win.canEdit; onActivated: win.movePage(1) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.rotate(90) }
    Shortcut { sequence: "Shift+R"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.rotate(-90) }
    Shortcut { sequences: ["Delete", "Backspace", "X"]; context: Qt.ApplicationShortcut; enabled: win.canEdit; onActivated: win.removePage() }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.cycleSource() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.cycleMode() }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.stepResolution(1) }
    Shortcut { sequence: "Shift+D"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.stepResolution(-1) }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.cyclePageSize() }
    Shortcut { sequence: "F5"; context: Qt.ApplicationShortcut; enabled: win.canChange; onActivated: backend.discover() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++", StandardKey.ZoomIn]; context: Qt.ApplicationShortcut; enabled: win.count > 0; onActivated: win.setZoom(win.zoom * 1.25) }
    Shortcut { sequences: ["Ctrl+-", StandardKey.ZoomOut]; context: Qt.ApplicationShortcut; enabled: win.count > 0; onActivated: win.setZoom(win.zoom / 1.25) }
    Shortcut { sequence: "Ctrl+0"; context: Qt.ApplicationShortcut; enabled: win.count > 0; onActivated: win.setZoom(1) }

    // --- top bar ---
    Rectangle {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: "#1c1c1e"

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "black" }

        // How far the page being scanned has come, along the bar's edge.
        Rectangle {
            anchors.bottom: parent.bottom
            height: 2
            width: parent.width * Math.max(0, Math.min(100, backend.progress)) / 100
            color: win.accent
            visible: backend.scanning && backend.progress > 0
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 12
            spacing: 8

            // The scanner, where signed shows the document's name. Click it
            // for the other scanners.
            Rectangle {
                id: deviceButton
                Layout.preferredWidth: deviceName.implicitWidth + chevron.width + (dirtyDot.visible ? dirtyDot.width + 8 : 0) + 32
                Layout.maximumWidth: topBar.width - tools.width - 48
                Layout.preferredHeight: 34
                radius: theme.radius
                color: (deviceHover.hovered && deviceButton.enabled) || deviceMenu.opened ? "#2c2c2f" : "transparent"
                enabled: !backend.scanning
                clip: true

                HoverHandler { id: deviceHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: deviceMenu.opened ? deviceMenu.close() : deviceMenu.open() }

                Row {
                    id: deviceRow
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Label {
                        id: deviceName
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, Math.min(implicitWidth, deviceButton.width - 32 - chevron.width - (dirtyDot.visible ? dirtyDot.width + 8 : 0)))
                        text: backend.discovering && backend.devices.length === 0 ? qsTr("Looking for scanners…")
                            : backend.deviceIndex < 0 ? qsTr("No scanner found")
                            : backend.devices[backend.deviceIndex].name
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Shape {
                        id: chevron
                        width: 10
                        height: 10
                        anchors.verticalCenter: parent.verticalCenter
                        ShapePath {
                            strokeColor: "#8e8e93"
                            strokeWidth: 1.6
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: "M1 3 L5 7 L9 3" }
                        }
                    }
                    Rectangle {
                        id: dirtyDot
                        visible: backend.dirty
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7
                        height: 7
                        radius: 3.5
                        color: win.accent
                    }
                }

                ChoiceMenu {
                    id: deviceMenu
                    y: deviceButton.height + 8
                    minimumWidth: 320
                    current: backend.deviceIndex
                    options: backend.devices.map((device, index) => ({ value: index, label: device.name, detail: device.address }))
                    footer: backend.discovering ? qsTr("Looking for scanners…") : qsTr("Find scanners again")
                    footerKey: "F5"
                    onPicked: (value) => backend.deviceIndex = value
                    onFooterClicked: if (!backend.discovering) backend.discover()
                }
            }

            Item { Layout.fillWidth: true }

            Row {
                id: tools
                spacing: 8

                BarButton {
                    id: sourceButton
                    text: backend.sources.length ? win.sourceName(backend.source) : qsTr("Source")
                    icon: win.tight ? "" : win.icons.source
                    key: win.compact ? "" : "S"
                    menu: true
                    active: sourceMenu.opened
                    accent: win.accent
                    enabled: backend.sources.length > 0 && !backend.scanning
                    onClicked: sourceMenu.opened ? sourceMenu.close() : sourceMenu.open()
                    ChoiceMenu {
                        id: sourceMenu
                        objectName: "sourceMenu"
                        y: sourceButton.height + 8
                        current: backend.source
                        options: backend.sources.map(value => ({ value: value, label: win.sourceName(value) }))
                        onPicked: (value) => backend.source = value
                    }
                }
                BarButton {
                    id: modeButton
                    text: backend.modes.length ? win.modeName(backend.mode) : qsTr("Color")
                    icon: win.tight ? "" : win.icons.mode
                    key: win.compact ? "" : "M"
                    menu: true
                    active: modeMenu.opened
                    accent: win.accent
                    enabled: backend.modes.length > 0 && !backend.scanning
                    onClicked: modeMenu.opened ? modeMenu.close() : modeMenu.open()
                    ChoiceMenu {
                        id: modeMenu
                        y: modeButton.height + 8
                        current: backend.mode
                        options: backend.modes.map(value => ({ value: value, label: win.modeName(value) }))
                        onPicked: (value) => backend.mode = value
                    }
                }
                BarButton {
                    id: resolutionButton
                    text: backend.resolutions.length ? qsTr("%1 dpi").arg(win.number(backend.resolution)) : qsTr("Resolution")
                    icon: win.tight ? "" : win.icons.resolution
                    key: win.compact ? "" : "D"
                    menu: true
                    active: resolutionMenu.opened
                    accent: win.accent
                    enabled: backend.resolutions.length > 0 && !backend.scanning
                    onClicked: resolutionMenu.opened ? resolutionMenu.close() : resolutionMenu.open()
                    ChoiceMenu {
                        id: resolutionMenu
                        y: resolutionButton.height + 8
                        minimumWidth: 160
                        current: backend.resolution
                        options: backend.resolutions.map(value => ({ value: value, label: qsTr("%1 dpi").arg(win.number(value)) }))
                        onPicked: (value) => backend.resolution = value
                    }
                }
                BarButton {
                    id: paperButton
                    text: win.paperName(backend.pageSize)
                    icon: win.tight ? "" : win.icons.paper
                    key: win.compact ? "" : "P"
                    menu: true
                    active: paperMenu.opened
                    accent: win.accent
                    enabled: !backend.scanning
                    onClicked: paperMenu.opened ? paperMenu.close() : paperMenu.open()
                    ChoiceMenu {
                        id: paperMenu
                        y: paperButton.height + 8
                        minimumWidth: 160
                        current: backend.pageSize
                        options: backend.pageSizes.map(size => ({ value: size.id, label: qsTr(size.name) }))
                        onPicked: (value) => backend.pageSize = value
                    }
                }
                Rectangle { width: 1; height: 22; color: "#3a3a3c"; anchors.verticalCenter: parent.verticalCenter }
                BarButton {
                    text: backend.scanning ? qsTr("Cancel") : qsTr("Scan")
                    icon: win.tight ? "" : backend.scanning ? win.icons.cancel : win.icons.scan
                    key: win.compact ? "" : backend.scanning ? "Esc" : "Space"
                    accent: win.accent
                    enabled: backend.ready || backend.scanning
                    onClicked: win.toggleScan()
                }
                BarButton {
                    text: qsTr("Export PDF…")
                    primary: true
                    accent: win.accent
                    accentForeground: win.accentForeground
                    enabled: win.count > 0 && !backend.scanning && !backend.exporting
                    onClicked: win.exportPdf()
                }
            }
        }
    }

    // --- pages ---
    Item {
        id: body
        anchors.top: topBar.bottom
        anchors.bottom: statusBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: win.count > 0

        ListView {
            id: thumbs
            width: win.count > 1 ? 132 : 0
            visible: win.count > 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            topMargin: 16
            bottomMargin: 16
            spacing: 10
            clip: true
            model: backend.pages
            boundsBehavior: Flickable.StopAtBounds

            Rectangle { anchors.fill: parent; color: "#161618"; z: -1 }

            delegate: Item {
                id: thumb
                required property int index
                required property url url
                required property int turn
                required property real pageWidth
                required property real pageHeight
                readonly property bool sideways: turn % 180 !== 0
                readonly property bool current: index === win.current
                width: thumbs.width
                height: sheet.height + 26

                Rectangle {
                    id: sheet
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 76
                    height: Math.round(76 * (thumb.sideways ? thumb.pageWidth / thumb.pageHeight : thumb.pageHeight / thumb.pageWidth))
                    color: "white"
                    opacity: thumb.current ? 1 : 0.85

                    Image {
                        anchors.centerIn: parent
                        width: thumb.sideways ? parent.height : parent.width
                        height: thumb.sideways ? parent.width : parent.height
                        rotation: thumb.turn
                        asynchronous: true
                        smooth: true
                        source: thumb.url
                        sourceSize.width: Math.round(152 * Screen.devicePixelRatio)
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -5
                        color: "transparent"
                        radius: Math.min(3, theme.radius)
                        border.width: 2.5
                        border.color: win.accent
                        visible: thumb.current
                    }
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: sheet.bottom
                    anchors.topMargin: 6
                    text: win.number(thumb.index + 1)
                    color: "#8e8e93"
                    font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        win.goToPage(thumb.index);
                        if (mouse.button === Qt.RightButton)
                            pageMenu.popup();
                    }
                }
            }
        }

        Rectangle { visible: thumbs.visible; x: thumbs.width; width: 1; height: parent.height; color: "black" }

        ListView {
            id: pageList
            anchors.left: thumbs.right
            anchors.leftMargin: thumbs.visible ? 1 : 0
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true
            spacing: 24
            topMargin: 28
            bottomMargin: 28
            cacheBuffer: Math.max(0, height)
            model: backend.pages
            contentWidth: Math.max(width, backend.pages.widest * win.viewScale + 64)
            flickableDirection: Flickable.AutoFlickIfNeeded
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            onContentYChanged: win.updateCurrentPage()
            onHeightChanged: win.updateCurrentPage()

            // ListView pins delegates to x = 0, so each page centers itself
            // inside a full-width row.
            delegate: Item {
                id: row
                required property int index
                required property url url
                required property int turn
                required property real pageWidth
                required property real pageHeight
                width: pageList.contentWidth
                height: sheet.height

                PageView {
                    id: sheet
                    source: row.url
                    turn: row.turn
                    pageWidth: row.pageWidth
                    pageHeight: row.pageHeight
                    viewScale: win.viewScale
                    x: Math.max(32, (row.width - width) / 2)
                    onPressed: (mouse) => {
                        win.goToPage(row.index);
                        if (mouse.button === Qt.RightButton)
                            pageMenu.popup();
                    }
                }
            }

            // Ctrl+scroll zooms, like everywhere else.
            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: (event) => win.setZoom(win.zoom * (event.angleDelta.y > 0 ? 1.1 : 1 / 1.1))
            }
        }

        // What can be done to the page under the mouse.
        ChoiceMenu {
            id: pageMenu
            minimumWidth: 220
            options: [
                { value: "right", label: qsTr("Rotate right"), key: "R" },
                { value: "left", label: qsTr("Rotate left"), key: "Shift+R" },
                { value: "earlier", label: qsTr("Move earlier"), key: "Shift+↑" },
                { value: "later", label: qsTr("Move later"), key: "Shift+↓" },
                { value: "delete", label: qsTr("Delete page"), key: "Delete" }
            ]
            function popup() {
                const point = body.mapFromItem(null, cursor.x, cursor.y);
                x = Math.min(point.x, body.width - width - 8);
                y = Math.min(point.y, body.height - height - 8);
                open();
            }
            onPicked: (value) => {
                if (backend.scanning && value !== "right" && value !== "left")
                    return;
                if (value === "right") win.rotate(90);
                else if (value === "left") win.rotate(-90);
                else if (value === "earlier") win.movePage(-1);
                else if (value === "later") win.movePage(1);
                else if (value === "delete") win.removePage();
            }
        }
        // Where the mouse is, for the page menu.
        HoverHandler { id: cursor; readonly property real x: point.scenePosition.x; readonly property real y: point.scenePosition.y }
    }

    // --- empty state ---
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 460)
        visible: win.count === 0
        spacing: 14

        readonly property bool noScanner: backend.devices.length === 0 && !backend.discovering

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            width: 120
            height: 150
            radius: theme.radius
            color: "transparent"
            border.color: "#3a3a3c"
            border.width: 2
            Shape {
                anchors.centerIn: parent
                width: 48
                height: 48
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    scale: Qt.size(2, 2)
                    strokeColor: backend.scanning ? win.accent : "#5a5a60"
                    strokeWidth: 1.5
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: "M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8 M4 13 L21 13" }
                }
            }
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: backend.scanning ? qsTr("Scanning…")
                : backend.devices.length === 0 && backend.discovering ? qsTr("Looking for scanners…")
                : parent.noScanner ? qsTr("No scanner found")
                : qsTr("Scan a page")
            color: "white"
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: text !== ""
            text: backend.error !== "" ? backend.error
                : backend.scanning || (backend.devices.length === 0 && backend.discovering) ? ""
                : parent.noScanner ? qsTr("Check that the scanner is on and on this network.")
                : backend.feeder ? qsTr("Load the pages into the document feeder.")
                : qsTr("Put the page face down on the glass.")
            color: backend.error !== "" ? "#ff8a80" : "#b8b8bc"
            font.pixelSize: 14
        }
        BarButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            visible: !(backend.devices.length === 0 && backend.discovering)
            text: backend.scanning ? qsTr("Cancel")
                : parent.noScanner ? qsTr("Find scanners again")
                : qsTr("Scan")
            key: backend.scanning ? "Esc" : parent.noScanner ? "F5" : "Space"
            primary: !backend.scanning
            accent: win.accent
            accentForeground: win.accentForeground
            enabled: backend.scanning || parent.noScanner || backend.ready
            onClicked: {
                if (parent.noScanner && !backend.scanning)
                    backend.discover();
                else
                    win.toggleScan();
            }
        }
    }

    // --- status line ---
    Rectangle {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 30
        color: "#1c1c1e"

        Rectangle { width: parent.width; height: 1; color: "black" }

        Label {
            id: pageStatus
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            visible: win.count > 0
            textFormat: Text.StyledText
            text: {
                let s = qsTr("Page %1 of %2").arg(win.number(win.current + 1)).arg(win.number(win.count));
                if (backend.dirty)
                    s += " · <font color=\"" + win.accent + "\">" + qsTr("not exported") + "</font>";
                if (win.zoom !== 1)
                    s += " · " + qsTr("%1%").arg(win.number(Math.round(win.zoom * 100)));
                return s;
            }
            color: "#b8b8bc"
            font.pixelSize: 12
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, statusBar.width - (pageStatus.visible ? pageStatus.implicitWidth : 0) - 90)
                elide: Text.ElideMiddle
                readonly property bool failed: backend.error !== "" && win.count > 0
                text: {
                    if (failed)
                        return backend.error;
                    if (backend.scanning) {
                        const progress = backend.progress > 0 ? " · " + qsTr("%1%").arg(win.number(Math.round(backend.progress))) : "";
                        return (backend.feeder ? qsTr("Scanning page %1…").arg(win.number(backend.scannedPages + 1)) : qsTr("Scanning…")) + progress;
                    }
                    if (backend.exporting)
                        return qsTr("Writing PDF…");
                    if (win.notice !== "")
                        return win.notice;
                    if (backend.discovering)
                        return qsTr("Looking for scanners…");
                    if (backend.loadingOptions)
                        return qsTr("Reading scanner settings…");
                    if (win.count > 0)
                        return qsTr("R to rotate · Shift+↑ ↓ to move · Delete to remove · right-click for more");
                    return "";
                }
                color: failed ? "#ff8a80" : win.notice !== "" && !backend.scanning ? win.accent : "#8e8e93"
                font.pixelSize: 12
                HoverHandler { id: errorHover; enabled: parent.failed; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: parent.failed; onTapped: backend.dismissError() }
                ToolTip.visible: errorHover.hovered
                ToolTip.text: qsTr("Click to dismiss")
            }
            Rectangle {
                width: 22
                height: 22
                radius: Math.min(5, theme.radius)
                color: helpHover.hovered ? "#2c2c2f" : "transparent"
                border.color: "#3a3a3c"
                Label { anchors.centerIn: parent; text: "?"; color: helpHover.hovered ? "white" : "#7a7a80"; font.pixelSize: 12 }
                HoverHandler { id: helpHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: win.helpVisible = !win.helpVisible }
            }
        }
    }

    // --- hotkeys ---
    Rectangle {
        visible: win.helpVisible
        anchors.fill: parent
        color: "#000000cc"

        MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

        Rectangle {
            anchors.centerIn: parent
            width: helpLayout.implicitWidth + 64
            height: helpLayout.implicitHeight + 56
            radius: theme.radius
            color: "#1c1c1e"

            ColumnLayout {
                id: helpLayout
                anchors.centerIn: parent
                spacing: 16

                Label { text: qsTr("Hotkeys"); color: "white"; font.pixelSize: 17; font.weight: Font.DemiBold }

                GridLayout {
                    columns: 4
                    columnSpacing: 16
                    rowSpacing: 9

                    Repeater {
                        model: [
                            ["Space", qsTr("Scan"), "Ctrl+S", qsTr("Export PDF")],
                            ["Esc", qsTr("Cancel the scan"), "Ctrl+N", qsTr("New document")],
                            ["S", qsTr("Next source"), "↑ / ↓", qsTr("Previous / next page")],
                            ["M", qsTr("Next color mode"), "Home / End", qsTr("First / last page")],
                            ["D / Shift+D", qsTr("Higher / lower resolution"), "Ctrl+= / − / 0", qsTr("Zoom in / out / fit")],
                            ["P", qsTr("Next paper size"), "Ctrl+Z", qsTr("Undo")],
                            ["F5", qsTr("Find scanners again"), "Ctrl+Shift+Z", qsTr("Redo")],
                            ["R / Shift+R", qsTr("Rotate right / left"), "Q", qsTr("Quit")],
                            ["Shift+↑ / ↓", qsTr("Move the page"), "?", qsTr("This list")],
                            ["Delete", qsTr("Delete the page"), "", ""]
                        ]
                        delegate: Repeater {
                            required property var modelData
                            model: 4
                            delegate: Label {
                                required property int index
                                Layout.alignment: index % 2 === 0 ? Qt.AlignRight : Qt.AlignLeft
                                Layout.leftMargin: index === 2 ? 24 : 0
                                text: modelData[index]
                                color: index % 2 === 0 ? win.accent : "#d6d6da"
                                font.pixelSize: 13
                                font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                            }
                        }
                    }
                }
            }
        }
    }

    Confirm {
        id: confirm
        accent: win.accent
        accentForeground: win.accentForeground
    }
}
