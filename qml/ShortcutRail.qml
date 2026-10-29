pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: rail

    required property bool darkTheme
    required property var tokens
    property var themeOverride: null
    property bool highContrast: false
    property bool keyboardNavigation: false
    property bool hintsEnabled: true
    property bool busy: false
    readonly property string assetRoot: "qrc:/xdstarter/ui/" + (darkTheme ? "dark" : "light") + "/"
    readonly property var theme: themeOverride || tokens.themes[darkTheme ? "dark" : "light"]

    signal actionRequested(string actionId)

    objectName: "shortcutRail"
    implicitWidth: 64
    // The common menu chrome paints and clips this edge's background once.
    color: "transparent"

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: rail.theme.separator
    }

    component RailButton: Button {
        id: button

        required property string actionId
        required property string actionName
        property string compactName: actionName
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
            color: button.down ? rail.theme.pressed : (button.hovered ? rail.theme.hover : "transparent")
            border.width: (button.activeFocus && rail.keyboardNavigation) || (rail.highContrast && button.down) ? 2 : rail.highContrast && button.hovered ? 1 : 0
            border.color: rail.theme.focusAccent
        }

        contentItem: Item {
            opacity: button.enabled || rail.highContrast ? 1 : 0.45

            Image {
                objectName: button.avatar ? "shortcutAvatar" : "shortcutIcon-" + button.actionId
                anchors.centerIn: parent
                width: button.avatar ? 32 : 24
                height: width
                source: rail.assetRoot + (button.avatar ? "avatar" : button.actionId) + ".svg"
                sourceSize.width: width * Screen.devicePixelRatio
                sourceSize.height: height * Screen.devicePixelRatio
                fillMode: Image.PreserveAspectFit
                visible: !rail.highContrast
            }

            Text {
                objectName: "shortcutHighContrastText-" + button.actionId
                anchors.centerIn: parent
                width: parent.width - 4
                visible: rail.highContrast
                text: button.compactName
                textFormat: Text.PlainText
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: button.enabled ? rail.theme.foreground : rail.highContrast ? rail.theme.disabled : rail.theme.secondary
                Accessible.ignored: true
            }
        }

        Hint {
            contrastTheme: rail.highContrast ? rail.theme : null
            visible: rail.hintsEnabled && button.enabled && (button.hovered || (rail.keyboardNavigation && button.activeFocus))
            text: button.actionName
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 0

        RailButton {
            actionId: "account"
            actionName: qsTr("账户设置")
            compactName: qsTr("账户")
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
            compactName: qsTr("电源")
        }
    }
}
