pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs as FileDialogs
import XDStarter

QtObject {
    id: picker

    property Window parentWindow
    property string title: qsTr("选择布局文件")
    readonly property int options: 0x10 // Qt FileDialog.ReadOnly
    readonly property int modality: Qt.WindowModal
    readonly property bool visible: nativePicker.supported ? nativePicker.visible : fallback.visible
    readonly property double currentProcessId: nativePicker.currentProcessId
    readonly property url selectedFile: nativePicker.supported ? nativePicker.selectedFile : fallback.selectedFile
    signal accepted()
    signal rejected()
    signal failed()

    function open(): void {
        if (nativePicker.supported)
            nativePicker.open();
        else
            fallback.open();
    }
    function close(): void {
        if (nativePicker.supported)
            nativePicker.close();
        else
            fallback.close();
    }
    function reject(): void { close(); }

    property NativeLayoutPicker nativePicker: NativeLayoutPicker {
        parentWindow: picker.parentWindow
        title: picker.title
        jsonFilter: qsTr("JSON 布局")
        allFilter: qsTr("所有文件")
        onAccepted: picker.accepted()
        onRejected: picker.rejected()
        onFailed: picker.failed()
    }
    property FileDialogs.FileDialog fallback: FileDialogs.FileDialog {
        title: picker.title
        parentWindow: picker.parentWindow
        modality: Qt.WindowModal
        fileMode: FileDialogs.FileDialog.OpenFile
        // Qt 6.8.2 qmltypes omit this public FileDialogOptions flag.
        // qmllint disable missing-type missing-property
        options: FileDialogs.FileDialog.ReadOnly
        // qmllint enable missing-type missing-property
        nameFilters: [qsTr("JSON 布局 (*.json)"), qsTr("所有文件 (*)")]
        onAccepted: picker.accepted()
        onRejected: picker.rejected()
    }
}
