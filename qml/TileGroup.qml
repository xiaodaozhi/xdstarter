pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: tileGroup

    required property var group
    required property var tokens
    required property color titleColor
    property bool editMode: false
    property bool readOnly: false
    property int iconRevision: 0
    property var iconVersions: ({})
    property string draggedTileId: ""
    property alias gridItem: grid

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

    objectName: "group-" + group.id
    width: group.width
    height: groupLabel.implicitHeight + 8 + group.height

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

    Label {
        id: groupLabel

        anchors.left: parent.left
        anchors.right: parent.right
        text: tileGroup.group.name
        color: tileGroup.titleColor
        font.pixelSize: 18
        font.bold: true
        Accessible.name: text
    }

    Item {
        id: grid

        objectName: "grid-" + tileGroup.group.id
        anchors.top: groupLabel.bottom
        anchors.topMargin: 8
        width: tileGroup.group.width
        height: tileGroup.group.height

        Label {
            objectName: "emptyGroupHint"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            width: Math.min(360, parent.width - 24)
            visible: tileGroup.group.tiles.length === 0
            text: qsTr("此分组还没有磁贴。请在“所有应用”中选择应用并固定到这里。")
            wrapMode: Text.WordWrap
            color: tileGroup.titleColor
            opacity: 0.7
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
