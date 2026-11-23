import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// A list to pick from under a top-bar button, drawn like signed's signature
// menu. The arrow keys and Enter work as well as the mouse. Each option is
// { value, label, detail, key }: detail is a second, dimmer line and key the
// shortcut that does the same thing. The current value gets a check mark.
Popup {
    id: menu

    property var options: []
    property var current: undefined
    property string footer: ""      // one action after the options, in the accent
    property string footerKey: ""
    property int highlighted: 0
    property real minimumWidth: 200
    signal picked(var value)
    signal footerClicked()

    readonly property int total: options.length + (footer !== "" ? 1 : 0)

    width: Math.max(minimumWidth, measure.width + 16)
    padding: 8
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    onOpened: {
        const index = options.findIndex(option => option.value === current);
        highlighted = Math.max(0, index);
        content.forceActiveFocus();
    }

    function activate(index) {
        if (index < options.length)
            picked(options[index].value);
        else if (footer !== "")
            footerClicked();
        close();
    }

    // The widest option, so the menu fits its labels.
    FontMetrics { id: labelMetrics; font.pixelSize: 13 }
    FontMetrics { id: detailMetrics; font.pixelSize: 11 }
    readonly property var measure: {
        let width = 0;
        for (const option of options) {
            width = Math.max(width, labelMetrics.advanceWidth(option.label + (option.key ? "   " + option.key : "")));
            if (option.detail)
                width = Math.max(width, detailMetrics.advanceWidth(option.detail));
        }
        if (footer !== "")
            width = Math.max(width, labelMetrics.advanceWidth(footer + "   " + footerKey));
        return { width: width + 64 };
    }

    background: Rectangle {
        color: "#1c1c1e"
        radius: theme.radius
        border.color: "#3a3a3c"
    }

    contentItem: FocusScope {
        id: content
        implicitHeight: column.implicitHeight
        focus: true

        Keys.onUpPressed: menu.highlighted = Math.max(0, menu.highlighted - 1)
        Keys.onDownPressed: menu.highlighted = Math.min(menu.total - 1, menu.highlighted + 1)
        Keys.onReturnPressed: menu.activate(menu.highlighted)
        Keys.onEnterPressed: menu.activate(menu.highlighted)
        Keys.onSpacePressed: menu.activate(menu.highlighted)

        Column {
            id: column
            width: parent.width
            spacing: 2

            Repeater {
                model: menu.options
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var modelData
                    readonly property bool chosen: menu.current !== undefined && modelData.value === menu.current

                    width: column.width
                    height: modelData.detail ? 48 : 34
                    radius: Math.min(8, theme.radius)
                    color: menu.highlighted === index ? "#2c2c2f" : "transparent"

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: if (hovered) menu.highlighted = row.index
                    }
                    TapHandler { onTapped: menu.activate(row.index) }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: trailing.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Label {
                            width: parent.width
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: row.chosen ? theme.accent : "white"
                            font.pixelSize: 13
                            font.weight: row.chosen ? Font.DemiBold : Font.Normal
                        }
                        Label {
                            visible: !!row.modelData.detail
                            width: parent.width
                            text: row.modelData.detail || ""
                            elide: Text.ElideRight
                            color: "#8e8e93"
                            font.pixelSize: 11
                        }
                    }

                    Item {
                        id: trailing
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(check.visible ? check.width : 0, key.visible ? key.implicitWidth : 0)
                        height: 16

                        Shape {
                            id: check
                            visible: row.chosen
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 14
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                scale: Qt.size(14 / 24, 14 / 24)
                                strokeColor: theme.accent
                                strokeWidth: 2.6
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                PathSvg { path: "M5 12 L10 17 L19 7" }
                            }
                        }
                        Label {
                            id: key
                            visible: !row.chosen && !!row.modelData.key
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.key || ""
                            color: "#7a7a80"
                            font.pixelSize: 11
                            font.family: "monospace"
                        }
                    }
                }
            }

            Rectangle {
                visible: menu.footer !== "" && menu.options.length > 0
                width: parent.width - 8
                x: 4
                height: 1
                color: "#3a3a3c"
            }

            Rectangle {
                visible: menu.footer !== ""
                width: parent.width
                height: 36
                radius: Math.min(8, theme.radius)
                color: menu.highlighted === menu.options.length ? "#2c2c2f" : "transparent"
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: if (hovered) menu.highlighted = menu.options.length
                }
                TapHandler { onTapped: menu.activate(menu.options.length) }
                Label {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: menu.footer
                    color: theme.accent
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: menu.footerKey
                    color: "#7a7a80"
                    font.pixelSize: 11
                    font.family: "monospace"
                }
            }
        }
    }
}
