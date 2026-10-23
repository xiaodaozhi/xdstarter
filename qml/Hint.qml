// qml/Hint.qml

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ToolTip {
    id: hint

    delay: 600
    timeout: -1
    padding: 10
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: 13
    width: Math.min(380, implicitWidth)

    contentItem: Text {
        text: hint.text
        textFormat: Text.PlainText
        font: hint.font
        color: hint.palette.toolTipText
        wrapMode: Text.Wrap
    }
    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            anchors.topMargin: 1
            anchors.bottomMargin: -5
            radius: 10
            color: "#14000000"
        }
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: hint.palette.toolTipBase
            border.width: 1
            border.color: hint.palette.mid
        }
    }
}
