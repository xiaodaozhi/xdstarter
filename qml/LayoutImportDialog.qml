// qml/LayoutImportDialog.qml

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XDStarter

Dialog {
    id: dialog

    required property MenuController controller
    required property var theme
    required property var tokens
    required property real panelInset
    required property real panelWidth
    required property real panelHeight
    readonly property var preview: controller.importPreview
    readonly property bool hasPreview: Object.keys(preview).length > 0

    objectName: "layoutImportDialog"
    popupType: Popup.Item
    modal: true
    focus: true
    closePolicy: controller.importCommitting ? Popup.NoAutoClose : Popup.CloseOnEscape
    width: Math.min(480, panelWidth - 40)
    padding: 20
    x: (parent.width - width) / 2
    y: Math.max(panelInset, (parent.height - height) / 2)
    title: preview.backup === true ? qsTr("恢复上一份布局") : qsTr("导入布局")
    onClosed: controller.cancelLayoutImport()

    component ImportButton: Button {
        id: button

        implicitHeight: dialog.tokens.layout.controlSize
        leftPadding: 14
        rightPadding: 14
        focusPolicy: Qt.StrongFocus
        opacity: enabled ? 1 : 0.45
        contentItem: Text {
            text: button.text
            textFormat: Text.PlainText
            color: dialog.theme.foreground
            font: button.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            objectName: button.objectName + "Background"
            radius: 6
            color: button.down ? dialog.theme.pressed : button.hovered ? dialog.theme.hover : dialog.theme.surface
            border.width: (button.activeFocus && dialog.controller.keyboardNavigation) ? 2 : 1
            border.color: (button.activeFocus && dialog.controller.keyboardNavigation) ? dialog.tokens.brand.blue : dialog.theme.controlBorder
        }
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
    }

    Overlay.modal: Item {
        Rectangle {
            x: dialog.panelInset
            y: dialog.panelInset
            width: dialog.panelWidth
            height: dialog.panelHeight
            radius: dialog.tokens.cornerRadiusDip.menu
            color: Qt.rgba(0, 0, 0, 0.4)
        }
    }
    header: Label {
        text: dialog.title
        textFormat: Text.PlainText
        padding: 20
        bottomPadding: 12
        font.pixelSize: dialog.tokens.font.heading
        font.bold: true
        color: dialog.theme.foreground
    }
    background: Rectangle {
        radius: 8
        color: dialog.theme.surface
        border.width: 1
        border.color: dialog.theme.controlBorder
    }
    contentItem: ColumnLayout {
        spacing: 12

        Label {
            objectName: "layoutImportProgress"
            Layout.fillWidth: true
            visible: dialog.controller.importBusy
            text: dialog.controller.importCommitting ? qsTr("正在保存当前编辑并替换布局…") : qsTr("正在读取并校验所选文件…")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: dialog.theme.foreground
            Accessible.role: Accessible.StatusBar
        }
        Label {
            id: fileNameLabel

            objectName: "layoutImportFileName"
            Layout.fillWidth: true
            visible: dialog.hasPreview
            text: dialog.preview.fileName || ""
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            maximumLineCount: 1
            color: dialog.theme.foreground
            font.weight: Font.DemiBold
            Accessible.name: text
            Accessible.description: dialog.preview.source || ""
            HoverHandler {
                id: fileNameHover
            }
            Hint {
                id: sourceToolTip
                delay: 600
                font.family: dialog.font.family

                visible: fileNameHover.hovered
                text: dialog.preview.source || ""
                width: Math.min(380, implicitWidth)
                contentItem: Text {
                    text: sourceToolTip.text
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 8
                    elide: Text.ElideMiddle
                    color: sourceToolTip.palette.toolTipText
                    font: sourceToolTip.font
                }
            }
        }
        Label {
            objectName: "layoutImportSource"
            Layout.fillWidth: true
            visible: dialog.hasPreview
            text: dialog.preview.directory || ""
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideMiddle
            color: dialog.theme.secondary
        }
        Label {
            objectName: "layoutImportCounts"
            Layout.fillWidth: true
            visible: dialog.hasPreview
            text: qsTr("%1 个分组 · %2 枚磁贴 · %3 个应用目标\n主题：%4 · 格式版本：%5").arg(dialog.preview.groups || 0).arg(dialog.preview.tiles || 0).arg(dialog.preview.targets || 0).arg(dialog.preview.theme === "light" ? qsTr("浅色") : qsTr("深色")).arg(dialog.preview.sourceVersion === dialog.preview.currentVersion ? String(dialog.preview.currentVersion || 0) : (dialog.preview.sourceVersion || 0) + " → " + (dialog.preview.currentVersion || 0))
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: dialog.theme.foreground
        }
        Label {
            objectName: "layoutImportWarning"
            Layout.fillWidth: true
            visible: dialog.hasPreview
            text: qsTr("确认后将替换全部分组、磁贴和主题。先保存当前尚未保存的编辑，再保存预览中的布局；已有的有效配置将保留为备份。不会启动导入的应用。")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: dialog.theme.foreground
        }
        Label {
            objectName: "layoutImportError"
            Layout.fillWidth: true
            visible: dialog.controller.importError.length > 0
            text: dialog.controller.importError
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: dialog.theme.errorForeground
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            ImportButton {
                objectName: "layoutImportCancelButton"
                text: dialog.hasPreview ? qsTr("取消") : qsTr("关闭")
                enabled: !dialog.controller.importCommitting
                onClicked: dialog.close()
            }
            ImportButton {
                objectName: "layoutImportConfirmButton"
                text: qsTr("确认替换")
                enabled: dialog.hasPreview && !dialog.controller.importBusy && !dialog.controller.readOnly
                onClicked: dialog.controller.confirmLayoutImport()
            }
        }
    }
}
