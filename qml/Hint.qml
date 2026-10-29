// qml/Hint.qml

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ToolTip {
    id: hint

    property var contrastTheme: null
    readonly property color foregroundColor: contrastTheme ? contrastTheme.foreground : palette.toolTipText
    delay: 600
    timeout: -1
    padding: 7
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: 13
    width: Math.min(380, implicitWidth)

    contentItem: Text {
        text: hint.text
        textFormat: Text.PlainText
        font: hint.font
        color: hint.foregroundColor
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: 4
        color: hint.contrastTheme ? hint.contrastTheme.opaqueFallback : hint.palette.toolTipBase
        border.width: 1
        border.color: hint.contrastTheme ? hint.contrastTheme.foreground : hint.palette.mid
    }
}
