// qml/MenuChrome.qml

pragma ComponentBehavior: Bound

import QtQuick

Canvas {
    id: chrome

    required property color color
    required property color railColor
    required property color borderColor
    required property real radius
    required property real shadowInset
    required property real borderWidth
    property real railWidth: 64

    objectName: "menuSurface"
    antialiasing: true
    renderTarget: Canvas.Image

    function roundedPath(context: var, x: real, y: real, w: real, h: real, r: real): void {
        context.beginPath();
        context.moveTo(x + r, y);
        context.lineTo(x + w - r, y);
        context.arcTo(x + w, y, x + w, y + r, r);
        context.lineTo(x + w, y + h - r);
        context.arcTo(x + w, y + h, x + w - r, y + h, r);
        context.lineTo(x + r, y + h);
        context.arcTo(x, y + h, x, y + h - r, r);
        context.lineTo(x, y + r);
        context.arcTo(x, y, x + r, y, r);
        context.closePath();
    }

    onPaint: {
        const context = getContext("2d");
        context.reset();
        context.clearRect(0, 0, width, height);
        const inset = shadowInset;
        const panelWidth = width - 2 * inset;
        const panelHeight = height - 2 * inset;

        // Nested translucent fills form a soft shadow on both the RHI and software
        // renderers. It is painted once per size/theme change, without a live effect.
        for (let spread = inset - 1; spread > 0; --spread) {
            const strength = 0.012 * Math.pow(1 - spread / inset, 1.2);
            context.fillStyle = Qt.rgba(0, 0, 0, strength);
            roundedPath(context, inset - spread, inset - spread,
                        panelWidth + 2 * spread, panelHeight + 2 * spread, radius + spread);
            context.fill();
        }

        roundedPath(context, inset, inset, panelWidth, panelHeight, radius);
        context.fillStyle = color;
        context.fill();
        // The rail touches the panel edge. Clip its fill to the exact same path,
        // so its square background cannot produce a second pair of corner edges.
        context.save();
        context.clip();
        context.fillStyle = railColor;
        context.fillRect(inset, inset, railWidth, panelHeight);
        context.restore();

        const halfBorder = borderWidth / 2;
        roundedPath(context, inset + halfBorder, inset + halfBorder,
                    panelWidth - borderWidth, panelHeight - borderWidth, radius - halfBorder);
        context.lineWidth = borderWidth;
        context.strokeStyle = borderColor;
        context.stroke();
    }

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onColorChanged: requestPaint()
    onRailColorChanged: requestPaint()
    onBorderColorChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onShadowInsetChanged: requestPaint()
    onBorderWidthChanged: requestPaint()
    onRailWidthChanged: requestPaint()
}
