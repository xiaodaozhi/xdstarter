// qml/FloatingSurface.qml

pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: surface

    property color color: "white"
    property color borderColor: "#D1DAE6"
    property real radius: 8
    property bool darkTheme: false
    property bool shadowEnabled: true
    property real elevation: 1
    readonly property real shadowInset: 32 * elevation

    function roundedPath(ctx: var, x: real, y: real, w: real, h: real, r: real): void {
        ctx.beginPath();
        ctx.moveTo(x + r, y);
        ctx.lineTo(x + w - r, y);
        ctx.arcTo(x + w, y, x + w, y + r, r);
        ctx.lineTo(x + w, y + h - r);
        ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
        ctx.lineTo(x + r, y + h);
        ctx.arcTo(x, y + h, x, y + h - r, r);
        ctx.lineTo(x, y + r);
        ctx.arcTo(x, y, x + r, y, r);
        ctx.closePath();
    }

    Canvas {
        id: shadow
        antialiasing: true
        renderTarget: Canvas.Image
        objectName: "floatingSurfaceShadow"
        visible: surface.shadowEnabled
        anchors.fill: parent
        anchors.margins: -surface.shadowInset
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: surface
            function onDarkThemeChanged(): void { shadow.requestPaint(); }
            function onRadiusChanged(): void { shadow.requestPaint(); }
            function onElevationChanged(): void { shadow.requestPaint(); }
        }
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const inset = surface.shadowInset;
            for (let spread = Math.ceil(inset); spread > 0; --spread) {
                const alpha = (surface.darkTheme ? 0.022 : 0.012) * Math.exp(-0.5 * Math.pow(spread / (8 * surface.elevation), 2));
                ctx.fillStyle = Qt.rgba(7 / 255, 17 / 255, 30 / 255, alpha);
                surface.roundedPath(ctx, inset - spread, inset + 4 * surface.elevation - spread, surface.width + 2 * spread, surface.height + 2 * spread, surface.radius + spread);
                ctx.fill();
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: surface.radius
        color: surface.color
        border.width: 1
        border.color: surface.borderColor
    }
}
