pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as FileDialogs
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
    property bool selectionStatusError: false
    readonly property bool showSelectionFeedback: selectionStatus.length > 0 && (selectionStatusError || selectionFeedbackTimer.running)
    readonly property bool showDesktopFeedback: controller.desktopActionStatus.length > 0 && (controller.desktopActionError || controller.desktopActionBusy || desktopFeedbackTimer.running)
    readonly property bool showOperationFeedback: dragActive || controller.operationMessage.length > 0 && controller.operationMessage !== controller.desktopActionStatus && (controller.operationError || operationFeedbackTimer.running)
    readonly property bool showPersistenceFeedback: controller.persistenceStatus.length > 0 && (controller.readOnly || controller.persistenceError || controller.saving || persistenceFeedbackTimer.running)
    property string integrationStatus: ""
    property bool winKeyCaptureAvailable: false
    property bool winKeyCaptureActive: false
    property bool winKeyCaptureBusy: false
    property string winKeyCaptureStatus: qsTr("Win 键唤起未启用；开始按钮保持原生行为。")
    readonly property bool compactLayout: panelWidth - shortcutRail.width - 2 * tokens.layout.contentMargin < tokens.layout.catalogWidth + tokens.layout.contentSpacing + 552
    property bool showApplications: false
    readonly property var applicationBrowser: controller.applications
    property bool alphabetOpen: false
    onAlphabetOpenChanged: {
        if (alphabetOpen) {
            Qt.callLater(function () {
                for (let index = 0; index < alphabetRepeater.count; ++index) {
                    const button = alphabetRepeater.itemAt(index);
                    if (button && button.enabled) {
                        button.forceActiveFocus(Qt.TabFocusReason);
                        break;
                    }
                }
            });
        }
    }
    property string pendingCatalogFolder: ""

    function navigateCatalogFolder(id: string): void {
        if (folderTransition.running)
            return;
        pendingCatalogFolder = id;
        alphabetOpen = false;
        folderFadeOut.restart();
        folderTransition.start();
    }

    Timer {
        id: folderTransition
        interval: 100
        onTriggered: {
            if (window.pendingCatalogFolder === "..")
                window.applicationBrowser.back();
            else
                window.applicationBrowser.openFolder(window.pendingCatalogFolder);
            applicationsList.positionViewAtBeginning();
            applicationsList.currentIndex = -1;
            folderFadeIn.restart();
        }
    }
    NumberAnimation {
        id: folderFadeOut
        target: applicationsList
        property: "opacity"
        to: 0
        duration: 100
    }
    NumberAnimation {
        id: folderFadeIn
        target: applicationsList
        property: "opacity"
        to: 1
        duration: 170
        easing.type: Easing.OutCubic
    }

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
    property string dismissedTileId: ""
    property bool dismissalFocusSaved: false
    property string focusedGroupId: ""
    property bool groupFocusPending: false
    property string managedGroupId: ""
    property string namingGroupId: ""
    property string namingTileId: ""
    property string tileNameError: ""
    readonly property var contextTile: tileById(resizeTileId)
    property string groupNameError: ""
    readonly property var managedGroup: controller.groups.find(group => group.id === managedGroupId) || null
    readonly property int managedGroupIndex: controller.groups.findIndex(group => group.id === managedGroupId)
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
    readonly property int dismissAnimationDuration: 120
    property real revealProgress: 1
    NumberAnimation {
        id: revealMotion
        target: window
        property: "revealProgress"
        from: 0
        to: 1
        duration: 180
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: dismissMotion
        target: window
        property: "revealProgress"
        to: 0
        duration: 120
        easing.type: Easing.InCubic
    }
    function animateDismissal(): void {
        revealMotion.stop();
        dismissMotion.restart();
    }
    property bool dismissalManaged: false
    property bool presentationSuspended: false
    readonly property bool internalPopupVisible: resizeMenu.visible || pinMenu.visible || powerMenu.visible || aboutPopup.visible || groupMenu.visible || groupNameDialog.visible || moreMenu.visible || tileNameDialog.visible || layoutFileDialog.visible || layoutImportDialog.visible
    readonly property bool dismissalBlocked: dragActive || internalPopupVisible
    readonly property bool contextShortcutsEnabled: visible && !presentationSuspended && !internalPopupVisible && !dragActive && !controller.searchActive && !searchField.inputMethodComposing && !alphabetOpen && !folderTransition.running
    readonly property TileButton tileContextTarget: activeFocusItem as TileButton

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
        opacity: window.revealProgress
        transform: Translate {
            y: (1 - window.revealProgress) * 10
        }
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
    palette.button: theme.surface
    palette.buttonText: theme.foreground
    palette.toolTipBase: theme.surface
    palette.toolTipText: theme.foreground
    palette.mid: theme.controlBorder
    palette.highlight: tokens.brand.blue
    palette.highlightedText: tokens.themes.dark.foreground

    font.pixelSize: 14

    component CategoryGlyph: Item {
        id: categoryGlyph
        required property string label
        Text {
            anchors.fill: parent
            text: categoryGlyph.label === "🌐" ? "" : categoryGlyph.label
            font.family: window.font.family
            font.pixelSize: 22
            color: window.theme.secondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Image {
            anchors.centerIn: parent
            width: 32
            height: 32
            visible: categoryGlyph.label === "🌐"
            source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/globe.svg"
            sourceSize.width: 64
            sourceSize.height: 64
        }
    }

    component ActionButton: Button {
        id: actionButton

        implicitHeight: window.tokens.layout.controlSize
        leftPadding: 14
        rightPadding: 14
        focusPolicy: Qt.StrongFocus
        opacity: enabled ? 1 : 0.45
        contentItem: Text {
            text: actionButton.text
            textFormat: Text.PlainText
            color: window.theme.foreground
            font: actionButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: actionButton.down ? window.theme.pressed : actionButton.hovered ? window.theme.hover : window.theme.surface
            border.width: (actionButton.activeFocus && window.controller.keyboardNavigation) ? 2 : 1
            border.color: (actionButton.activeFocus && window.controller.keyboardNavigation) ? window.tokens.brand.blue : window.theme.controlBorder
        }
    }

    component IconButton: ToolButton {
        id: iconButton
        property real cornerRadius: 8
        property string hintText: Accessible.name

        implicitWidth: window.tokens.layout.controlSize
        implicitHeight: window.tokens.layout.controlSize
        icon.width: 20
        icon.height: 20
        icon.color: window.theme.foreground
        focusPolicy: Qt.StrongFocus
        opacity: enabled ? 1 : 0.45
        Hint {
            visible: iconButton.enabled && (iconButton.hovered || iconButton.activeFocus) && text.length > 0
            text: iconButton.hintText
        }
        background: Rectangle {
            radius: iconButton.cornerRadius
            color: iconButton.down ? window.theme.pressed : iconButton.checked ? Qt.alpha(window.tokens.brand.blue, 0.14) : iconButton.hovered ? window.theme.hover : "transparent"
            border.width: (iconButton.activeFocus && window.controller.keyboardNavigation) || iconButton.checked ? 1 : 0
            border.color: window.tokens.brand.blue
        }
    }

    component ActionMenu: Menu {
        padding: 6
        font.family: window.font.family
        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 140
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.96
                    to: 1
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: 100
                }
                NumberAnimation {
                    property: "scale"
                    to: 0.97
                    duration: 100
                }
            }
        }
        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                anchors.topMargin: 1
                anchors.bottomMargin: -7
                radius: 14
                color: "#14000000"
            }
            Rectangle {
                anchors.fill: parent
                radius: 10
                color: window.theme.surface
                border.width: 1
                border.color: window.theme.controlBorder
            }
        }
    }

    component ActionMenuItem: MenuItem {
        id: menuAction
        font.family: window.font.family
        scale: down ? 0.985 : 1
        Behavior on scale {
            NumberAnimation {
                duration: 90
                easing.type: Easing.OutCubic
            }
        }

        implicitHeight: 36
        leftPadding: 12
        rightPadding: 12
        opacity: enabled ? 1 : 0.45
        indicator: Text {
            font.family: window.font.family
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: menuAction.checkable && menuAction.checked
            text: "✓"
            color: window.theme.foreground
            font.pixelSize: 15
        }
        contentItem: Text {
            leftPadding: menuAction.checkable ? 22 : 0
            text: menuAction.text
            textFormat: Text.PlainText
            font: menuAction.font
            color: window.theme.foreground
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 5
            color: !menuAction.enabled ? "transparent" : menuAction.down ? window.theme.pressed : menuAction.highlighted || menuAction.hovered ? window.theme.hover : "transparent"
            Behavior on color {
                ColorAnimation {
                    duration: 90
                }
            }
            border.width: (menuAction.activeFocus && window.controller.keyboardNavigation) ? 1 : 0
            border.color: window.tokens.brand.blue
        }
    }

    component ActionSeparator: MenuSeparator {
        topPadding: 5
        bottomPadding: 5
        contentItem: Rectangle {
            implicitHeight: 1
            color: window.theme.separator
        }
    }

    function selectPreview(name: string): void {
        selectionStatus = qsTr("已选择「%1」。这是布局预览，不会启动应用。").arg(name);
    }

    function activateApplication(applicationId: string): void {
        if (!controller.launchBusy && controller.launchApplication(applicationId))
            selectionStatus = qsTr("正在启动应用…");
    }

    function activateTile(tileId: string): void {
        if (!controller.launchBusy && controller.launchTile(tileId))
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

    function findTileItem(id: string): TileButton {
        const find = function (item) {
            if (item.objectName === "tile-" + id)
                return item;
            for (let index = 0; index < item.children.length; ++index) {
                const match = find(item.children[index]);
                if (match)
                    return match;
            }
            return null;
        };
        return find(contentItem) as TileButton;
    }
    Rectangle {
        id: removalGhost
        objectName: "removalGhost"
        z: 800
        radius: window.tokens.cornerRadiusDip.tile
        visible: removalMotion.running
        Label {
            id: removalGhostLabel
            anchors.centerIn: parent
            color: "white"
        }
    }
    ParallelAnimation {
        id: removalMotion
        NumberAnimation {
            target: removalGhost
            property: "opacity"
            from: 1
            to: 0
            duration: 140
        }
        NumberAnimation {
            target: removalGhost
            property: "scale"
            from: 1
            to: 0.85
            duration: 140
            easing.type: Easing.InCubic
        }
    }

    function unpinCurrentTile(): void {
        resizeMenu.close();
        const tileId = resizeTileId;
        const item = findTileItem(tileId);
        if (item) {
            const location = item.mapToItem(contentItem, 0, 0);
            removalGhost.x = location.x;
            removalGhost.y = location.y;
            removalGhost.width = item.width;
            removalGhost.height = item.height;
            removalGhost.color = item.baseColor;
            removalGhostLabel.text = item.tile.small ? item.tile.symbol : item.tile.name;
        }
        focusedTileId = "";
        if (!controller.unpinTile(tileId)) {
            focusedTileId = tileId;
        } else {
            removalMotion.restart();
            Qt.callLater(function () {
                moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
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
        if (visible && !presentationSuspended && !internalPopupVisible && !dragActive && !controller.searchActive && !searchField.activeFocus && focusedGroupId.length === 0)
            focusTileById(focusedTileId);
    }

    function restoreGroupFocus(): void {
        if (!visible || presentationSuspended || internalPopupVisible || dragActive || controller.searchActive || (searchField.activeFocus && !groupFocusPending) || !controller.editMode)
            return;
        let focusItem = activeFocusItem;
        let groupHasFocus = false;
        while (focusItem && focusItem !== contentItem) {
            if (focusItem.objectName.indexOf("groupActions-") === 0) {
                groupHasFocus = true;
                break;
            }
            focusItem = focusItem.parent;
        }
        const focusWasLost = !activeFocusItem || activeFocusItem === contentItem || !activeFocusItem.visible;
        const returningFromCreate = groupFocusPending && (activeFocusItem === newGroupButton || activeFocusItem === moreMenuButton);
        // Replacing a focused Repeater delegate leaves its ScrollView focus scope active.
        // Only our pending operation can return from these non-interactive containers.
        const returningFromLayout = groupFocusPending && (activeFocusItem === groupsScroll || activeFocusItem === groupsFlickable || activeFocusItem === groupsColumn);
        if (!groupFocusPending && !focusWasLost && !groupHasFocus && !returningFromCreate && !returningFromLayout) {
            groupFocusPending = false;
            return;
        }
        for (let index = 0; index < groupRepeater.count; ++index) {
            const group = groupRepeater.itemAt(index) as TileGroup;
            if (group && group.group.id === focusedGroupId) {
                ensureTileVisible(group.actionsItem);
                group.focusActions();
                groupFocusPending = false;
                return;
            }
        }
    }

    function openGroupMenu(groupId: string, source: Item): void {
        if (!controller.editMode || controller.readOnly || dragActive)
            return;
        managedGroupId = groupId;
        focusedGroupId = groupId;
        const position = source.mapToItem(contentItem, 0, source.height);
        groupMenu.x = Math.max(shadowInset, Math.min(position.x, width - shadowInset - groupMenu.width));
        groupMenu.y = Math.max(shadowInset, Math.min(position.y, height - shadowInset - groupMenu.implicitHeight));
        groupMenu.open();
    }

    function tileById(tileId: string): var {
        for (const group of controller.groups)
            for (const tile of group.tiles)
                if (tile.id === tileId)
                    return tile;
        return null;
    }

    function openTileNameDialog(): void {
        if (controller.readOnly || dragActive || !contextTile)
            return;
        namingTileId = contextTile.id;
        focusedTileId = namingTileId;
        focusedGroupId = "";
        tileNameError = "";
        tileNameField.text = contextTile.name;
        resizeMenu.close();
        tileNameDialog.open();
    }

    function submitTileName(): void {
        if (tileNameField.inputMethodComposing || controller.readOnly)
            return;
        const rawName = tileNameField.text;
        const name = rawName.trim();
        if (name.length === 0 || name.length > controller.maximumTileDisplayNameLength || /[\u0000-\u001f\u007f-\u009f\u2028\u2029]/.test(rawName)) {
            tileNameError = qsTr("请输入 1 至 %1 个字符，不含控制字符或换行的磁贴显示名称。").arg(controller.maximumTileDisplayNameLength);
            return;
        }
        if (!controller.renameTile(namingTileId, rawName)) {
            tileNameError = controller.operationMessage;
            return;
        }
        tileNameDialog.close();
    }

    function resetCurrentTileName(): void {
        const tileId = resizeTileId;
        resizeMenu.close();
        controller.resetTileDisplayName(tileId);
        Qt.callLater(restoreTileFocus);
    }

    function openGroupNameDialog(groupId: string): void {
        if (!controller.editMode || controller.readOnly)
            return;
        namingGroupId = groupId;
        groupNameError = "";
        const group = controller.groups.find(candidate => candidate.id === groupId);
        groupNameField.text = group ? group.name : "";
        groupMenu.close();
        moreMenu.close();
        groupNameDialog.open();
    }

    function submitGroupName(): void {
        if (groupNameField.inputMethodComposing || !controller.editMode || controller.readOnly)
            return;
        const rawName = groupNameField.text;
        const name = rawName.trim();
        if (name.length === 0 || name.length > controller.maximumGroupNameLength || /[\u0000-\u001f\u007f-\u009f\u2028\u2029]/.test(rawName)) {
            groupNameError = qsTr("请输入 1 至 %1 个字符，不含控制字符的分组名称。").arg(controller.maximumGroupNameLength);
            return;
        }
        const id = namingGroupId;
        const acceptedId = id.length === 0 ? controller.createGroup(name) : controller.renameGroup(id, name) ? id : "";
        if (acceptedId.length === 0) {
            groupNameError = qsTr("无法保存分组，请检查分组数量或布局状态后重试。");
            return;
        }
        focusedGroupId = acceptedId;
        groupFocusPending = true;
        showApplications = false;
        groupNameDialog.close();
        Qt.callLater(restoreGroupFocus);
    }

    function moveManagedGroup(delta: int): void {
        const id = managedGroupId;
        groupMenu.close();
        if (controller.moveGroupBy(id, delta)) {
            focusedGroupId = id;
            groupFocusPending = true;
        }
        Qt.callLater(restoreGroupFocus);
    }

    function removeManagedGroup(): void {
        const index = managedGroupIndex;
        const id = managedGroupId;
        const fallback = controller.groups[index > 0 ? index - 1 : 1];
        groupMenu.close();
        if (controller.removeGroup(id)) {
            focusedGroupId = fallback ? fallback.id : "";
            groupFocusPending = true;
        }
        Qt.callLater(restoreGroupFocus);
    }

    function focusSearch(): void {
        cancelDrag();
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
        searchField.selectAll();
    }

    function clearSearch(): void {
        // qmllint disable missing-property
        Qt.inputMethod.reset();
        // qmllint enable missing-property
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
        reopenMoreTimer.stop();
        resizeMenu.close();
        pinMenu.close();
        powerMenu.close();
        aboutPopup.close();
        groupMenu.close();
        groupNameDialog.close();
        moreMenu.close();
        tileNameDialog.close();
        layoutFileDialog.close();
        layoutImportDialog.close();
        controller.cancelLayoutImport();
    }

    Timer {
        id: reopenMoreTimer
        interval: 130
        onTriggered: {
            if (window.visible && !window.presentationSuspended)
                window.openMoreMenu();
        }
    }
    function openMoreMenu(): void {
        if (moreMenu.visible && !moreMenu.opened) {
            reopenMoreTimer.restart();
            return;
        }
        if (dragActive)
            return;
        const position = moreMenuButton.mapToItem(contentItem, moreMenuButton.width, moreMenuButton.height + 6);
        moreMenu.x = Math.max(shadowInset, Math.min(position.x - moreMenu.width, width - shadowInset - moreMenu.width));
        moreMenu.y = Math.max(shadowInset, Math.min(position.y, height - shadowInset - moreMenu.implicitHeight));
        moreMenu.open();
    }

    function toggleTheme(): void {
        moreMenu.close();
        if (!controller.readOnly)
            controller.darkTheme = !controller.darkTheme;
    }

    function saveLayout(): void {
        moreMenu.close();
        if (!controller.readOnly && !controller.saving && !dragActive)
            controller.requestSave();
    }

    function openAbout(): void {
        moreMenu.close();
        aboutPopup.open();
    }

    function openLayoutImport(): void {
        closeInternalPopups();
        if (!controller.readOnly && controller.layoutImportSupported && !controller.importBusy)
            layoutFileDialog.open();
    }

    function restoreLayoutBackup(): void {
        closeInternalPopups();
        if (controller.stageBackupRestore())
            layoutImportDialog.open();
    }

    function prepareForDismissal(): void {
        if (!presentationSuspended) {
            dismissedTileId = dragActive ? draggedTileId : focusedTileId;
            dismissalFocusSaved = true;
        }
        alphabetOpen = false;
        folderTransition.stop();
        folderFadeOut.stop();
        folderFadeIn.stop();
        applicationsList.opacity = 1;
        presentationSuspended = true;
        closeInternalPopups();
        cancelDrag();
        selectionFeedbackTimer.stop();
        desktopFeedbackTimer.stop();
        operationFeedbackTimer.stop();
        persistenceFeedbackTimer.stop();
    }

    function prepareForPresentation(): void {
        dismissMotion.stop();
        revealMotion.restart();
        presentationSuspended = true;
        closeInternalPopups();
        cancelDrag();
        presentationSuspended = false;
        searchField.forceActiveFocus(Qt.OtherFocusReason);
        if (dismissalFocusSaved)
            focusedTileId = dismissedTileId;
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
        if (alphabetOpen)
            alphabetOpen = false;
        else if (applicationBrowser && applicationBrowser.inFolder && !controller.searchActive)
            navigateCatalogFolder("..");
        else if (dragActive)
            cancelDrag();
        else if (controller.searchActive || searchField.text.length > 0)
            clearSearch();
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

        function onLayoutImportSucceeded(): void {
            window.dismissalFocusSaved = false;
            window.dismissedTileId = "";
            layoutImportDialog.close();
            window.showApplications = false;
            window.browseStateSaved = false;
            window.focusedTileId = "";
            window.focusedGroupId = "";
            if (window.visible && !window.presentationSuspended)
                searchField.forceActiveFocus(Qt.OtherFocusReason);
        }

        function onGroupsChanged(): void {
            Qt.callLater(window.restoreTileFocus);
            Qt.callLater(window.restoreGroupFocus);
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
            if (!window.controller.editMode) {
                window.cancelDrag();
                groupMenu.close();
                groupNameDialog.close();
                tileNameDialog.close();
                window.focusedGroupId = "";
                window.groupFocusPending = false;
            } else
                window.showApplications = false;
        }

        function onReadOnlyChanged(): void {
            if (window.controller.readOnly) {
                window.cancelDrag();
                resizeMenu.close();
                pinMenu.close();
                groupMenu.close();
                groupNameDialog.close();
                tileNameDialog.close();
            }
        }

        function onLaunchSucceeded(): void {
            window.hide();
        }

        function onLaunchFailed(message: string): void {
            window.selectionStatus = qsTr("启动失败：%1").arg(message);
            window.selectionStatusError = true;
        }

        function onOperationMessageChanged(): void {
            if (window.visible && !window.presentationSuspended)
                operationFeedbackTimer.restart();
        }

        function onPersistenceStatusChanged(): void {
            if (window.visible && !window.presentationSuspended)
                persistenceFeedbackTimer.restart();
        }

        function onDesktopActionStateChanged(): void {
            if (window.visible && !window.presentationSuspended)
                desktopFeedbackTimer.restart();
        }

        function onDesktopActionSucceeded(): void {
            window.hide();
        }
    }

    Shortcut {
        objectName: "tileContextShortcut"
        sequence: "Shift+F10"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.contextShortcutsEnabled && window.tileContextTarget !== null && window.tileContextTarget.visible && window.tileContextTarget.enabled
        onActivated: window.tileContextTarget.requestContextMenu()
    }

    Shortcut {
        sequence: "Escape"
        autoRepeat: false
        enabled: !window.internalPopupVisible && !searchField.inputMethodComposing
        onActivated: window.handleEscape()
    }

    Timer {
        id: selectionFeedbackTimer
        objectName: "selectionFeedbackTimer"
        interval: 4500
    }
    Timer {
        id: desktopFeedbackTimer
        objectName: "desktopFeedbackTimer"
        interval: 4500
    }
    Timer {
        id: operationFeedbackTimer
        objectName: "operationFeedbackTimer"
        interval: 4500
    }
    Timer {
        id: persistenceFeedbackTimer
        objectName: "persistenceFeedbackTimer"
        interval: 4500
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

    property int allowedResizeEdges: Qt.TopEdge | Qt.RightEdge
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge]
        delegate: MouseArea {
            required property int modelData
            objectName: "resizeEdge-" + modelData
            z: 950
            enabled: (window.allowedResizeEdges & modelData) !== 0 && !window.dragActive && !window.internalPopupVisible
            visible: enabled
            readonly property bool horizontal: modelData === Qt.TopEdge || modelData === Qt.BottomEdge
            x: modelData === Qt.RightEdge ? window.width - window.shadowInset - 4 : window.shadowInset
            y: modelData === Qt.BottomEdge ? window.height - window.shadowInset - 4 : window.shadowInset
            width: horizontal ? window.panelWidth : 6
            height: horizontal ? 6 : window.panelHeight
            cursorShape: horizontal ? Qt.SizeVerCursor : Qt.SizeHorCursor
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                mouse.accepted = window.startSystemResize(modelData);
            }
        }
    }

    RowLayout {
        id: menuPanel

        objectName: "menuPanel"
        opacity: window.revealProgress
        // qmllint disable Quick.layout-positioning
        transform: Translate {
            y: (1 - window.revealProgress) * 10
        }
        // qmllint enable Quick.layout-positioning
        anchors.fill: parent
        anchors.margins: window.shadowInset
        spacing: 0

        ShortcutRail {
            id: shortcutRail

            Layout.preferredWidth: window.tokens.layout.railWidth
            Layout.fillHeight: true
            darkTheme: window.darkTheme
            tokens: window.tokens
            keyboardNavigation: window.controller.keyboardNavigation
            busy: window.controller.desktopActionBusy
            onActionRequested: actionId => window.requestShortcutAction(actionId)
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: window.tokens.layout.contentMargin
                spacing: window.tokens.layout.contentSpacing

                RowLayout {
                    objectName: "searchToolbar"
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: searchField

                        objectName: "searchField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: window.tokens.layout.searchHeight
                        leftPadding: 40
                        rightPadding: clearSearchButton.visible ? 40 : 14
                        font.pixelSize: 14
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
                            radius: window.tokens.layout.searchHeight / 2
                            color: window.theme.searchBackground
                            border.width: (searchField.activeFocus && window.controller.keyboardNavigation) ? 2 : 1
                            border.color: (searchField.activeFocus && window.controller.keyboardNavigation) ? window.tokens.brand.blue : window.theme.controlBorder
                        }

                        Image {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/search.svg"
                            sourceSize.width: 36
                            sourceSize.height: 36
                            Accessible.ignored: true
                        }

                        IconButton {
                            id: clearSearchButton

                            objectName: "clearSearchButton"
                            cornerRadius: width / 2
                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 32
                            visible: searchField.text.length > 0 || searchField.inputMethodComposing
                            icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/close.svg"
                            Accessible.name: qsTr("清空搜索")
                            onClicked: window.clearSearch()
                            Keys.onReturnPressed: window.clearSearch()
                            Keys.onEnterPressed: window.clearSearch()
                        }
                    }

                    IconButton {
                        id: moreMenuButton

                        objectName: "moreMenuButton"
                        Layout.preferredWidth: window.tokens.layout.controlSize
                        Layout.preferredHeight: window.tokens.layout.controlSize
                        icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/more.svg"
                        Accessible.name: qsTr("更多选项")
                        onClicked: window.openMoreMenu()
                        Keys.onReturnPressed: window.openMoreMenu()
                        Keys.onEnterPressed: window.openMoreMenu()
                    }

                    IconButton {
                        objectName: "closeMenuButton"
                        Layout.preferredWidth: window.tokens.layout.controlSize
                        Layout.preferredHeight: window.tokens.layout.controlSize
                        icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/close.svg"
                        Accessible.name: qsTr("收起菜单")
                        Accessible.description: qsTr("关闭当前菜单。")
                        onClicked: window.requestDismissal()
                        Keys.onReturnPressed: window.requestDismissal()
                        Keys.onEnterPressed: window.requestDismissal()
                    }
                }

                RowLayout {
                    objectName: "browseContextToolbar"
                    Layout.fillWidth: true
                    visible: !window.controller.searchActive && window.compactLayout
                    spacing: 12

                    ActionButton {
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
                    spacing: window.tokens.layout.contentSpacing

                    ColumnLayout {
                        id: catalogPanel

                        objectName: "catalogPanel"
                        visible: !window.compactLayout || window.showApplications
                        Layout.preferredWidth: window.compactLayout ? -1 : window.tokens.layout.catalogWidth
                        Layout.maximumWidth: window.compactLayout ? Number.POSITIVE_INFINITY : window.tokens.layout.catalogWidth
                        Layout.fillWidth: window.compactLayout
                        Layout.fillHeight: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: window.tokens.layout.headerHeight
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("所有应用")
                                color: window.theme.foreground
                                textFormat: Text.PlainText
                                font.pixelSize: window.tokens.font.heading
                                font.bold: true
                            }

                            Label {
                                text: "A–Z"
                                color: window.theme.secondary
                                font.pixelSize: window.tokens.font.caption
                                Accessible.name: qsTr("应用按现有目录顺序显示，字母与首字符分节。")
                            }

                            IconButton {
                                objectName: "refreshApplicationsButton"
                                implicitWidth: 28
                                implicitHeight: 28
                                icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/refresh.svg"
                                enabled: !window.controller.catalogBusy
                                Accessible.name: window.controller.catalogBusy ? qsTr("正在刷新应用目录") : qsTr("刷新应用目录")
                                onClicked: window.controller.refreshApplications()
                                Keys.onReturnPressed: window.controller.refreshApplications()
                                Keys.onEnterPressed: window.controller.refreshApplications()
                            }
                        }

                        Label {
                            objectName: "catalogStatus"
                            Layout.fillWidth: true
                            text: window.controller.catalogStatus
                            textFormat: Text.PlainText
                            visible: window.controller.catalogError || window.controller.catalogBusy && applicationsList.count > 0
                            color: window.controller.catalogError ? window.theme.errorForeground : window.theme.secondary
                            wrapMode: Text.WordWrap
                            Accessible.name: text
                        }

                        Item {
                            id: catalogViewport
                            objectName: "catalogViewport"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ListView {
                                id: applicationsList

                                objectName: "applicationList"
                                anchors.fill: parent
                                enabled: !window.alphabetOpen && !folderTransition.running
                                header: Item {
                                    width: applicationsList.width
                                    height: window.applicationBrowser && window.applicationBrowser.inFolder ? 44 : 0
                                    visible: height > 0
                                    RowLayout {
                                        anchors.fill: parent
                                        IconButton {
                                            objectName: "folderBackButton"
                                            text: "‹"
                                            font.pixelSize: 24
                                            Accessible.name: qsTr("返回上一级")
                                            onClicked: window.navigateCatalogFolder("..")
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: window.applicationBrowser ? window.applicationBrowser.folderName : ""
                                            color: window.theme.foreground
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                clip: true
                                spacing: 0
                                model: window.controller.applications
                                boundsBehavior: Flickable.StopAtBounds
                                section.property: "sectionKey"
                                section.criteria: ViewSection.FullString
                                section.delegate: Item {
                                    id: catalogSection

                                    required property string section

                                    objectName: "catalogSection-" + section
                                    width: applicationsList.width
                                    height: window.tokens.layout.catalogSectionHeight

                                    Button {
                                        id: sectionButton
                                        objectName: "sectionButton-" + catalogSection.section
                                        x: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 32
                                        height: 32
                                        text: catalogSection.section
                                        font.pixelSize: 22
                                        Accessible.name: qsTr("选择应用分类 %1").arg(text)
                                        contentItem: CategoryGlyph {
                                            label: sectionButton.text
                                        }
                                        background: Rectangle {
                                            radius: 6
                                            color: sectionButton.hovered ? window.theme.hover : "transparent"
                                        }
                                        onClicked: window.alphabetOpen = true
                                    }
                                }

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
                                    HoverHandler {
                                        id: rowHover
                                    }
                                    readonly property string displayType: kind === "packaged" ? qsTr("打包应用") : qsTr("开始菜单快捷方式")

                                    objectName: "appRow-" + appId
                                    width: applicationsList.width
                                    height: window.tokens.layout.catalogRowHeight

                                    function focusApplication(): void {
                                        appButton.forceActiveFocus(Qt.TabFocusReason);
                                    }

                                    ItemDelegate {
                                        id: appButton

                                        function requestContextMenu(): void {
                                            if (appRow.kind !== "folder")
                                                window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, appButton);
                                        }

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
                                        onClicked: appRow.kind === "folder" ? window.navigateCatalogFolder(appRow.appId) : window.activateApplication(appRow.appId)
                                        Keys.onReturnPressed: appRow.kind === "folder" ? window.navigateCatalogFolder(appRow.appId) : window.activateApplication(appRow.appId)
                                        Keys.onEnterPressed: appRow.kind === "folder" ? window.navigateCatalogFolder(appRow.appId) : window.activateApplication(appRow.appId)
                                        Keys.onPressed: event => {
                                            if (event.key === Qt.Key_Menu) {
                                                requestContextMenu();
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

                                        Shortcut {
                                            objectName: "applicationContextShortcut-" + appRow.appId
                                            sequence: "Shift+F10"
                                            context: Qt.WindowShortcut
                                            autoRepeat: false
                                            enabled: window.contextShortcutsEnabled && appRow.kind !== "folder" && appButton.activeFocus && appButton.visible && appButton.enabled
                                            onActivated: appButton.requestContextMenu()
                                        }

                                        Hint {
                                            id: applicationToolTip
                                            delay: 600
                                            font.family: window.font.family

                                            visible: appButton.hovered || appButton.activeFocus
                                            text: appRow.displayName + " · " + appRow.displayType
                                            timeout: -1
                                            contentItem: Text {
                                                text: applicationToolTip.text
                                                textFormat: Text.PlainText
                                                font: applicationToolTip.font
                                                color: applicationToolTip.palette.toolTipText
                                                wrapMode: Text.Wrap
                                            }
                                        }

                                        background: Rectangle {
                                            color: appButton.down ? window.theme.pressed : appButton.hovered ? window.theme.hover : "transparent"
                                            radius: 6
                                            border.width: (appButton.activeFocus && window.controller.keyboardNavigation) ? 2 : 0
                                            border.color: window.tokens.brand.blue
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
                                                    sourceSize.width: 64
                                                    sourceSize.height: 64
                                                    asynchronous: true
                                                    cache: true
                                                    fillMode: Image.PreserveAspectFit
                                                }

                                                Label {
                                                    anchors.centerIn: parent
                                                    visible: catalogIcon.status !== Image.Ready
                                                    text: appRow.displayName.slice(0, 1)
                                                    textFormat: Text.PlainText
                                                    font.pixelSize: 22
                                                    font.bold: true
                                                    color: window.theme.foreground
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: appRow.displayName
                                                textFormat: Text.PlainText
                                                font.pixelSize: window.tokens.font.body
                                                color: window.theme.foreground
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.RightButton
                                            onClicked: {
                                                appButton.forceActiveFocus(Qt.MouseFocusReason);
                                                appButton.requestContextMenu();
                                            }
                                        }
                                    }

                                    IconButton {
                                        id: pinButton

                                        objectName: "pinButton-" + appRow.appId
                                        opacity: rowHover.hovered && appRow.kind !== "folder" ? 1 : 0
                                        focusPolicy: Qt.NoFocus
                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: 100
                                            }
                                        }
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 28
                                        height: 32
                                        icon.width: 16
                                        icon.height: 16
                                        icon.source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/pin.svg"
                                        enabled: appRow.kind !== "folder" && !window.controller.readOnly && !window.controller.importCommitting && !appRow.pinned
                                        Accessible.name: appRow.pinned ? qsTr("已固定 %1").arg(appRow.displayName) : qsTr("固定 %1").arg(appRow.displayName)
                                        onClicked: window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, pinButton)
                                        Keys.onReturnPressed: window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, pinButton)
                                        Keys.onEnterPressed: window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, pinButton)
                                    }
                                }

                                Label {
                                    objectName: "emptyCatalogHint"
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    visible: applicationsList.count === 0
                                    text: window.controller.catalogBusy ? qsTr("正在读取应用目录…") : qsTr("暂未找到可用应用。可刷新目录后重试。")
                                    textFormat: Text.PlainText
                                    color: window.theme.secondary
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Rectangle {
                                objectName: "folderOverlay"
                                anchors.fill: parent
                                z: -1
                                visible: window.applicationBrowser && window.applicationBrowser.inFolder
                                color: window.theme.opaqueFallback
                            }
                            FocusScope {
                                id: alphabetSelector
                                objectName: "alphabetSelector"
                                anchors.fill: parent
                                z: 5
                                visible: opacity > 0
                                enabled: window.alphabetOpen
                                opacity: window.alphabetOpen ? 1 : 0
                                scale: window.alphabetOpen ? 1 : 0.97
                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 130
                                    }
                                }
                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 170
                                        easing.type: Easing.OutCubic
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    color: window.theme.opaqueFallback
                                    radius: 8
                                }
                                MouseArea {
                                    anchors.fill: parent
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 12
                                    Label {
                                        text: qsTr("跳转到分类")
                                        color: window.theme.secondary
                                        font.pixelSize: 12
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: catalogViewport.height < 380 ? 6 : 4
                                        rowSpacing: 6
                                        columnSpacing: 6
                                        Repeater {
                                            id: alphabetRepeater
                                            model: ["#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "🌐"]
                                            delegate: Button {
                                                id: alphabetButton
                                                required property string modelData
                                                objectName: "alphabet-" + modelData
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 42
                                                text: modelData
                                                font.pixelSize: 20
                                                enabled: window.alphabetOpen && window.applicationBrowser && window.applicationBrowser.sections.indexOf(modelData) >= 0
                                                contentItem: CategoryGlyph {
                                                    label: alphabetButton.text
                                                    opacity: alphabetButton.enabled ? 1 : 0.25
                                                }
                                                background: Rectangle {
                                                    radius: 7
                                                    color: alphabetButton.down ? window.theme.pressed : alphabetButton.hovered ? window.theme.hover : window.theme.surface
                                                }
                                                onClicked: {
                                                    const index = window.applicationBrowser.sectionIndex(modelData);
                                                    applicationsList.positionViewAtIndex(index, ListView.Beginning);
                                                    applicationsList.currentIndex = index;
                                                    window.alphabetOpen = false;
                                                }
                                            }
                                        }
                                    }
                                    Item {
                                        Layout.fillHeight: true
                                    }
                                }
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

                                spacing: window.tokens.layout.groupSpacing

                                Repeater {
                                    id: groupRepeater

                                    model: window.controller.groups
                                    onItemRemoved: (index, item) => {
                                        const removedGroup = item as TileGroup;
                                        // Capture actual focus before this delegate is deleted. A catalog
                                        // availability update can replace it without a user edit operation.
                                        if (removedGroup && removedGroup.actionsItem.activeFocus && !window.presentationSuspended) {
                                            window.focusedGroupId = removedGroup.group.id;
                                            window.groupFocusPending = true;
                                        }
                                    }

                                    delegate: TileGroup {
                                        required property var modelData

                                        keyboardNavigation: window.controller.keyboardNavigation
                                        tileTransitions: window.controller.tileTransitions
                                        group: modelData
                                        tokens: window.tokens
                                        darkTheme: window.darkTheme
                                        titleColor: window.theme.foreground
                                        editMode: window.controller.editMode
                                        readOnly: window.controller.readOnly || window.controller.importCommitting
                                        contextShortcutsEnabled: window.contextShortcutsEnabled
                                        iconRevision: window.controller.iconRevision
                                        iconVersions: window.controller.iconVersions
                                        draggedTileId: window.draggedTileId
                                        onActionsRequested: (groupId, source) => window.openGroupMenu(groupId, source)
                                        onActionsFocused: groupId => {
                                            if (window.presentationSuspended || !window.visible || (window.groupFocusPending && groupId !== window.focusedGroupId))
                                                return;
                                            window.focusedGroupId = groupId;
                                            Qt.callLater(window.restoreGroupFocus);
                                        }
                                        onSelected: name => window.selectPreview(name)
                                        onActivated: tileId => window.activateTile(tileId)
                                        onFocused: item => {
                                            if (window.presentationSuspended || !window.visible)
                                                return;
                                            const button = item as TileButton;
                                            if (button && window.dragActive && button.tile.id !== window.draggedTileId)
                                                return;
                                            if (button)
                                                window.focusedTileId = button.tile.id;
                                            window.focusedGroupId = "";
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
                        visible: searchResultsList.count > 0
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
                            height: 56
                            padding: 8
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

                            Hint {
                                id: searchResultToolTip
                                delay: 600
                                font.family: window.font.family

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
                                color: searchRow.down ? window.theme.pressed : searchRow.hovered || searchRow.index === searchResultsList.currentIndex ? window.theme.hover : "transparent"
                                border.width: (searchRow.activeFocus && window.controller.keyboardNavigation) ? 2 : 0
                                border.color: window.tokens.brand.blue
                            }

                            contentItem: RowLayout {
                                spacing: 12

                                Item {
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32

                                    Image {
                                        id: searchIcon

                                        objectName: "searchResultIcon"
                                        anchors.fill: parent
                                        source: searchRow.modelData.iconSource ? searchRow.modelData.iconSource + "?v=" + (window.controller.iconVersions[searchRow.modelData.appId] || 0) : ""
                                        sourceSize.width: 64
                                        sourceSize.height: 64
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
            }
        }
    }

    Pane {
        id: feedbackPanel

        objectName: "feedbackPanel"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: window.shadowInset + shortcutRail.width + window.tokens.layout.contentMargin
        anchors.rightMargin: window.shadowInset + window.tokens.layout.contentMargin
        anchors.bottomMargin: window.shadowInset + window.tokens.layout.contentMargin
        z: 900
        padding: 16
        readonly property bool feedbackShown: window.showSelectionFeedback || window.showDesktopFeedback || window.showOperationFeedback || window.showPersistenceFeedback
        opacity: feedbackShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutCubic
            }
        }
        background: Rectangle {
            radius: 12
            color: window.theme.surface
            border.width: 1
            border.color: window.theme.menuBorder
            Rectangle {
                x: 1
                y: 12
                width: 3
                height: Math.max(0, parent.height - 24)
                radius: 1.5
                color: window.controller.operationError || window.controller.persistenceError ? window.theme.errorForeground : window.tokens.brand.blue
            }
        }

        contentItem: ColumnLayout {
            spacing: 4

            Label {
                id: selectionFeedback

                objectName: "selectionStatus"
                Layout.fillWidth: true
                text: window.selectionStatus
                textFormat: Text.PlainText
                visible: window.showSelectionFeedback
                color: window.selectionStatusError ? window.theme.errorForeground : window.theme.foreground
                wrapMode: Text.WordWrap
                Accessible.name: text
            }

            Label {
                id: desktopFeedback

                objectName: "desktopActionStatus"
                Layout.fillWidth: true
                text: window.controller.desktopActionStatus
                textFormat: Text.PlainText
                visible: window.showDesktopFeedback
                color: window.controller.desktopActionError ? window.theme.errorForeground : window.theme.foreground
                wrapMode: Text.WordWrap
                Accessible.name: text
            }

            Label {
                id: operationFeedback

                objectName: "operationStatus"
                Layout.fillWidth: true
                text: window.dragActive ? window.dragMessage : (window.controller.operationMessage === window.controller.desktopActionStatus ? "" : window.controller.operationMessage)
                textFormat: Text.PlainText
                visible: window.showOperationFeedback
                color: window.dragActive && !window.dragValid || window.controller.operationError ? window.theme.errorForeground : window.theme.foreground
                wrapMode: Text.WordWrap
                Accessible.name: text
            }

            Label {
                id: persistenceFeedback

                objectName: "persistenceStatus"
                Layout.fillWidth: true
                text: window.controller.persistenceStatus
                textFormat: Text.PlainText
                visible: window.showPersistenceFeedback
                color: window.controller.readOnly || window.controller.persistenceError ? window.theme.errorForeground : window.theme.secondary
                wrapMode: Text.WordWrap
                Accessible.name: text
            }
        }
    }

    ActionMenu {
        id: moreMenu

        objectName: "moreMenu"
        parent: window.contentItem
        popupType: Popup.Item
        title: qsTr("更多选项")
        ActionMenuItem {
            id: newGroupButton
            objectName: "newGroupButton"
            text: qsTr("新建分组…")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && window.controller.groups.length < window.controller.maximumGroups
            onTriggered: window.openGroupNameDialog("")
            Keys.onReturnPressed: window.openGroupNameDialog("")
            Keys.onEnterPressed: window.openGroupNameDialog("")
        }
        ActionSeparator {}
        width: 240
        onClosed: Qt.callLater(function () {
            if (window.groupFocusPending)
                window.restoreGroupFocus();
            else if (window.visible && !window.presentationSuspended && !window.internalPopupVisible)
                moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
        })

        ActionMenuItem {
            id: themeButton

            objectName: "themeButton"
            text: window.darkTheme ? qsTr("切换浅色") : qsTr("切换深色")
            enabled: !window.controller.readOnly && !window.controller.importCommitting
            Accessible.description: qsTr("切换深浅主题并保存偏好。")
            onTriggered: window.toggleTheme()
            Keys.onReturnPressed: window.toggleTheme()
            Keys.onEnterPressed: window.toggleTheme()
        }

        ActionMenuItem {
            objectName: "saveLayoutButton"
            text: window.controller.saving ? qsTr("正在保存") : qsTr("保存布局")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && !window.controller.saving && !window.dragActive
            onTriggered: window.saveLayout()
            Keys.onReturnPressed: window.saveLayout()
            Keys.onEnterPressed: window.saveLayout()
        }

        ActionSeparator {}

        ActionMenuItem {
            objectName: "importLayoutButton"
            text: qsTr("导入布局…")
            enabled: window.controller.layoutImportSupported && !window.controller.readOnly && !window.controller.importCommitting && !window.controller.importBusy && !window.dragActive
            onTriggered: window.openLayoutImport()
            Keys.onReturnPressed: window.openLayoutImport()
            Keys.onEnterPressed: window.openLayoutImport()
        }

        ActionSeparator {}

        ActionMenuItem {
            objectName: "aboutButton"
            text: qsTr("关于 XDStarter")
            onTriggered: window.openAbout()
            Keys.onReturnPressed: window.openAbout()
            Keys.onEnterPressed: window.openAbout()
        }
    }

    FileDialogs.FileDialog {
        id: layoutFileDialog

        objectName: "layoutFileDialog"
        title: qsTr("选择布局文件")
        parentWindow: window
        modality: Qt.WindowModal
        fileMode: FileDialogs.FileDialog.OpenFile
        // Keep Qt's native platform dialog preference; only request read-only file selection.
        // Qt 6.8.2 qmltypes omit the public FileDialogOptions flags; runtime tests verify ReadOnly.
        // qmllint disable missing-type missing-property
        options: FileDialogs.FileDialog.ReadOnly
        // qmllint enable missing-type missing-property
        nameFilters: [qsTr("JSON 布局 (*.json)"), qsTr("所有文件 (*)")]
        onAccepted: {
            if (window.controller.stageLayoutImport(selectedFile))
                layoutImportDialog.open();
        }
        onRejected: window.controller.cancelLayoutImport()
    }

    LayoutImportDialog {
        id: layoutImportDialog

        parent: window.contentItem
        controller: window.controller
        theme: window.theme
        tokens: window.tokens
        panelInset: window.shadowInset
        panelWidth: window.panelWidth
        panelHeight: window.panelHeight
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
        onClosed: Qt.callLater(function () {
            if (window.visible && !window.presentationSuspended && !window.internalPopupVisible)
                moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
        })

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: qsTr("关于 XDStarter")
                textFormat: Text.PlainText
                font.pixelSize: window.tokens.font.heading
                font.bold: true
                color: window.theme.foreground
            }

            Label {
                Layout.fillWidth: true
                text: window.controller.platformName + "\n" + window.controller.platformStatus
                textFormat: Text.PlainText
                color: window.theme.secondary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: window.integrationStatus
                textFormat: Text.PlainText
                visible: text.length > 0
                color: window.theme.secondary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("单击或 Enter 启动应用，右键固定或调整磁贴。编辑时可用 Ctrl + 方向键移动，Esc 取消。")
                textFormat: Text.PlainText
                color: window.theme.foreground
                wrapMode: Text.WordWrap
            }

            ActionButton {
                objectName: "closeAboutButton"
                text: qsTr("关闭")
                onClicked: aboutPopup.close()
                Keys.onReturnPressed: aboutPopup.close()
                Keys.onEnterPressed: aboutPopup.close()
            }
        }

        background: Rectangle {
            radius: window.tokens.cornerRadiusDip.menu
            color: window.theme.surface
            border.width: 1
            border.color: window.theme.controlBorder
        }
    }

    ActionMenu {
        id: groupMenu

        objectName: "groupMenu"
        title: qsTr("管理分组")
        parent: window.contentItem
        popupType: Popup.Item
        width: Math.min(300, window.panelWidth - 32)
        onClosed: Qt.callLater(window.restoreGroupFocus)

        ActionMenuItem {
            objectName: "groupRenameAction"
            text: qsTr("重命名")
            enabled: window.controller.editMode && !window.controller.readOnly && !window.controller.importCommitting && window.managedGroup !== null
            onTriggered: window.openGroupNameDialog(window.managedGroupId)
            Keys.onReturnPressed: window.openGroupNameDialog(window.managedGroupId)
            Keys.onEnterPressed: window.openGroupNameDialog(window.managedGroupId)
        }

        ActionMenuItem {
            objectName: "groupMoveUpAction"
            text: qsTr("上移分组")
            enabled: window.controller.editMode && !window.controller.readOnly && !window.controller.importCommitting && window.managedGroupIndex > 0
            onTriggered: window.moveManagedGroup(-1)
            Keys.onReturnPressed: window.moveManagedGroup(-1)
            Keys.onEnterPressed: window.moveManagedGroup(-1)
        }

        ActionMenuItem {
            objectName: "groupMoveDownAction"
            text: qsTr("下移分组")
            enabled: window.controller.editMode && !window.controller.readOnly && !window.controller.importCommitting && window.managedGroupIndex >= 0 && window.managedGroupIndex < window.controller.groups.length - 1
            onTriggered: window.moveManagedGroup(1)
            Keys.onReturnPressed: window.moveManagedGroup(1)
            Keys.onEnterPressed: window.moveManagedGroup(1)
        }

        ActionSeparator {}

        ActionMenuItem {
            objectName: "groupDeleteAction"
            text: qsTr("删除空分组")
            enabled: window.controller.editMode && !window.controller.readOnly && !window.controller.importCommitting && window.controller.groups.length > 1 && window.managedGroup !== null && window.managedGroup.tiles.length === 0
            Accessible.description: groupDeleteHint.text
            onTriggered: window.removeManagedGroup()
            Keys.onReturnPressed: window.removeManagedGroup()
            Keys.onEnterPressed: window.removeManagedGroup()
        }

        Label {
            id: groupDeleteHint

            objectName: "groupDeleteHint"
            visible: false
            width: groupMenu.width - groupMenu.leftPadding - groupMenu.rightPadding
            leftPadding: 12
            rightPadding: 12
            topPadding: 4
            bottomPadding: 8
            text: window.controller.groups.length <= 1 ? qsTr("至少保留一个分组。") : window.managedGroup && window.managedGroup.tiles.length > 0 ? qsTr("请先将磁贴移至其他分组，或取消固定，再删除此分组。") : qsTr("只删除空分组，不影响其他分组。")
            textFormat: Text.PlainText
            color: window.theme.secondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: groupNameDialog

        objectName: "groupNameDialog"
        parent: window.contentItem
        popupType: Popup.Item
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        Overlay.modal: Item {
            Rectangle {
                x: window.shadowInset
                y: window.shadowInset
                width: window.panelWidth
                height: window.panelHeight
                radius: window.tokens.cornerRadiusDip.menu
                color: Qt.rgba(0, 0, 0, 0.4)
            }
        }
        width: Math.min(420, window.panelWidth - 40)
        padding: 20
        x: (window.width - width) / 2
        y: Math.max(window.shadowInset, (window.height - height) / 2)
        title: window.namingGroupId.length > 0 ? qsTr("重命名分组") : qsTr("新建分组")
        header: Label {
            text: groupNameDialog.title
            textFormat: Text.PlainText
            leftPadding: groupNameDialog.leftPadding
            rightPadding: groupNameDialog.rightPadding
            topPadding: groupNameDialog.topPadding
            bottomPadding: 12
            font.pixelSize: window.tokens.font.heading
            font.bold: true
            color: window.theme.foreground
        }
        onOpened: {
            groupNameField.forceActiveFocus(Qt.OtherFocusReason);
            groupNameField.selectAll();
        }
        onClosed: {
            if (groupNameField.inputMethodComposing) {
                // qmllint disable missing-property
                Qt.inputMethod.reset();
                // qmllint enable missing-property
            }
            groupNameField.clear();
            window.groupNameError = "";
            Qt.callLater(window.restoreGroupFocus);
        }

        background: Rectangle {
            radius: 12
            color: window.theme.surface
            border.width: 1
            border.color: window.theme.menuBorder
            Rectangle {
                x: 1
                y: 12
                width: 3
                height: Math.max(0, parent.height - 24)
                radius: 1.5
                color: window.controller.operationError || window.controller.persistenceError ? window.theme.errorForeground : window.tokens.brand.blue
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("分组名称（最多 %1 个字符）").arg(window.controller.maximumGroupNameLength)
                textFormat: Text.PlainText
                color: window.theme.foreground
                wrapMode: Text.WordWrap
            }

            TextField {
                id: groupNameField

                objectName: "groupNameField"
                Layout.fillWidth: true
                implicitHeight: window.tokens.layout.searchHeight
                padding: 10
                maximumLength: window.controller.maximumGroupNameLength
                selectByMouse: true
                color: window.theme.foreground
                Accessible.name: qsTr("分组名称")
                Accessible.description: qsTr("输入名称后按 Enter 保存，Esc 取消。允许重名。")
                background: Rectangle {
                    radius: 6
                    color: window.theme.searchBackground
                    border.width: (groupNameField.activeFocus && window.controller.keyboardNavigation) ? 2 : 1
                    border.color: (groupNameField.activeFocus && window.controller.keyboardNavigation) ? window.tokens.brand.blue : window.theme.controlBorder
                }
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: event => {
                    if (inputMethodComposing)
                        return;
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        window.submitGroupName();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Escape) {
                        groupNameDialog.close();
                        event.accepted = true;
                    }
                }
            }

            Label {
                objectName: "groupNameError"
                Layout.fillWidth: true
                visible: window.groupNameError.length > 0
                text: window.groupNameError
                textFormat: Text.PlainText
                color: window.theme.errorForeground
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                ActionButton {
                    objectName: "groupNameCancelButton"
                    text: qsTr("取消")
                    onClicked: groupNameDialog.close()
                    Keys.onReturnPressed: groupNameDialog.close()
                    Keys.onEnterPressed: groupNameDialog.close()
                    Keys.onEscapePressed: groupNameDialog.close()
                }

                ActionButton {
                    objectName: "groupNameSaveButton"
                    text: qsTr("保存")
                    enabled: !groupNameField.inputMethodComposing && window.controller.editMode && !window.controller.readOnly && !window.controller.importCommitting
                    onClicked: window.submitGroupName()
                    Keys.onReturnPressed: window.submitGroupName()
                    Keys.onEnterPressed: window.submitGroupName()
                    Keys.onEscapePressed: groupNameDialog.close()
                }
            }
        }
    }

    Dialog {
        id: tileNameDialog

        objectName: "tileNameDialog"
        parent: window.contentItem
        popupType: Popup.Item
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        Overlay.modal: Item {
            Rectangle {
                x: window.shadowInset
                y: window.shadowInset
                width: window.panelWidth
                height: window.panelHeight
                radius: window.tokens.cornerRadiusDip.menu
                color: Qt.rgba(0, 0, 0, 0.4)
            }
        }
        width: Math.min(420, window.panelWidth - 40)
        padding: 20
        x: (window.width - width) / 2
        y: Math.max(window.shadowInset, (window.height - height) / 2)
        title: qsTr("修改磁贴显示名称")
        header: Label {
            text: tileNameDialog.title
            textFormat: Text.PlainText
            leftPadding: tileNameDialog.leftPadding
            rightPadding: tileNameDialog.rightPadding
            topPadding: tileNameDialog.topPadding
            bottomPadding: 12
            font.pixelSize: window.tokens.font.heading
            font.bold: true
            color: window.theme.foreground
        }
        onOpened: {
            tileNameField.forceActiveFocus(Qt.OtherFocusReason);
            tileNameField.selectAll();
        }
        onClosed: {
            if (tileNameField.inputMethodComposing) {
                // qmllint disable missing-property
                Qt.inputMethod.reset();
                // qmllint enable missing-property
            }
            tileNameField.clear();
            window.tileNameError = "";
            window.namingTileId = "";
            Qt.callLater(window.restoreTileFocus);
        }

        background: Rectangle {
            radius: 12
            color: window.theme.surface
            border.width: 1
            border.color: window.theme.menuBorder
            Rectangle {
                x: 1
                y: 12
                width: 3
                height: Math.max(0, parent.height - 24)
                radius: 1.5
                color: window.controller.operationError || window.controller.persistenceError ? window.theme.errorForeground : window.tokens.brand.blue
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("仅修改此磁贴的显示名称，应用名称和启动目标不变。最多 %1 个字符，表情可能占两个字符。").arg(window.controller.maximumTileDisplayNameLength)
                textFormat: Text.PlainText
                color: window.theme.foreground
                wrapMode: Text.WordWrap
            }

            TextField {
                id: tileNameField

                objectName: "tileNameField"
                Layout.fillWidth: true
                implicitHeight: window.tokens.layout.searchHeight
                padding: 10
                maximumLength: 512
                selectByMouse: true
                color: window.theme.foreground
                Accessible.name: qsTr("磁贴显示名称")
                Accessible.description: qsTr("输入名称后按 Enter 保存，Esc 取消。原名称保留为搜索别名。")
                background: Rectangle {
                    radius: 6
                    color: window.theme.searchBackground
                    border.width: (tileNameField.activeFocus && window.controller.keyboardNavigation) ? 2 : 1
                    border.color: (tileNameField.activeFocus && window.controller.keyboardNavigation) ? window.tokens.brand.blue : window.theme.controlBorder
                }
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: event => {
                    if (inputMethodComposing)
                        return;
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        window.submitTileName();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Escape) {
                        tileNameDialog.close();
                        event.accepted = true;
                    }
                }
            }

            Label {
                objectName: "tileNameError"
                Layout.fillWidth: true
                visible: window.tileNameError.length > 0
                text: window.tileNameError
                textFormat: Text.PlainText
                color: window.theme.errorForeground
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                ActionButton {
                    objectName: "tileNameCancelButton"
                    text: qsTr("取消")
                    onClicked: tileNameDialog.close()
                    Keys.onReturnPressed: tileNameDialog.close()
                    Keys.onEnterPressed: tileNameDialog.close()
                    Keys.onEscapePressed: tileNameDialog.close()
                }

                ActionButton {
                    objectName: "tileNameSaveButton"
                    text: qsTr("保存")
                    enabled: !tileNameField.inputMethodComposing && !window.controller.readOnly && !window.controller.importCommitting
                    onClicked: window.submitTileName()
                    Keys.onReturnPressed: window.submitTileName()
                    Keys.onEnterPressed: window.submitTileName()
                    Keys.onEscapePressed: tileNameDialog.close()
                }
            }
        }
    }

    ActionMenu {
        id: powerMenu

        objectName: "powerMenu"
        parent: window.contentItem
        popupType: Popup.Item
        title: qsTr("电源与退出")
        width: 240

        ActionMenuItem {
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

        ActionSeparator {}

        ActionMenuItem {
            text: qsTr("系统电源操作暂未启用")
            enabled: false
        }

        ActionSeparator {}

        ActionMenuItem {
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

    component SizeMenuItem: ActionMenuItem {
        id: sizeOption

        checkable: true
        autoExclusive: true
        enabled: !window.controller.readOnly && !window.controller.importCommitting
    }

    ActionMenu {
        id: resizeMenu

        objectName: "tileSizeMenu"
        title: qsTr("磁贴")
        width: 240
        parent: window.contentItem
        popupType: Popup.Item
        onClosed: Qt.callLater(window.restoreTileFocus)

        ActionMenuItem {
            objectName: "renameTileItem"
            text: qsTr("修改显示名称")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && window.contextTile !== null
            onTriggered: window.openTileNameDialog()
            Keys.onReturnPressed: window.openTileNameDialog()
            Keys.onEnterPressed: window.openTileNameDialog()
        }

        ActionMenuItem {
            objectName: "resetTileNameItem"
            text: qsTr("恢复原名称")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && window.contextTile !== null && window.contextTile.displayName.length > 0
            onTriggered: window.resetCurrentTileName()
            Keys.onReturnPressed: window.resetCurrentTileName()
            Keys.onEnterPressed: window.resetCurrentTileName()
        }

        ActionSeparator {}

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

        ActionSeparator {}

        ActionMenuItem {
            id: unpinOption

            objectName: "unpinTileItem"
            text: qsTr("取消固定")
            enabled: !window.controller.readOnly && !window.controller.importCommitting
            onTriggered: window.unpinCurrentTile()
            Keys.onReturnPressed: window.unpinCurrentTile()
            Keys.onEnterPressed: window.unpinCurrentTile()
        }
    }

    ActionMenu {
        id: pinMenu

        objectName: "pinApplicationMenu"
        title: qsTr("固定到分组")
        parent: window.contentItem
        popupType: Popup.Item
        width: Math.min(320, window.width - 48)

        ActionMenuItem {
            text: qsTr("选择目标分组")
            Accessible.description: qsTr("为 %1 选择固定位置").arg(window.pinApplicationName)
            enabled: false
        }

        Instantiator {
            model: window.controller.groups
            onObjectAdded: (index, object) => pinMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => pinMenu.removeItem(object)

            delegate: ActionMenuItem {
                id: pinGroupOption

                required property var modelData

                objectName: "pinToGroup-" + modelData.id
                text: modelData.name
                contentItem: Text {
                    text: pinGroupOption.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: pinGroupOption.palette.windowText
                    font: pinGroupOption.font
                    verticalAlignment: Text.AlignVCenter
                }
                Accessible.name: text
                Hint {
                    id: pinGroupToolTip
                    delay: 600
                    font.family: window.font.family

                    width: Math.min(380, implicitWidth)
                    visible: pinGroupOption.enabled && (pinGroupOption.hovered || pinGroupOption.activeFocus)
                    text: pinGroupOption.text
                    contentItem: Text {
                        text: pinGroupOption.text
                        textFormat: Text.PlainText
                        color: pinGroupToolTip.palette.toolTipText
                        font: pinGroupToolTip.font
                        wrapMode: Text.Wrap
                    }
                }
                enabled: !window.controller.readOnly && !window.controller.importCommitting && !window.pinApplicationPinned
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
        scale: window.dragActive ? 1.025 : 1
        Behavior on x {
            NumberAnimation {
                duration: 75
                easing.type: Easing.OutCubic
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: 75
                easing.type: Easing.OutCubic
            }
        }
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
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

    onSelectionStatusChanged: {
        selectionStatusError = false;
        selectionFeedbackTimer.restart();
    }

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
