// qml/MenuGlyph.qml

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Item {
    id: glyph

    required property var paths
    property string name: ""
    property color color: "black"
    implicitWidth: 16
    implicitHeight: 16
    readonly property int status: image.status
    Image {
        id: image
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        source: glyph.name.length > 0 && glyph.paths[glyph.name] ? "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="' + glyph.color.toString() + '" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' + glyph.paths[glyph.name] + '</g></svg>') : ""
    }
}
