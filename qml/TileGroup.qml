pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: tileGroup

    required property var group
    required property var tokens
    required property color titleColor
    property bool darkTheme: true
    readonly property var theme: tokens.themes[darkTheme ? "dark" : "light"]
    readonly property real occupiedGridHeight: {
        let occupied = 0;
        for (let index = 0; index < group.tiles.length; ++index) {
            const tile = group.tiles[index];
            occupied = Math.max(occupied, tile.y + tile.height);
        }
        return occupied;
    }
    property bool editMode: false
    property bool readOnly: false
    property int iconRevision: 0
    property var iconVersions: ({})
    property string draggedTileId: ""
    property alias gridItem: grid
    property alias actionsItem: groupActions

    signal selected(string name)
    signal activated(string tileId)
    signal focused(Item item)
    signal navigate(string tileId, string direction)
    signal moveBy(string tileId, int dx, int dy)
    signal contextRequested(string tileId, string size, real sceneX, real sceneY)
    signal dragStarted(string tileId, real sceneX, real sceneY, real offsetX, real offsetY, real tileWidth, real tileHeight)
    signal dragMoved(real sceneX, real sceneY)
    signal dragFinished
    signal dragCanceled
    signal actionsRequested(string groupId, Item source)
    signal actionsFocused(string groupId)

    objectName: "group-" + group.id
    width: group.width
    height: groupHeader.height + 8 + grid.height

    function focusTile(tileId: string): bool {
        for (let index = 0; index < tileRepeater.count; ++index) {
            const button = tileRepeater.itemAt(index) as TileButton;
            if (button && button.tile.id === tileId) {
                button.forceActiveFocus(Qt.OtherFocusReason);
                return true;
            }
        }
        return false;
    }

    function focusActions(): void {
        groupActions.forceActiveFocus(Qt.OtherFocusReason);
    }

    Item {
        id: groupHeader

        anchors.left: parent.left
        anchors.right: parent.right
        height: tileGroup.tokens.layout.headerHeight

        Label {
            id: groupLabel

            objectName: "groupTitle-" + tileGroup.group.id
            anchors.left: parent.left
            anchors.right: groupActions.visible ? groupActions.left : parent.right
            anchors.rightMargin: groupActions.visible ? 8 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: tileGroup.group.name
            textFormat: Text.PlainText
            color: tileGroup.titleColor
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Accessible.name: text

            HoverHandler {
                id: titleHover
            }

            ToolTip {
                id: titleToolTip

                width: Math.min(380, implicitWidth)
                visible: titleHover.hovered && groupLabel.truncated
                text: tileGroup.group.name
                contentItem: Text {
                    text: tileGroup.group.name
                    textFormat: Text.PlainText
                    color: titleToolTip.palette.toolTipText
                    wrapMode: Text.Wrap
                }
            }
        }

        ToolButton {
            id: groupActions

            objectName: "groupActions-" + tileGroup.group.id
            anchors.right: parent.right
            width: 36
            height: 32
            visible: tileGroup.editMode
            enabled: !tileGroup.readOnly
            text: "⋯"
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("管理分组 %1").arg(tileGroup.group.name)
            Accessible.description: qsTr("重命名、调整顺序或删除空分组。Enter 或 Shift+F10 打开。")

            background: Rectangle {
                radius: 6
                color: groupActions.down ? tileGroup.theme.pressed : (groupActions.hovered ? tileGroup.theme.hover : "transparent")
                border.width: groupActions.activeFocus ? 2 : 0
                border.color: tileGroup.tokens.brand.cyan
            }

            contentItem: Text {
                text: groupActions.text
                color: tileGroup.titleColor
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: groupActions.enabled ? 1 : 0.45
            }

            ToolTip {
                id: actionsToolTip

                width: Math.min(380, implicitWidth)
                visible: groupActions.hovered || groupActions.activeFocus
                text: groupActions.Accessible.name
                contentItem: Text {
                    text: actionsToolTip.text
                    textFormat: Text.PlainText
                    color: actionsToolTip.palette.toolTipText
                    font: actionsToolTip.font
                    wrapMode: Text.Wrap
                }
            }
            onActiveFocusChanged: {
                if (activeFocus)
                    tileGroup.actionsFocused(tileGroup.group.id);
            }
            onClicked: tileGroup.actionsRequested(tileGroup.group.id, groupActions)
            Keys.onReturnPressed: tileGroup.actionsRequested(tileGroup.group.id, groupActions)
            Keys.onEnterPressed: tileGroup.actionsRequested(tileGroup.group.id, groupActions)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
                    tileGroup.actionsRequested(tileGroup.group.id, groupActions);
                    event.accepted = true;
                }
            }
        }
    }

    Item {
        id: grid

        objectName: "grid-" + tileGroup.group.id
        anchors.top: groupHeader.bottom
        anchors.topMargin: 8
        width: tileGroup.group.width
        // Browsing shows the occupied extent. Editing restores the full shared
        // drop grid; the individual tiles retain the same coordinates and sizes.
        height: tileGroup.editMode ? tileGroup.group.height : (tileGroup.group.tiles.length === 0 ? 56 : tileGroup.occupiedGridHeight)

        Label {
            objectName: "emptyGroupHint"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 0
            anchors.topMargin: 8
            width: Math.min(360, parent.width)
            visible: tileGroup.group.tiles.length === 0
            text: tileGroup.editMode ? qsTr("拖入磁贴，或从“所有应用”固定到此分组。") : qsTr("此分组还没有磁贴。可从“所有应用”固定应用。")
            wrapMode: Text.WordWrap
            color: tileGroup.theme.secondary
            font.pixelSize: 13
        }

        Repeater {
            id: tileRepeater

            model: tileGroup.group.tiles

            delegate: TileButton {
                required property var modelData

                tile: modelData
                tokens: tileGroup.tokens
                editMode: tileGroup.editMode
                readOnly: tileGroup.readOnly
                iconRevision: tileGroup.iconRevision
                iconVersions: tileGroup.iconVersions
                dragging: tile.id === tileGroup.draggedTileId
                onSelected: name => tileGroup.selected(name)
                onActivated: tileId => tileGroup.activated(tileId)
                onFocused: item => tileGroup.focused(item)
                onNavigate: (tileId, direction) => tileGroup.navigate(tileId, direction)
                onMoveBy: (tileId, dx, dy) => tileGroup.moveBy(tileId, dx, dy)
                onContextRequested: (tileId, size, sceneX, sceneY) => tileGroup.contextRequested(tileId, size, sceneX, sceneY)
                onDragStarted: (tileId, sceneX, sceneY, offsetX, offsetY, tileWidth, tileHeight) => tileGroup.dragStarted(tileId, sceneX, sceneY, offsetX, offsetY, tileWidth, tileHeight)
                onDragMoved: (sceneX, sceneY) => tileGroup.dragMoved(sceneX, sceneY)
                onDragFinished: tileGroup.dragFinished()
                onDragCanceled: tileGroup.dragCanceled()
            }
        }
    }
}
