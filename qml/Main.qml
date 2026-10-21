pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import XDStarter

ApplicationWindow {
    id: window

    required property MenuController controller

    readonly property var tokens: controller.visualTokens
    readonly property var theme: tokens.themes[darkTheme ? "dark" : "light"]
    readonly property real shadowInset: tokens.menuChrome.shadowInsetDip
    readonly property real screenGapDip: tokens.menuChrome.screenGapDip
    readonly property real panelWidth: width - 2 * shadowInset
    readonly property real panelHeight: height - 2 * shadowInset
    property bool darkTheme: controller.darkTheme
    property string selectionStatus: ""
    property string integrationStatus: ""
    property bool winKeyCaptureAvailable: false
    property bool winKeyCaptureActive: false
    property bool winKeyCaptureBusy: false
    property string winKeyCaptureStatus: qsTr("Win 键唤起未启用；开始按钮保持原生行为。")
    readonly property bool compactLayout: panelWidth - shortcutRail.width - 40 < 260 + 20 + 552 + 16
    property bool showApplications: false
    property bool browseStateSaved: false
    property real savedApplicationsY: 0
    property int savedApplicationIndex: -1
    property real savedGroupsX: 0
    property real savedGroupsY: 0
    property string renderedSearchQuery: ""
    property string savedSearchResultId: ""
    property bool savedSearchResultFocus: false
    property int searchResultsRevision: 0
    property string pinApplicationId: ""
    property string pinApplicationName: ""
    property bool pinApplicationPinned: false
    property string focusedTileId: ""
    property string draggedTileId: ""
    readonly property bool dragActive: draggedTileId.length > 0
    property real dragPointerX: 0
    property real dragPointerY: 0
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    property real draggedWidth: 0
    property real draggedHeight: 0
    property string dragTargetGroupId: ""
    property int dragColumn: 0
    property int dragRow: 0
    property bool dragValid: false
    property string dragMessage: ""
    property real previewX: 0
    property real previewY: 0
    property real previewWidth: 0
    property real previewHeight: 0
    property string resizeTileId: ""
    property string resizeTileSize: ""
    property bool dismissalManaged: false
    property bool presentationSuspended: false
    readonly property bool internalPopupVisible: resizeMenu.visible || pinMenu.visible || powerMenu.visible || aboutPopup.visible
    readonly property bool dismissalBlocked: dragActive || internalPopupVisible

    signal dismissRequested
    signal winKeyCaptureToggleRequested

    objectName: "prototypeWindow"
    width: tokens.defaultMenuSizeDip.width + 2 * shadowInset
    height: tokens.defaultMenuSizeDip.height + 2 * shadowInset
    minimumWidth: 480 + 2 * shadowInset
    minimumHeight: 480 + 2 * shadowInset
    visible: true
    title: qsTr("XDStarter")
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint
    color: "transparent"

    background: MenuChrome {
        radius: window.tokens.cornerRadiusDip.menu
        color: window.theme.opaqueFallback
        railColor: Qt.darker(window.theme.opaqueFallback, window.darkTheme ? 1.12 : 1.035)
        borderWidth: window.tokens.menuChrome.borderWidthDip
        borderColor: window.theme.menuBorder
        shadowInset: window.shadowInset
    }

    palette.window: theme.opaqueFallback
    palette.windowText: theme.foreground
    palette.base: theme.opaqueFallback
    palette.text: theme.foreground
    palette.button: tokens.brand.ink
    palette.buttonText: tokens.themes.dark.foreground
    palette.highlight: tokens.brand.blue
    palette.highlightedText: tokens.themes.dark.foreground

    function selectPreview(name: string): void {
        selectionStatus = qsTr("已选择「%1」。这是布局预览，不会启动应用。").arg(name);
    }

    function activateApplication(applicationId: string): void {
        if (!controller.launchBusy && controller.launchApplication(applicationId))
            selectionStatus = qsTr("正在启动应用…");
    }

    function activateTile(tileId: string): void {
        if (!controller.launchBusy && !controller.editMode && controller.launchTile(tileId))
            selectionStatus = qsTr("正在启动应用…");
    }

    function focusApplicationAt(index: int): void {
        if (index < 0 || index >= applicationsList.count)
            return;
        applicationsList.currentIndex = index;
        applicationsList.positionViewAtIndex(index, ListView.Contain);
        Qt.callLater(function () {
            const row = applicationsList.itemAtIndex(index);
            if (row)
                row.focusApplication();
        });
    }

    function openPinMenu(applicationId: string, name: string, pinned: bool, source: Item): void {
        pinApplicationId = applicationId;
        pinApplicationName = name;
        pinApplicationPinned = pinned;
        const position = source.mapToItem(contentItem, 0, source.height);
        pinMenu.x = Math.max(shadowInset, Math.min(position.x, width - shadowInset - pinMenu.width));
        pinMenu.y = Math.max(shadowInset, Math.min(position.y, height - shadowInset - pinMenu.height));
        pinMenu.open();
    }

    function unpinCurrentTile(): void {
        resizeMenu.close();
        const tileId = resizeTileId;
        focusedTileId = "";
        if (!controller.unpinTile(tileId)) {
            focusedTileId = tileId;
        } else {
            Qt.callLater(function () {
                editLayoutButton.forceActiveFocus(Qt.OtherFocusReason);
            });
        }
    }

    function pinCurrentApplication(groupId: string): void {
        pinMenu.close();
        controller.pinApplication(pinApplicationId, groupId);
    }

    function ensureTileVisible(tileItem: Item): void {
        const position = tileItem.mapToItem(groupsColumn, 0, 0);
        let nextX = groupsFlickable.contentX;
        let nextY = groupsFlickable.contentY;
        if (position.x < nextX)
            nextX = position.x;
        else if (position.x + tileItem.width > nextX + groupsFlickable.width)
            nextX = position.x + tileItem.width - groupsFlickable.width;
        if (position.y < nextY)
            nextY = position.y;
        else if (position.y + tileItem.height > nextY + groupsFlickable.height)
            nextY = position.y + tileItem.height - groupsFlickable.height;
        groupsFlickable.contentX = Math.max(0, Math.min(nextX, groupsFlickable.contentWidth - groupsFlickable.width));
        groupsFlickable.contentY = Math.max(0, Math.min(nextY, groupsFlickable.contentHeight - groupsFlickable.height));
    }

    function navigateTile(tileId: string, direction: string): void {
        const targetId = controller.neighborTileId(tileId, direction);
        if (targetId.length > 0)
            focusTileById(targetId);
    }

    function focusTileById(tileId: string): bool {
        if (tileId.length === 0)
            return false;
        for (let index = 0; index < groupRepeater.count; ++index) {
            const group = groupRepeater.itemAt(index) as TileGroup;
            if (group && group.focusTile(tileId))
                return true;
        }
        return false;
    }

    function restoreTileFocus(): void {
        if (visible && !presentationSuspended && !internalPopupVisible && !dragActive && !controller.searchActive && !searchField.activeFocus)
            focusTileById(focusedTileId);
    }

    function focusSearch(): void {
        cancelDrag();
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
        searchField.selectAll();
    }

    function clearSearch(): void {
        Qt.inputMethod.reset();
        searchField.clear();
        controller.searchQuery = "";
        focusSearch();
    }

    function updateSearchPresentation(): void {
        if (controller.searchActive && !browseStateSaved) {
            savedApplicationsY = applicationsList.contentY;
            savedApplicationIndex = applicationsList.currentIndex;
            savedGroupsX = groupsFlickable.contentX;
            savedGroupsY = groupsFlickable.contentY;
            browseStateSaved = true;
            closeInternalPopups();
            cancelDrag();
        } else if (!controller.searchActive && browseStateSaved) {
            Qt.callLater(function () {
                if (!window.controller.searchActive) {
                    applicationsList.currentIndex = window.savedApplicationIndex;
                    applicationsList.contentY = window.savedApplicationsY;
                    groupsFlickable.contentX = window.savedGroupsX;
                    groupsFlickable.contentY = window.savedGroupsY;
                    window.browseStateSaved = false;
                }
            });
        }
    }

    function focusSearchResultAt(index: int): void {
        if (controller.searchBusy || searchResultsList.count === 0)
            return;
        if (index < 0) {
            searchField.forceActiveFocus(Qt.BacktabFocusReason);
            return;
        }
        const target = Math.min(index, searchResultsList.count - 1);
        searchResultsList.currentIndex = target;
        searchResultsList.positionViewAtIndex(target, ListView.Contain);
        Qt.callLater(function () {
            const row = searchResultsList.itemAtIndex(target);
            if (row)
                row.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    function searchResultsHaveFocus(): bool {
        let item = activeFocusItem;
        while (item) {
            if (item === searchResultsList)
                return true;
            item = item.parent;
        }
        return false;
    }

    function rememberSearchSelection(): void {
        ++searchResultsRevision;
        savedSearchResultId = "";
        savedSearchResultFocus = false;
        const index = searchResultsList.currentIndex;
        if (!controller.searchActive || renderedSearchQuery !== controller.searchQuery || index < 0 || index >= controller.searchResults.length)
            return;
        savedSearchResultId = controller.searchResults[index].appId;
        savedSearchResultFocus = searchResultsHaveFocus();
    }

    function restoreSearchSelection(): void {
        const revision = searchResultsRevision;
        const query = controller.searchQuery;
        const resultId = savedSearchResultId;
        const hadFocus = savedSearchResultFocus;
        renderedSearchQuery = query;
        savedSearchResultId = "";
        savedSearchResultFocus = false;
        Qt.callLater(function () {
            if (revision !== window.searchResultsRevision || query !== window.controller.searchQuery || !window.controller.searchActive)
                return;
            let index = -1;
            for (let candidate = 0; candidate < window.controller.searchResults.length; ++candidate) {
                if (window.controller.searchResults[candidate].appId === resultId) {
                    index = candidate;
                    break;
                }
            }
            if (index < 0 && searchResultsList.count > 0)
                index = 0;
            searchResultsList.currentIndex = index;
            if (index >= 0) {
                searchResultsList.positionViewAtIndex(index, ListView.Contain);
                searchResultsList.forceLayout();
            }
            const focusCanReturn = !window.activeFocusItem || window.activeFocusItem === window.contentItem || window.searchResultsHaveFocus();
            if (hadFocus && focusCanReturn && window.visible && !window.presentationSuspended && !window.internalPopupVisible && !searchField.inputMethodComposing) {
                if (index < 0)
                    searchField.forceActiveFocus(Qt.OtherFocusReason);
                else {
                    const row = searchResultsList.itemAtIndex(index);
                    if (row)
                        row.forceActiveFocus(Qt.OtherFocusReason);
                }
            }
        });
    }

    function activateSearchResultAt(index: int): void {
        if (searchField.inputMethodComposing || controller.searchBusy || index < 0 || index >= controller.searchResults.length)
            return;
        const result = controller.searchResults[index];
        if (result.available !== false && !controller.launchBusy && controller.launchSearchResult(result.appId))
            selectionStatus = qsTr("正在启动应用…");
    }

    function closeInternalPopups(): void {
        resizeMenu.close();
        pinMenu.close();
        powerMenu.close();
        aboutPopup.close();
    }

    function prepareForDismissal(): void {
        presentationSuspended = true;
        closeInternalPopups();
        cancelDrag();
    }

    function prepareForPresentation(): void {
        presentationSuspended = true;
        closeInternalPopups();
        cancelDrag();
        presentationSuspended = false;
        searchField.forceActiveFocus(Qt.OtherFocusReason);
    }

    function requestDismissal(): void {
        if (dismissalManaged)
            dismissRequested();
        else
            close();
    }

    function panelContains(sceneX: real, sceneY: real): bool {
        const x = sceneX - shadowInset;
        const y = sceneY - shadowInset;
        if (x < 0 || y < 0 || x >= panelWidth || y >= panelHeight)
            return false;
        const radius = tokens.cornerRadiusDip.menu;
        const centerX = Math.max(radius, Math.min(x, panelWidth - radius));
        const centerY = Math.max(radius, Math.min(y, panelHeight - radius));
        return Math.pow(x - centerX, 2) + Math.pow(y - centerY, 2) <= radius * radius;
    }

    function ensureActiveTileVisible(): void {
        const tileItem = activeFocusItem as TileButton;
        if (tileItem && !dragActive && !controller.searchActive)
            ensureTileVisible(tileItem);
    }

    function openResizeMenu(tileId: string, size: string, sceneX: real, sceneY: real): void {
        if (dragActive)
            return;
        resizeTileId = tileId;
        resizeTileSize = size;
        focusedTileId = tileId;
        const position = contentItem.mapFromItem(null, sceneX, sceneY);
        resizeMenu.x = Math.max(shadowInset, Math.min(position.x, width - shadowInset - resizeMenu.width));
        resizeMenu.y = Math.max(shadowInset, Math.min(position.y, height - shadowInset - resizeMenu.height));
        resizeMenu.open();
    }

    function resizeCurrentTile(size: string): void {
        const previousSize = resizeTileSize;
        const accepted = controller.resizeTile(resizeTileId, size);
        // Refresh check marks even when a conflicting resize keeps the same model value.
        resizeTileSize = "";
        resizeTileSize = accepted ? size : previousSize;
        Qt.callLater(restoreTileFocus);
    }

    function startDrag(tileId: string, sceneX: real, sceneY: real, offsetX: real, offsetY: real, tileWidth: real, tileHeight: real): void {
        if (!controller.editMode || controller.readOnly)
            return;
        draggedTileId = tileId;
        focusedTileId = tileId;
        dragOffsetX = offsetX;
        dragOffsetY = offsetY;
        draggedWidth = tileWidth;
        draggedHeight = tileHeight;
        updateDrag(sceneX, sceneY);
    }

    function updateDrag(sceneX: real, sceneY: real): void {
        if (!dragActive)
            return;
        dragPointerX = sceneX;
        dragPointerY = sceneY;
        dragValid = false;
        dragTargetGroupId = "";
        dragMessage = qsTr("请将磁贴拖到分组网格内；Esc 取消。");
        const pointer = contentItem.mapFromItem(null, sceneX, sceneY);
        previewX = pointer.x - dragOffsetX;
        previewY = pointer.y - dragOffsetY;
        previewWidth = draggedWidth;
        previewHeight = draggedHeight;
        for (let index = 0; index < groupRepeater.count; ++index) {
            const group = groupRepeater.itemAt(index) as TileGroup;
            if (!group)
                continue;
            const position = group.gridItem.mapFromItem(null, sceneX, sceneY);
            if (position.x < 0 || position.y < 0 || position.x >= group.gridItem.width || position.y >= group.gridItem.height)
                continue;
            dragTargetGroupId = group.group.id;
            dragColumn = Math.round((position.x - dragOffsetX) / controller.gridPitch);
            dragRow = Math.round((position.y - dragOffsetY) / controller.gridPitch);
            const placement = controller.previewPlacement(draggedTileId, dragTargetGroupId, dragColumn, dragRow);
            dragValid = placement.valid;
            dragMessage = dragValid ? qsTr("松开移至「%1」第 %2 列、第 %3 行；Esc 取消。").arg(group.group.name).arg(dragColumn + 1).arg(dragRow + 1) : placement.message;
            const location = group.gridItem.mapToItem(contentItem, placement.x, placement.y);
            previewX = location.x;
            previewY = location.y;
            previewWidth = placement.width;
            previewHeight = placement.height;
            return;
        }
    }

    function finishDrag(): void {
        if (!dragActive)
            return;
        const tileId = draggedTileId;
        const targetId = dragTargetGroupId;
        const valid = dragValid;
        const column = dragColumn;
        const row = dragRow;
        const message = dragMessage;
        clearDragPreview();
        if (valid)
            controller.moveTile(tileId, targetId, column, row);
        else
            selectionStatus = qsTr("未移动：%1").arg(message);
        Qt.callLater(restoreTileFocus);
    }

    function cancelDrag(): void {
        if (!dragActive)
            return;
        clearDragPreview();
        selectionStatus = qsTr("已取消拖拽，布局保持不变。");
        Qt.callLater(restoreTileFocus);
    }

    function clearDragPreview(): void {
        dragValid = false;
        dragTargetGroupId = "";
        dragMessage = "";
        previewX = 0;
        previewY = 0;
        previewWidth = 0;
        previewHeight = 0;
        draggedTileId = "";
    }

    function handleEscape(): void {
        if (dragActive)
            cancelDrag();
        else if (controller.searchActive || searchField.text.length > 0)
            clearSearch();
        else if (controller.editMode)
            controller.editMode = false;
        else
            requestDismissal();
    }

    function requestShortcutAction(actionId: string): void {
        if (actionId === "power") {
            const button = shortcutRail.mapToItem(contentItem, 8, shortcutRail.height - 60);
            powerMenu.x = button.x;
            powerMenu.y = Math.max(shadowInset, button.y - powerMenu.implicitHeight);
            powerMenu.open();
        } else {
            controller.requestDesktopAction(actionId);
        }
    }

    Connections {
        target: window.controller

        function onGroupsChanged(): void {
            Qt.callLater(window.restoreTileFocus);
        }

        function onSearchQueryChanged(): void {
            if (!searchField.inputMethodComposing && searchField.text !== window.controller.searchQuery)
                searchField.text = window.controller.searchQuery;
            window.updateSearchPresentation();
        }

        function onSearchChanged(): void {
            window.updateSearchPresentation();
        }

        function onSearchResultsAboutToChange(): void {
            window.rememberSearchSelection();
        }

        function onSearchResultsChanged(): void {
            window.restoreSearchSelection();
        }

        function onEditModeChanged(): void {
            if (!window.controller.editMode)
                window.cancelDrag();
            else
                window.showApplications = false;
        }

        function onReadOnlyChanged(): void {
            if (window.controller.readOnly) {
                window.cancelDrag();
                resizeMenu.close();
                pinMenu.close();
            }
        }

        function onLaunchSucceeded(): void {
            window.hide();
        }

        function onLaunchFailed(message: string): void {
            window.selectionStatus = qsTr("启动失败：%1").arg(message);
        }

        function onDesktopActionSucceeded(): void {
            window.hide();
        }
    }

    Shortcut {
        sequence: "Escape"
        autoRepeat: false
        enabled: !resizeMenu.visible && !pinMenu.visible && !powerMenu.visible && !aboutPopup.visible && !searchField.inputMethodComposing
        onActivated: window.handleEscape()
    }

    Shortcut {
        sequence: "Ctrl+F"
        autoRepeat: false
        enabled: !window.internalPopupVisible
        onActivated: window.focusSearch()
    }

    Timer {
        interval: 40
        repeat: true
        running: window.dragActive
        onTriggered: {
            const pointer = groupsFlickable.mapFromItem(null, window.dragPointerX, window.dragPointerY);
            const edge = 40;
            const step = 18;
            let dx = 0;
            let dy = 0;
            if (pointer.x >= -edge && pointer.x < edge)
                dx = -step;
            else if (pointer.x > groupsFlickable.width - edge && pointer.x <= groupsFlickable.width + edge)
                dx = step;
            if (pointer.y >= -edge && pointer.y < edge)
                dy = -step;
            else if (pointer.y > groupsFlickable.height - edge && pointer.y <= groupsFlickable.height + edge)
                dy = step;
            const nextX = Math.max(0, Math.min(groupsFlickable.contentX + dx, groupsFlickable.contentWidth - groupsFlickable.width));
            const nextY = Math.max(0, Math.min(groupsFlickable.contentY + dy, groupsFlickable.contentHeight - groupsFlickable.height));
            if (nextX !== groupsFlickable.contentX || nextY !== groupsFlickable.contentY) {
                groupsFlickable.contentX = nextX;
                groupsFlickable.contentY = nextY;
                window.updateDrag(window.dragPointerX, window.dragPointerY);
            }
        }
    }

    MouseArea {
        objectName: "shadowDismissArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (!window.panelContains(mouse.x, mouse.y))
                window.requestDismissal();
            else
                mouse.accepted = false;
        }
    }

    RowLayout {
        id: menuPanel

        objectName: "menuPanel"
        anchors.fill: parent
        anchors.margins: window.shadowInset
        spacing: 0

        ShortcutRail {
            id: shortcutRail

            Layout.preferredWidth: 64
            Layout.fillHeight: true
            darkTheme: window.darkTheme
            tokens: window.tokens
            busy: window.controller.desktopActionBusy
            onActionRequested: actionId => window.requestShortcutAction(actionId)
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Image {
                        objectName: "brandMark"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        source: "qrc:/xdstarter/app/xdstarter-mark.svg"
                        sourceSize.width: 42
                        sourceSize.height: 42
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("XDStarter")
                        color: window.theme.foreground
                        font.pixelSize: 24
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Button {
                        id: editLayoutButton

                        objectName: "editLayoutButton"
                        text: window.controller.editMode ? qsTr("完成编辑") : qsTr("编辑布局")
                        checkable: true
                        checked: window.controller.editMode
                        enabled: !window.controller.readOnly && !window.controller.searchActive
                        Accessible.description: qsTr("启用磁贴拖拽和 Ctrl 加方向键移动。Esc 退出编辑。")
                        onClicked: window.controller.editMode = !window.controller.editMode
                    }

                    Button {
                        id: themeButton

                        objectName: "themeButton"
                        text: window.darkTheme ? qsTr("切换浅色") : qsTr("切换深色")
                        focusPolicy: Qt.StrongFocus
                        enabled: !window.controller.readOnly
                        Accessible.description: qsTr("切换深浅主题并保存偏好。")
                        onClicked: window.controller.darkTheme = !window.controller.darkTheme
                        Keys.onReturnPressed: window.controller.darkTheme = !window.controller.darkTheme
                        Keys.onEnterPressed: window.controller.darkTheme = !window.controller.darkTheme
                    }

                    ToolButton {
                        objectName: "closeMenuButton"
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/close.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: window.theme.foreground
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("收起菜单")
                        Accessible.description: qsTr("关闭当前菜单。")
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.text: qsTr("收起菜单")
                        onClicked: window.requestDismissal()
                        Keys.onReturnPressed: window.requestDismissal()
                        Keys.onEnterPressed: window.requestDismissal()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: searchField

                        objectName: "searchField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("搜索应用、拼音或首字母 (Ctrl+F)")
                        color: window.theme.foreground
                        placeholderTextColor: window.theme.secondary
                        maximumLength: 128
                        selectByMouse: true
                        Accessible.name: qsTr("搜索应用")
                        Accessible.description: qsTr("输入名称、拼音或首字母。向下键选择结果，Enter 启动，Esc 清空搜索。")
                        onTextChanged: {
                            if (!inputMethodComposing)
                                window.controller.searchQuery = text;
                        }
                        onInputMethodComposingChanged: {
                            if (!inputMethodComposing)
                                window.controller.searchQuery = text;
                        }
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: event => {
                            if (inputMethodComposing)
                                return;
                            if (event.key === Qt.Key_Down && window.controller.searchActive) {
                                window.focusSearchResultAt(0);
                                event.accepted = true;
                            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && window.controller.searchActive) {
                                window.activateSearchResultAt(Math.max(0, searchResultsList.currentIndex));
                                event.accepted = true;
                            }
                        }

                        background: Rectangle {
                            radius: 8
                            color: Qt.alpha(window.tokens.brand.blue, window.darkTheme ? 0.09 : 0.035)
                            border.width: searchField.activeFocus ? 2 : 1
                            border.color: searchField.activeFocus ? window.tokens.brand.cyan : window.theme.menuBorder
                        }
                    }

                    Button {
                        objectName: "clearSearchButton"
                        text: qsTr("清空")
                        visible: searchField.text.length > 0 || searchField.inputMethodComposing
                        Accessible.name: qsTr("清空搜索")
                        onClicked: window.clearSearch()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !window.controller.searchActive

                    Label {
                        Layout.fillWidth: true
                        text: window.controller.editMode ? qsTr("编辑布局 · 拖拽移动，右键调整尺寸") : qsTr("你的应用，触手可及")
                        color: window.theme.foreground
                        wrapMode: Text.WordWrap
                    }

                    Button {
                        objectName: "catalogViewButton"
                        visible: window.compactLayout
                        text: window.showApplications ? qsTr("返回磁贴") : qsTr("所有应用")
                        enabled: !window.dragActive
                        onClicked: window.showApplications = !window.showApplications
                    }
                }

                RowLayout {
                    id: browsePanel

                    objectName: "browsePanel"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !window.controller.searchActive
                    spacing: 20

                    ColumnLayout {
                        id: catalogPanel

                        objectName: "catalogPanel"
                        visible: !window.compactLayout || window.showApplications
                        Layout.preferredWidth: window.compactLayout ? -1 : 260
                        Layout.maximumWidth: window.compactLayout ? Number.POSITIVE_INFINITY : 260
                        Layout.fillWidth: window.compactLayout
                        Layout.fillHeight: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("所有应用")
                                color: window.theme.foreground
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Button {
                                objectName: "refreshApplicationsButton"
                                text: window.controller.catalogBusy ? qsTr("扫描中") : qsTr("刷新")
                                enabled: !window.controller.catalogBusy
                                onClicked: window.controller.refreshApplications()
                            }
                        }

                        Label {
                            objectName: "catalogStatus"
                            Layout.fillWidth: true
                            text: window.controller.catalogStatus
                            color: window.theme.secondary
                            wrapMode: Text.WordWrap
                            Accessible.name: text
                        }

                        ListView {
                            id: applicationsList

                            objectName: "applicationList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            model: window.controller.applications
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                                focusPolicy: Qt.NoFocus
                            }

                            delegate: FocusScope {
                                id: appRow

                                required property string appId
                                required property string displayName
                                required property string kind
                                required property string iconSource
                                required property bool available
                                required property bool pinned
                                required property int index
                                readonly property string displayType: kind === "packaged" ? qsTr("打包应用") : qsTr("开始菜单快捷方式")

                                objectName: "appRow-" + appId
                                width: applicationsList.width
                                height: 64

                                function focusApplication(): void {
                                    appButton.forceActiveFocus(Qt.TabFocusReason);
                                }

                                ItemDelegate {
                                    id: appButton

                                    objectName: "applicationLaunch-" + appRow.appId
                                    focus: true
                                    anchors.left: parent.left
                                    anchors.right: pinButton.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 4
                                    enabled: appRow.available !== false
                                    padding: 6
                                    Accessible.name: appRow.displayName
                                    Accessible.description: qsTr("%1。按 Enter 启动，Shift 加 F10 固定到分组。").arg(appRow.displayType)
                                    onClicked: window.activateApplication(appRow.appId)
                                    Keys.onReturnPressed: window.activateApplication(appRow.appId)
                                    Keys.onEnterPressed: window.activateApplication(appRow.appId)
                                    Keys.onPressed: event => {
                                        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                            window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, appButton);
                                            event.accepted = true;
                                        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                            window.focusApplicationAt(appRow.index + (event.key === Qt.Key_Up ? -1 : 1));
                                            event.accepted = true;
                                        }
                                    }
                                    onActiveFocusChanged: {
                                        if (activeFocus) {
                                            // Catalogue focus must survive background tile availability updates.
                                            window.focusedTileId = "";
                                            applicationsList.currentIndex = appRow.index;
                                            applicationsList.positionViewAtIndex(appRow.index, ListView.Contain);
                                        }
                                    }

                                    ToolTip.visible: hovered || activeFocus
                                    ToolTip.text: appRow.displayName + " · " + appRow.displayType
                                    ToolTip.timeout: -1

                                    background: Rectangle {
                                        color: appButton.hovered || appButton.down ? Qt.alpha(window.tokens.brand.blue, 0.16) : "transparent"
                                        radius: 6
                                        border.width: appButton.activeFocus ? 2 : 0
                                        border.color: window.tokens.brand.cyan
                                    }

                                    contentItem: RowLayout {
                                        spacing: 8

                                        Item {
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32

                                            Image {
                                                id: catalogIcon

                                                objectName: "applicationIcon"
                                                anchors.fill: parent
                                                source: appRow.iconSource || ""
                                                sourceSize.width: 48
                                                sourceSize.height: 48
                                                asynchronous: true
                                                cache: true
                                                fillMode: Image.PreserveAspectFit
                                            }

                                            Label {
                                                anchors.centerIn: parent
                                                visible: catalogIcon.status !== Image.Ready
                                                text: appRow.displayName.slice(0, 1)
                                                font.pixelSize: 22
                                                font.bold: true
                                                color: window.theme.foreground
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            Label {
                                                Layout.fillWidth: true
                                                text: appRow.displayName
                                                color: window.theme.foreground
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: appRow.displayType
                                                font.pixelSize: 11
                                                color: window.theme.secondary
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: {
                                            appButton.forceActiveFocus(Qt.MouseFocusReason);
                                            window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, appButton);
                                        }
                                    }
                                }

                                Button {
                                    id: pinButton

                                    objectName: "pinButton-" + appRow.appId
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 62
                                    text: appRow.pinned ? qsTr("已固定") : qsTr("固定")
                                    enabled: !window.controller.readOnly && !appRow.pinned
                                    Accessible.name: qsTr("固定 %1").arg(appRow.displayName)
                                    onClicked: window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, pinButton)
                                }
                            }

                            Label {
                                objectName: "emptyCatalogHint"
                                anchors.fill: parent
                                anchors.margins: 12
                                visible: applicationsList.count === 0
                                text: window.controller.catalogBusy ? qsTr("正在读取应用目录…") : qsTr("暂未找到可用应用。可刷新目录后重试。")
                                color: window.theme.secondary
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ScrollView {
                        id: groupsScroll

                        objectName: "groupsScroll"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !window.compactLayout || !window.showApplications
                        contentWidth: groupsFlickable.contentWidth
                        contentHeight: groupsFlickable.contentHeight
                        clip: true
                        focusPolicy: Qt.NoFocus
                        rightPadding: effectiveScrollBarWidth
                        bottomPadding: effectiveScrollBarHeight

                        ScrollBar.horizontal: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            focusPolicy: Qt.NoFocus
                        }
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            focusPolicy: Qt.NoFocus
                        }

                        Flickable {
                            id: groupsFlickable

                            objectName: "groupsFlickable"
                            contentWidth: groupsColumn.width
                            contentHeight: groupsColumn.height
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.AutoFlickIfNeeded
                            clip: true
                            interactive: !window.dragActive
                            onWidthChanged: window.ensureActiveTileVisible()
                            onHeightChanged: window.ensureActiveTileVisible()

                            Column {
                                id: groupsColumn

                                spacing: 20

                                Repeater {
                                    id: groupRepeater

                                    model: window.controller.groups

                                    delegate: TileGroup {
                                        required property var modelData

                                        group: modelData
                                        tokens: window.tokens
                                        titleColor: window.theme.foreground
                                        editMode: window.controller.editMode
                                        readOnly: window.controller.readOnly
                                        iconRevision: window.controller.iconRevision
                                        iconVersions: window.controller.iconVersions
                                        draggedTileId: window.draggedTileId
                                        onSelected: name => window.selectPreview(name)
                                        onActivated: tileId => window.activateTile(tileId)
                                        onFocused: item => {
                                            const button = item as TileButton;
                                            if (button)
                                                window.focusedTileId = button.tile.id;
                                            if (!window.dragActive)
                                                window.ensureTileVisible(item);
                                        }
                                        onNavigate: (tileId, direction) => window.navigateTile(tileId, direction)
                                        onMoveBy: (tileId, dx, dy) => window.controller.moveTileBy(tileId, dx, dy)
                                        onContextRequested: (tileId, size, sceneX, sceneY) => window.openResizeMenu(tileId, size, sceneX, sceneY)
                                        onDragStarted: (tileId, sceneX, sceneY, offsetX, offsetY, tileWidth, tileHeight) => window.startDrag(tileId, sceneX, sceneY, offsetX, offsetY, tileWidth, tileHeight)
                                        onDragMoved: (sceneX, sceneY) => window.updateDrag(sceneX, sceneY)
                                        onDragFinished: window.finishDrag()
                                        onDragCanceled: window.cancelDrag()
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    objectName: "searchPanel"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: window.controller.searchActive
                    spacing: 8

                    Label {
                        objectName: "searchStatus"
                        Layout.fillWidth: true
                        text: window.controller.searchStatus
                        textFormat: Text.PlainText
                        color: window.theme.secondary
                        wrapMode: Text.WordWrap
                        Accessible.name: text
                    }

                    ListView {
                        id: searchResultsList

                        objectName: "searchResultsList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: window.controller.searchResults
                        clip: true
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        onModelChanged: currentIndex = count > 0 ? 0 : -1
                        onCountChanged: currentIndex = count > 0 ? 0 : -1
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            focusPolicy: Qt.NoFocus
                        }

                        delegate: ItemDelegate {
                            id: searchRow

                            required property var modelData
                            required property int index
                            readonly property string displayType: modelData.kind === "packaged" ? qsTr("打包应用") : qsTr("开始菜单快捷方式")

                            objectName: "searchResult-" + modelData.appId
                            width: searchResultsList.width
                            height: 68
                            padding: 10
                            opacity: modelData.available === false ? 0.65 : 1
                            Accessible.name: modelData.displayName
                            Accessible.description: modelData.available === false ? modelData.unavailableReason : qsTr("%1。按 Enter 启动。").arg(displayType)
                            onClicked: {
                                searchResultsList.currentIndex = index;
                                window.activateSearchResultAt(index);
                            }
                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    searchResultsList.currentIndex = index;
                                    searchResultsList.positionViewAtIndex(index, ListView.Contain);
                                }
                            }
                            Keys.onReturnPressed: window.activateSearchResultAt(index)
                            Keys.onEnterPressed: window.activateSearchResultAt(index)
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                    window.focusSearchResultAt(index + (event.key === Qt.Key_Up ? -1 : 1));
                                    event.accepted = true;
                                }
                            }

                            ToolTip {
                                id: searchResultToolTip

                                objectName: "searchResultToolTip"
                                visible: searchRow.hovered || searchRow.activeFocus
                                text: searchRow.modelData.displayName + " · " + (searchRow.modelData.available === false ? searchRow.modelData.unavailableReason : searchRow.displayType)
                                timeout: -1
                                contentItem: Text {
                                    text: searchResultToolTip.text
                                    textFormat: Text.PlainText
                                    font: searchResultToolTip.font
                                    wrapMode: Text.Wrap
                                    color: searchResultToolTip.palette.toolTipText
                                }
                            }

                            background: Rectangle {
                                radius: 6
                                color: searchRow.hovered || searchRow.down || searchRow.index === searchResultsList.currentIndex ? Qt.alpha(window.tokens.brand.blue, 0.16) : "transparent"
                                border.width: searchRow.activeFocus ? 2 : 0
                                border.color: window.tokens.brand.cyan
                            }

                            contentItem: RowLayout {
                                spacing: 12

                                Item {
                                    Layout.preferredWidth: 36
                                    Layout.preferredHeight: 36

                                    Image {
                                        id: searchIcon

                                        objectName: "searchResultIcon"
                                        anchors.fill: parent
                                        source: searchRow.modelData.iconSource ? searchRow.modelData.iconSource + "?v=" + (window.controller.iconVersions[searchRow.modelData.appId] || 0) : ""
                                        sourceSize.width: 54
                                        sourceSize.height: 54
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectFit
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: searchIcon.status !== Image.Ready
                                        text: searchRow.modelData.displayName.slice(0, 1)
                                        textFormat: Text.PlainText
                                        color: window.theme.foreground
                                        font.pixelSize: 24
                                        font.bold: true
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    Label {
                                        Layout.fillWidth: true
                                        text: searchRow.modelData.displayName
                                        textFormat: Text.PlainText
                                        color: window.theme.foreground
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: searchRow.modelData.available === false ? searchRow.modelData.unavailableReason : searchRow.displayType + (searchRow.modelData.pinned ? qsTr(" · 已固定") : "")
                                        textFormat: Text.PlainText
                                        color: window.theme.secondary
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Label {
                            objectName: "emptySearchHint"
                            anchors.fill: parent
                            anchors.margins: 16
                            visible: searchResultsList.count === 0
                            text: window.controller.searchBusy ? qsTr("正在搜索…") : qsTr("未找到与「%1」匹配的应用。可修改关键词或清空搜索。").arg(window.controller.searchQuery)
                            textFormat: Text.PlainText
                            color: window.theme.secondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Label {
                    objectName: "selectionStatus"
                    Layout.fillWidth: true
                    text: window.selectionStatus
                    textFormat: Text.PlainText
                    visible: text.length > 0
                    color: window.theme.foreground
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }

                Label {
                    objectName: "desktopActionStatus"
                    Layout.fillWidth: true
                    text: window.controller.desktopActionStatus
                    visible: text.length > 0
                    color: window.theme.foreground
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }

                Label {
                    objectName: "operationStatus"
                    Layout.fillWidth: true
                    text: window.dragActive ? window.dragMessage : (window.controller.operationMessage === window.controller.desktopActionStatus ? "" : window.controller.operationMessage)
                    visible: text.length > 0
                    color: window.dragActive && !window.dragValid ? window.tokens.status.error : window.theme.foreground
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        objectName: "persistenceStatus"
                        Layout.fillWidth: true
                        text: window.controller.persistenceStatus
                        color: window.theme.secondary
                        wrapMode: Text.WordWrap
                        Accessible.name: text
                    }

                    Button {
                        objectName: "saveLayoutButton"
                        text: window.controller.saving ? qsTr("正在保存") : qsTr("保存布局")
                        enabled: !window.controller.readOnly && !window.controller.saving && !window.dragActive
                        onClicked: window.controller.requestSave()
                    }

                    Button {
                        objectName: "aboutButton"
                        text: qsTr("关于")
                        onClicked: aboutPopup.open()
                        Keys.onReturnPressed: aboutPopup.open()
                        Keys.onEnterPressed: aboutPopup.open()
                    }
                }
            }
        }
    }

    Popup {
        id: aboutPopup

        objectName: "aboutPopup"
        parent: window.contentItem
        popupType: Popup.Item
        anchors.centerIn: parent
        width: Math.min(440, window.width - 48)
        padding: 20
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: qsTr("关于 XDStarter")
                font.pixelSize: 20
                font.bold: true
                color: window.theme.foreground
            }

            Label {
                Layout.fillWidth: true
                text: window.controller.platformName + "\n" + window.controller.platformStatus
                color: window.theme.secondary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: window.integrationStatus
                visible: text.length > 0
                color: window.theme.secondary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("单击或 Enter 启动应用，右键固定或调整磁贴。编辑时可用 Ctrl + 方向键移动，Esc 取消。")
                color: window.theme.foreground
                wrapMode: Text.WordWrap
            }

            Button {
                objectName: "closeAboutButton"
                text: qsTr("关闭")
                onClicked: aboutPopup.close()
                Keys.onReturnPressed: aboutPopup.close()
                Keys.onEnterPressed: aboutPopup.close()
            }
        }

        background: Rectangle {
            radius: window.tokens.cornerRadiusDip.menu
            color: window.theme.opaqueFallback
            border.width: 1
            border.color: window.tokens.brand.blue
        }
    }

    Menu {
        id: powerMenu

        objectName: "powerMenu"
        parent: window.contentItem
        popupType: Popup.Item
        title: qsTr("电源与退出")
        width: 240

        MenuItem {
            objectName: "winKeyCaptureItem"
            text: window.winKeyCaptureBusy ? qsTr("正在启用 Win 键唤起…") : window.winKeyCaptureActive ? qsTr("暂停 Win 键唤起") : qsTr("启用 Win 键唤起")
            enabled: !window.winKeyCaptureBusy && (window.winKeyCaptureAvailable || window.winKeyCaptureActive)
            Accessible.description: window.winKeyCaptureStatus
            onTriggered: {
                powerMenu.close();
                window.winKeyCaptureToggleRequested();
            }
            Keys.onReturnPressed: {
                powerMenu.close();
                window.winKeyCaptureToggleRequested();
            }
            Keys.onEnterPressed: {
                powerMenu.close();
                window.winKeyCaptureToggleRequested();
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("系统电源操作暂未启用")
            enabled: false
        }

        MenuSeparator {}

        MenuItem {
            objectName: "exitApplicationItem"
            text: qsTr("退出 XDStarter")
            onTriggered: {
                powerMenu.close();
                window.controller.requestExit();
            }
            Keys.onReturnPressed: {
                powerMenu.close();
                window.controller.requestExit();
            }
            Keys.onEnterPressed: {
                powerMenu.close();
                window.controller.requestExit();
            }
        }
    }

    component SizeMenuItem: MenuItem {
        id: sizeOption

        checkable: true
        autoExclusive: true
        enabled: !window.controller.readOnly
        palette.light: window.tokens.brand.cyan
        palette.midlight: window.tokens.brand.blue
        palette.windowText: sizeOption.highlighted || sizeOption.down ? window.tokens.brand.ink : window.theme.foreground
    }

    Menu {
        id: resizeMenu

        objectName: "tileSizeMenu"
        title: qsTr("磁贴")
        parent: window.contentItem
        popupType: Popup.Item
        onClosed: Qt.callLater(window.restoreTileFocus)

        SizeMenuItem {
            objectName: "sizeSmallItem"
            text: qsTr("小磁贴 · 1×1")
            checked: window.resizeTileSize === "small"
            onTriggered: window.resizeCurrentTile("small")
        }

        SizeMenuItem {
            objectName: "sizeStandardItem"
            text: qsTr("标准磁贴 · 2×2")
            checked: window.resizeTileSize === "standard"
            onTriggered: window.resizeCurrentTile("standard")
        }

        SizeMenuItem {
            objectName: "sizeWideItem"
            text: qsTr("宽磁贴 · 4×2")
            checked: window.resizeTileSize === "wide"
            onTriggered: window.resizeCurrentTile("wide")
        }

        MenuSeparator {}

        MenuItem {
            id: unpinOption

            objectName: "unpinTileItem"
            text: qsTr("取消固定")
            enabled: !window.controller.readOnly
            palette.light: window.tokens.brand.cyan
            palette.midlight: window.tokens.brand.blue
            palette.windowText: unpinOption.highlighted || unpinOption.down ? window.tokens.brand.ink : window.theme.foreground
            onTriggered: window.unpinCurrentTile()
            Keys.onReturnPressed: window.unpinCurrentTile()
            Keys.onEnterPressed: window.unpinCurrentTile()
        }
    }

    Menu {
        id: pinMenu

        objectName: "pinApplicationMenu"
        title: qsTr("固定到分组")
        parent: window.contentItem
        popupType: Popup.Item
        width: Math.min(320, window.width - 48)

        MenuItem {
            text: qsTr("选择目标分组")
            Accessible.description: qsTr("为 %1 选择固定位置").arg(window.pinApplicationName)
            enabled: false
        }

        Instantiator {
            model: window.controller.groups
            onObjectAdded: (index, object) => pinMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => pinMenu.removeItem(object)

            delegate: MenuItem {
                id: pinGroupOption

                required property var modelData

                objectName: "pinToGroup-" + modelData.id
                text: modelData.name
                enabled: !window.controller.readOnly && !window.pinApplicationPinned
                palette.light: window.tokens.brand.cyan
                palette.midlight: window.tokens.brand.blue
                palette.windowText: pinGroupOption.highlighted || pinGroupOption.down ? window.tokens.brand.ink : window.theme.foreground
                onTriggered: {
                    // Pinning refreshes groups, which replaces this dynamically inserted menu item.
                    window.pinCurrentApplication(modelData.id);
                }
                Keys.onReturnPressed: window.pinCurrentApplication(modelData.id)
                Keys.onEnterPressed: window.pinCurrentApplication(modelData.id)
            }
        }
    }

    Rectangle {
        objectName: "dragPlacementPreview"
        z: 1000
        visible: window.dragActive
        x: window.previewX
        y: window.previewY
        width: window.previewWidth
        height: window.previewHeight
        radius: window.tokens.cornerRadiusDip.tile
        color: Qt.alpha(window.dragValid ? window.tokens.brand.cyan : window.tokens.status.error, window.dragValid ? 0.2 : 0.27)
        border.width: 2
        border.color: window.dragValid ? window.tokens.brand.cyan : window.tokens.status.error
    }

    onClosing: prepareForDismissal()

    onVisibleChanged: {
        if (!visible) {
            prepareForDismissal();
        } else {
            Qt.callLater(function () {
                if (window.visible)
                    window.prepareForPresentation();
            });
        }
    }

    Component.onCompleted: prepareForPresentation()
}
