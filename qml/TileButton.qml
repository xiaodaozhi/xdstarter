pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

Button {
    id: tileButton

    required property var tile
    required property var tokens
    property bool editMode: false
    property bool readOnly: false
    property bool dragging: false
    property int iconRevision: 0
    property var iconVersions: ({})
    readonly property bool applicationTile: tile.targetType !== undefined && tile.targetType !== "prototype"
    readonly property bool targetAvailable: tile.available !== false
    readonly property string iconSource: tile.iconSource || ""
    readonly property string paletteIdentity: tile.targetId || tile.id || ""
    readonly property int tokenPaletteIndex: paletteIndex(paletteIdentity)
    readonly property color baseColor: tokens.tilePalette[tokenPaletteIndex]
    readonly property color fillTopColor: down ? Qt.darker(baseColor, 1.10) : Qt.lighter(baseColor, hovered ? 1.16 : 1.05)
    readonly property color fillBottomColor: down ? Qt.darker(baseColor, 1.16) : (hovered ? Qt.lighter(baseColor, 1.06) : Qt.darker(baseColor, 1.04))
    readonly property int iconSizeDip: tile.small ? 28 : 36

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

    objectName: "tile-" + tile.id
    x: tile.x
    y: tile.y
    width: tile.width
    height: tile.height
    padding: 0
    opacity: dragging ? 0.35 : 1
    focusPolicy: Qt.StrongFocus
    Accessible.name: tile.name
    Accessible.description: !targetAvailable ? qsTr("%1，目标不可用。%2。Shift 加 F10 取消固定。").arg(tile.dimensions).arg(tile.unavailableReason || qsTr("应用已移除")) : (editMode ? qsTr("%1，编辑模式。按 Ctrl 加方向键移动一格，Shift 加 F10 打开磁贴菜单。").arg(tile.dimensions) : (applicationTile ? qsTr("%1，按 Enter 启动。Shift 加 F10 打开磁贴菜单。").arg(tile.dimensions) : qsTr("%1，仅供布局预览。Shift 加 F10 打开磁贴菜单。").arg(tile.dimensions)))

    function paletteIndex(identity: string): int {
        // Bounded identity hashing keeps one application's color stable through
        // renaming, resizing and moving, without claiming a brand association.
        let hash = 5381;
        const count = Math.min(identity.length, 256);
        for (let index = 0; index < count; ++index)
            hash = ((hash * 33) ^ identity.charCodeAt(index)) >>> 0;
        return hash % tokens.tilePalette.length;
    }

    function activate(): void {
        if (applicationTile && !editMode)
            activated(tile.id);
        else
            selected(tile.name);
    }

    onClicked: activate()
    onActiveFocusChanged: {
        if (activeFocus)
            focused(tileButton);
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            const position = tileButton.mapToItem(null, width / 2, height / 2);
            contextRequested(tile.id, tile.size, position.x, position.y);
            event.accepted = true;
            return;
        }
        let direction = "";
        let dx = 0;
        let dy = 0;
        switch (event.key) {
        case Qt.Key_Left:
            direction = "left";
            dx = -1;
            break;
        case Qt.Key_Right:
            direction = "right";
            dx = 1;
            break;
        case Qt.Key_Up:
            direction = "up";
            dy = -1;
            break;
        case Qt.Key_Down:
            direction = "down";
            dy = 1;
            break;
        default:
            return;
        }
        if (editMode && !readOnly && (event.modifiers & Qt.ControlModifier))
            moveBy(tile.id, dx, dy);
        else
            navigate(tile.id, direction);
        event.accepted = true;
    }
    Keys.onReturnPressed: activate()
    Keys.onEnterPressed: activate()

    ToolTip {
        id: tileNameTip

        objectName: "tileToolTip"
        width: Math.min(380, implicitWidth)
        parent: tileButton
        visible: !tileButton.dragging && (tileButton.hovered || tileButton.activeFocus)
        text: tileButton.tile.name + " · " + tileButton.tile.dimensions + (tileButton.targetAvailable ? "" : "\n" + (tileButton.tile.unavailableReason || qsTr("应用不可用；可右键取消固定。")))
        timeout: -1
        contentItem: Text {
            text: tileNameTip.text
            textFormat: Text.PlainText
            color: tileNameTip.palette.toolTipText
            font: tileNameTip.font
            wrapMode: Text.Wrap
        }
    }

    background: Rectangle {
        radius: tileButton.tokens.cornerRadiusDip.tile
        gradient: Gradient {
            GradientStop { position: 0; color: tileButton.fillTopColor }
            GradientStop { position: 1; color: tileButton.fillBottomColor }
        }
        border.width: tileButton.activeFocus ? 2 : 1
        border.color: tileButton.activeFocus ? tileButton.tokens.brand.cyan : Qt.rgba(1, 1, 1, tileButton.hovered ? 0.30 : 0.16)
    }

    contentItem: Item {
        Text {
            objectName: "tileSymbol"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: tileButton.tile.small ? 0 : -20
            width: tileButton.iconSizeDip
            height: tileButton.iconSizeDip
            text: tileButton.tile.symbol
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            clip: true
            visible: appIcon.status !== Image.Ready
            color: "#FFFFFF"
            font.pixelSize: tileButton.tile.small ? 22 : 26
            font.bold: true
        }

        Image {
            id: appIcon

            objectName: "tileIcon"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: tileButton.tile.small ? 0 : -20
            width: tileButton.iconSizeDip
            height: tileButton.iconSizeDip
            source: tileButton.iconSource.length > 0 ? tileButton.iconSource + "?g=" + tileButton.iconRevision + "&v=" + (tileButton.iconVersions[tileButton.tile.targetId] || 0) : ""
            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            visible: status === Image.Ready
            opacity: tileButton.targetAvailable ? 1 : 0.45
        }

        Rectangle {
            objectName: "unavailableMarker"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: 18
            height: 18
            radius: 9
            visible: !tileButton.targetAvailable
            color: tileButton.tokens.status.error

            Text {
                anchors.centerIn: parent
                text: "!"
                color: tileButton.tokens.brand.ink
                font.bold: true
            }
        }

        Text {
            objectName: "tileLabel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            visible: !tileButton.tile.small
            text: tileButton.tile.name
            textFormat: Text.PlainText
            color: "#FFFFFF"
            font.pixelSize: tileButton.tokens.tileLabelFontDip
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: editPointer

        property point pressPosition: Qt.point(0, 0)
        property bool suppressClick: false

        anchors.fill: parent
        acceptedButtons: Qt.RightButton | (tileButton.editMode && !tileButton.readOnly ? Qt.LeftButton : Qt.NoButton)
        preventStealing: tileButton.editMode
        cursorShape: tileButton.dragging ? Qt.ClosedHandCursor : (tileButton.editMode && !tileButton.readOnly ? Qt.OpenHandCursor : Qt.ArrowCursor)
        onPressed: mouse => {
            tileButton.forceActiveFocus(Qt.MouseFocusReason);
            pressPosition = Qt.point(mouse.x, mouse.y);
            suppressClick = false;
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton))
                return;
            const position = tileButton.mapToItem(null, mouse.x, mouse.y);
            const dx = mouse.x - pressPosition.x;
            const dy = mouse.y - pressPosition.y;
            if (!suppressClick && dx * dx + dy * dy >= editPointer.drag.threshold * editPointer.drag.threshold) {
                suppressClick = true;
                tileButton.dragStarted(tileButton.tile.id, position.x, position.y, pressPosition.x, pressPosition.y, tileButton.width, tileButton.height);
            } else if (tileButton.dragging) {
                tileButton.dragMoved(position.x, position.y);
            }
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return;
            if (tileButton.dragging) {
                const position = tileButton.mapToItem(null, mouse.x, mouse.y);
                tileButton.dragMoved(position.x, position.y);
                tileButton.dragFinished();
            } else if (!suppressClick) {
                tileButton.activate();
            }
        }
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                const position = tileButton.mapToItem(null, mouse.x, mouse.y);
                tileButton.contextRequested(tileButton.tile.id, tileButton.tile.size, position.x, position.y);
            }
        }
        onCanceled: {
            if (tileButton.dragging)
                tileButton.dragCanceled();
        }
    }
}
