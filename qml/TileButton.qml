pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

Button {
    id: tileButton

    required property var tile
    required property var tokens
    property var motion: ({})
    property bool keyboardNavigation: false
    property bool hintsEnabled: true
    property bool animationsEnabled: true
    property bool editMode: false
    property bool readOnly: false
    property bool dragging: false
    property int iconRevision: 0
    property var iconVersions: ({})
    readonly property bool pointerHovered: tileHover.hovered
    property bool pointerPressActive: false
    property bool pressComponentReady: false
    property point pressPoint: Qt.point(0, 0)
    property real pressDirectionX: 0
    property real pressDirectionY: 0
    property real pressProgress: 0
    readonly property bool pressAnimating: pressMotion.running
    readonly property var pressTokens: tokens.motion.tilePress
    readonly property matrix4x4 pressMatrix: projectedPressMatrix()
    readonly property bool visualPressed: down || pointerPressActive
    HoverHandler {
        id: tileHover
    }
    readonly property bool applicationTile: tile.targetType !== undefined && tile.targetType !== "prototype"
    readonly property bool targetAvailable: tile.available !== false
    readonly property string iconSource: tile.iconSource || ""
    readonly property string paletteIdentity: tile.targetId || tile.id || ""
    readonly property int tokenPaletteIndex: paletteIndex(paletteIdentity)
    readonly property color baseColor: tokens.tilePalette[tokenPaletteIndex]
    readonly property color fillTopColor: visualPressed ? Qt.darker(baseColor, 1.10) : Qt.lighter(baseColor, pointerHovered ? 1.16 : 1.05)
    readonly property color fillBottomColor: visualPressed ? Qt.darker(baseColor, 1.16) : (pointerHovered ? Qt.lighter(baseColor, 1.06) : Qt.darker(baseColor, 1.04))
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
    horizontalPadding: 0
    readonly property real targetOpacity: dragging ? 0.35 : 1
    opacity: targetOpacity
    // Input geometry stays on the untransformed Button. Only its visual layers
    // receive the projective press matrix, independently of pin/reflow motion.
    ParallelAnimation {
        id: pressMotion
        property int durationMs: 0
        NumberAnimation {
            id: pressProgressMotion
            target: tileButton
            property: "pressProgress"
            duration: pressMotion.durationMs
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: pressDirectionXMotion
            target: tileButton
            property: "pressDirectionX"
            duration: pressMotion.durationMs
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: pressDirectionYMotion
            target: tileButton
            property: "pressDirectionY"
            duration: pressMotion.durationMs
            easing.type: Easing.OutCubic
        }
    }
    Behavior on opacity {
        enabled: tileButton.animationsEnabled
        onEnabledChanged: {
            if (!enabled)
                tileButton.opacity = Qt.binding(function () { return tileButton.targetOpacity; });
        }
        NumberAnimation {
            duration: 140
        }
    }
    ParallelAnimation {
        id: reflowMotion
        running: tileButton.motion.hasPrevious === true
        onFinished: {
            if (tileButton.activeFocus)
                Qt.callLater(function () {
                    if (tileButton.activeFocus)
                        tileButton.focused(tileButton);
                });
        }
        NumberAnimation {
            target: tileButton
            property: "x"
            from: tileButton.motion.motionFrom_x ?? tileButton.tile.x
            to: tileButton.tile.x
            duration: tileButton.animationsEnabled ? 200 : 0
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: tileButton
            property: "y"
            from: tileButton.motion.motionFrom_y ?? tileButton.tile.y
            to: tileButton.tile.y
            duration: tileButton.animationsEnabled ? 200 : 0
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: tileButton
            property: "width"
            from: tileButton.motion.motionFrom_width ?? tileButton.tile.width
            to: tileButton.tile.width
            duration: tileButton.animationsEnabled ? 200 : 0
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: tileButton
            property: "height"
            from: tileButton.motion.motionFrom_height ?? tileButton.tile.height
            to: tileButton.tile.height
            duration: tileButton.animationsEnabled ? 200 : 0
            easing.type: Easing.OutCubic
        }
    }
    property real appearScale: 1
    transform: Scale {
        origin.x: tileButton.width / 2
        origin.y: tileButton.height / 2
        xScale: tileButton.appearScale
        yScale: tileButton.appearScale
    }
    NumberAnimation {
        id: pinAppearMotion
        target: tileButton
        property: "appearScale"
        from: 0.9
        to: 1
        duration: 180
        easing.type: Easing.OutCubic
    }
    Component.onCompleted: {
        pressComponentReady = true;
        if (animationsEnabled && motion.hasPrevious === false)
            pinAppearMotion.start();
    }
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
        if (applicationTile)
            activated(tile.id);
        else
            selected(tile.name);
    }

    function beginPointerPress(x: real, y: real): void {
        pressMotion.stop();
        pressPoint = Qt.point(Math.max(-1, Math.min(1, 2 * x / Math.max(1, width) - 1)), Math.max(-1, Math.min(1, 2 * y / Math.max(1, height) - 1)));
        if (pressProgress <= 0) {
            pressDirectionX = pressPoint.x;
            pressDirectionY = pressPoint.y;
        }
        pointerPressActive = true;
        if (animationsEnabled)
            animatePointerPress(1, pressTokens.pressMs);
    }

    function animatePointerPress(targetProgress: real, durationMs: int): void {
        pressProgressMotion.from = pressProgress;
        pressProgressMotion.to = targetProgress;
        pressDirectionXMotion.from = pressDirectionX;
        pressDirectionXMotion.to = targetProgress > 0 ? pressPoint.x : pressDirectionX;
        pressDirectionYMotion.from = pressDirectionY;
        pressDirectionYMotion.to = targetProgress > 0 ? pressPoint.y : pressDirectionY;
        pressMotion.durationMs = durationMs;
        pressMotion.start();
    }

    function cancelPointerPress(immediate: bool): void {
        editPointer.leftClickPending = false;
        pointerPressActive = false;
        pressMotion.stop();
        if (immediate) {
            pressProgress = 0;
            pressDirectionX = 0;
            pressDirectionY = 0;
            return;
        }
        if (pressProgress === 0)
            return;
        animatePointerPress(0, pressTokens.releaseMs);
    }

    function projectedPressMatrix(): matrix4x4 {
        let matrix = Qt.matrix4x4();
        if (pressProgress <= 0)
            return matrix;
        const centerX = width / 2;
        const centerY = height / 2;
        const maximumAngle = pressTokens.maximumTiltDegrees * Math.PI / 180;
        const directionLength = Math.sqrt(pressDirectionX * pressDirectionX + pressDirectionY * pressDirectionY);
        const strength = Math.min(1, directionLength);
        const directionX = directionLength > 0 ? pressDirectionX / directionLength : 0;
        const directionY = directionLength > 0 ? pressDirectionY / directionLength : 0;
        const centerWeight = 1 - strength;
        const visualScale = 1 - (1 - pressTokens.pressedScale) * centerWeight * pressProgress;
        const centerDepth = pressTokens.centerDepthDip * centerWeight * pressProgress;
        // One axis preserves diagonal symmetry. Bound its full plane span,
        // rather than adding independently capped X/Y rotations and depths.
        const span = (Math.abs(directionX) * width + Math.abs(directionY) * height) * visualScale;
        const depthAngle = Math.asin(Math.min(1, pressTokens.maximumEdgeDepthDip / Math.max(1, span)));
        const angle = Math.min(maximumAngle, depthAngle) * strength * pressProgress;
        const pivotX = pressDirectionX === 0 ? 0 : (pressDirectionX > 0 ? -centerX : centerX) * visualScale;
        const pivotY = pressDirectionY === 0 ? 0 : (pressDirectionY > 0 ? -centerY : centerY) * visualScale;
        const distance = pressTokens.perspectiveDistanceDip;
        matrix.translate(Qt.vector3d(centerX, centerY, 0));
        matrix = matrix.times(Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -1 / distance, 1));
        // Keep the opposite edge/corner anchored; center-only depression and
        // scaling fade out as the press approaches the tile's boundary.
        matrix.translate(Qt.vector3d(pivotX, pivotY, -centerDepth));
        if (angle > 0)
            matrix.rotate(angle * 180 / Math.PI, Qt.vector3d(-directionY, directionX, 0));
        matrix.translate(Qt.vector3d(-pivotX, -pivotY, 0));
        matrix.scale(visualScale, visualScale, 1);
        matrix.translate(Qt.vector3d(-centerX, -centerY, 0));
        return matrix;
    }

    function requestContextMenu(): void {
        const position = tileButton.mapToItem(null, width / 2, height / 2);
        contextRequested(tile.id, tile.size, position.x, position.y);
    }

    onClicked: activate()
    onDraggingChanged: {
        if (dragging)
            cancelPointerPress(false);
    }
    onAnimationsEnabledChanged: {
        if (animationsEnabled || !pressComponentReady)
            return;
        pressMotion.stop();
        pressProgress = 0;
        pressDirectionX = 0;
        pressDirectionY = 0;
        reflowMotion.complete();
        pinAppearMotion.complete();
    }
    onEnabledChanged: {
        if (tileButton.pressComponentReady && !tileButton.enabled)
            cancelPointerPress(false);
    }
    onVisibleChanged: {
        if (tileButton.pressComponentReady && !tileButton.visible)
            cancelPointerPress(true);
    }
    Connections {
        target: tileButton.Window.window
        function onVisibleChanged(): void {
            if (tileButton.pressComponentReady && tileButton.Window.window && !tileButton.Window.window.visible)
                tileButton.cancelPointerPress(true);
        }
    }
    onActiveFocusChanged: {
        if (activeFocus)
            focused(tileButton);
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu) {
            requestContextMenu();
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

    Hint {
        id: tileNameTip
        delay: 600
        font.family: tileButton.font.family

        objectName: "tileToolTip"
        width: Math.min(380, implicitWidth)
        parent: tileButton
        visible: tileButton.hintsEnabled && !tileButton.pointerPressActive && !tileButton.dragging && (tileButton.pointerHovered || (tileButton.keyboardNavigation && tileButton.activeFocus))
        text: tileButton.tile.name + "\n" + tileButton.tile.dimensions + (tileButton.targetAvailable ? "" : "\n" + (tileButton.tile.unavailableReason || qsTr("应用不可用；可右键取消固定。")))
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
        objectName: "tilePressBackground"
        transform: Matrix4x4 {
            matrix: tileButton.pressMatrix
        }
        radius: tileButton.tokens.cornerRadiusDip.tile
        gradient: Gradient {
            GradientStop {
                position: 0
                color: tileButton.fillTopColor
            }
            GradientStop {
                position: 1
                color: tileButton.fillBottomColor
            }
        }
        border.width: (tileButton.activeFocus && tileButton.keyboardNavigation) ? 2 : 1
        border.color: (tileButton.activeFocus && tileButton.keyboardNavigation) ? tileButton.tokens.brand.cyan : Qt.lighter(tileButton.baseColor, tileButton.pointerHovered ? 1.36 : 1.12)
    }

    contentItem: Item {
        objectName: "tilePressContent"
        transform: Matrix4x4 {
            matrix: tileButton.pressMatrix
        }
        Text {
            font.family: tileButton.font.family
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
            // Eliding already bounds the symbol. A nested clip under the
            // projective matrix drops glyphs on the Direct3D render path.
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
                font.family: tileButton.font.family
                anchors.centerIn: parent
                text: "!"
                color: tileButton.tokens.brand.ink
                font.bold: true
            }
        }

        Text {
            font.family: tileButton.font.family
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
        property bool leftClickPending: false
        property bool dragEligible: false

        objectName: "tileEditPointer"
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.LeftButton
        preventStealing: tileButton.editMode && !tileButton.readOnly
        cursorShape: tileButton.dragging ? Qt.ClosedHandCursor : (tileButton.editMode && !tileButton.readOnly ? Qt.OpenHandCursor : Qt.ArrowCursor)
        onPressed: mouse => {
            tileButton.forceActiveFocus(Qt.MouseFocusReason);
            pressPosition = Qt.point(mouse.x, mouse.y);
            suppressClick = false;
            dragEligible = mouse.button === Qt.LeftButton && tileButton.editMode && !tileButton.readOnly;
            if (mouse.button === Qt.LeftButton) {
                leftClickPending = true;
                tileButton.beginPointerPress(mouse.x, mouse.y);
            } else {
                tileButton.cancelPointerPress(false);
            }
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton) || !dragEligible || !tileButton.editMode || tileButton.readOnly)
                return;
            const position = tileButton.mapToItem(null, mouse.x, mouse.y);
            const dx = mouse.x - pressPosition.x;
            const dy = mouse.y - pressPosition.y;
            if (!suppressClick && dx * dx + dy * dy >= editPointer.drag.threshold * editPointer.drag.threshold) {
                suppressClick = true;
                tileButton.cancelPointerPress(false);
                tileButton.dragStarted(tileButton.tile.id, position.x, position.y, pressPosition.x, pressPosition.y, tileButton.width, tileButton.height);
            } else if (tileButton.dragging) {
                tileButton.dragMoved(position.x, position.y);
            }
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return;
            const activatePending = leftClickPending;
            tileButton.cancelPointerPress(false);
            if (tileButton.dragging) {
                const position = tileButton.mapToItem(null, mouse.x, mouse.y);
                tileButton.dragMoved(position.x, position.y);
                tileButton.dragFinished();
            } else if (activatePending && !suppressClick && mouse.x >= 0 && mouse.x < width && mouse.y >= 0 && mouse.y < height) {
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
            tileButton.cancelPointerPress(!tileButton.visible);
            if (tileButton.dragging)
                tileButton.dragCanceled();
        }
    }
}
