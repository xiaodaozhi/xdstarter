pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: rail

    required property bool darkTheme
    required property var tokens
    property bool busy: false
    readonly property string assetRoot: "qrc:/xdstarter/ui/" + (darkTheme ? "dark" : "light") + "/"

    signal actionRequested(string actionId)

    objectName: "shortcutRail"
    implicitWidth: 64
    // The common menu chrome paints and clips this edge's background once.
    color: "transparent"

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Qt.alpha(rail.tokens.brand.blue, 0.15)
    }

    component RailButton: Button {
        id: button

        required property string actionId
        required property string actionName
        property bool avatar: false

        objectName: "shortcut-" + actionId
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 48
        Layout.preferredHeight: 48
        padding: 0
        enabled: !rail.busy || actionId === "power"
        focusPolicy: Qt.StrongFocus
        Accessible.name: actionName
        Accessible.description: actionId === "power" ? qsTr("打开电源与退出文字菜单。") : qsTr("打开%1。").arg(actionName)
        onClicked: rail.actionRequested(actionId)
        Keys.onReturnPressed: rail.actionRequested(actionId)
        Keys.onEnterPressed: rail.actionRequested(actionId)

        background: Rectangle {
            radius: 8
            color: button.hovered || button.down ? Qt.alpha(rail.tokens.brand.blue, button.down ? 0.25 : 0.14) : "transparent"
            border.width: button.activeFocus ? 2 : 0
            border.color: rail.tokens.brand.cyan
        }

        contentItem: Item {
            Image {
                objectName: button.avatar ? "shortcutAvatar" : "shortcutIcon-" + button.actionId
                anchors.centerIn: parent
                width: button.avatar ? 32 : 24
                height: width
                source: rail.assetRoot + (button.avatar ? "avatar" : button.actionId) + ".svg"
                sourceSize.width: width * Screen.devicePixelRatio
                sourceSize.height: height * Screen.devicePixelRatio
                fillMode: Image.PreserveAspectFit
            }
        }

        ToolTip.visible: hovered || activeFocus
        ToolTip.text: actionName
        ToolTip.delay: hovered ? 400 : 0
        ToolTip.timeout: -1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 8

        RailButton {
            actionId: "account"
            actionName: qsTr("账户设置")
            avatar: true
        }

        Item {
            Layout.fillHeight: true
        }

        RailButton {
            actionId: "documents"
            actionName: qsTr("文档")
        }

        RailButton {
            actionId: "downloads"
            actionName: qsTr("下载")
        }

        RailButton {
            actionId: "settings"
            actionName: qsTr("设置")
        }

        RailButton {
            actionId: "power"
            actionName: qsTr("电源与退出")
        }
    }
}
