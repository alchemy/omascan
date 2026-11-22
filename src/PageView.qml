import QtQuick

// One scanned page as it will be exported: turned, on white paper with
// signed's shadow, at the window's scale.
Item {
    id: page

    property url source
    property int turn: 0
    property real pageWidth: 595     // points, unturned
    property real pageHeight: 842
    property real viewScale: 1
    signal pressed(var mouse)

    readonly property bool sideways: turn % 180 !== 0
    width: Math.round((sideways ? pageHeight : pageWidth) * viewScale)
    height: Math.round((sideways ? pageWidth : pageHeight) * viewScale)

    // Decoded only as large as it is shown, and again only once a zoom
    // settles, so dragging the zoom doesn't queue a decode per frame.
    property int renderWidth: 0
    function updateRenderWidth() {
        renderWidth = Math.ceil(pageWidth * viewScale * Screen.devicePixelRatio / 64) * 64;
    }
    onViewScaleChanged: renderTimer.restart()
    onPageWidthChanged: renderTimer.restart()
    Component.onCompleted: updateRenderWidth()
    Timer { id: renderTimer; interval: 150; onTriggered: page.updateRenderWidth() }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 2
        anchors.bottomMargin: -6
        color: "#60000000"
        radius: Math.min(2, theme.radius)
    }
    Rectangle { anchors.fill: parent; color: "white" }

    // The page unturned, turned about its middle.
    Item {
        anchors.centerIn: parent
        width: page.sideways ? page.height : page.width
        height: page.sideways ? page.width : page.height
        rotation: page.turn

        // The last decode stays up while a sharper one loads behind it, but
        // never one of another page.
        Image {
            id: shown
            anchors.fill: parent
            cache: true
            smooth: true
        }
        Image {
            id: loader
            visible: false
            asynchronous: true
            cache: true
            source: page.renderWidth > 0 ? page.source : ""
            sourceSize.width: page.renderWidth
            onStatusChanged: if (status === Image.Ready) {
                shown.sourceSize.width = sourceSize.width;
                shown.source = source;
            }
        }
    }
    onSourceChanged: shown.source = ""

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => page.pressed(mouse)
    }
}
