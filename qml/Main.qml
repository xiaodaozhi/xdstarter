pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XDStarter

ApplicationWindow {
    id: window

    required property MenuController controller

    readonly property var tokens: controller.visualTokens
    readonly property bool highContrast: controller.highContrast
    // System color pairs are intentionally kept together. Hover and pressed
    // outlines below provide state without placing normal text on highlight.
    readonly property var theme: highContrast ? ({
        foreground: controller.highContrastColors.windowText, secondary: controller.highContrastColors.windowText,
        errorForeground: controller.highContrastColors.windowText, opaqueFallback: controller.highContrastColors.window,
        menuBorder: controller.highContrastColors.windowText, surface: controller.highContrastColors.window,
        searchBackground: controller.highContrastColors.window, hover: controller.highContrastColors.window,
        pressed: controller.highContrastColors.window, controlBorder: controller.highContrastColors.windowText,
        separator: controller.highContrastColors.windowText, disabled: controller.highContrastColors.grayText,
        focusAccent: controller.highContrastColors.windowText,
        focusControl: controller.highContrastColors.windowText, focusTileInner: controller.highContrastColors.window
    }) : tokens.themes[darkTheme ? "dark" : "light"]
    readonly property var surfaceTheme: highContrast ? ({
        card: controller.highContrastColors.window, hover: controller.highContrastColors.window, pressed: controller.highContrastColors.window,
        primary: controller.highContrastColors.windowText, onPrimary: controller.highContrastColors.window,
        disabled: controller.highContrastColors.grayText
    }) : tokens.interactionSurface.themes[darkTheme ? "dark" : "light"]
    readonly property real shadowInset: tokens.menuChrome.shadowInsetDip
    readonly property real screenGapDip: tokens.menuChrome.screenGapDip
    readonly property real panelWidth: width - 2 * shadowInset
    readonly property real panelHeight: height - 2 * shadowInset
    property bool darkTheme: controller.darkTheme
    readonly property bool animationsEnabled: controller.animationsEnabled
    property string selectionStatus: ""
    property bool selectionStatusError: false
    property string dismissedSelectionError: ""
    property string dismissedDesktopError: ""
    property string dismissedOperationError: ""
    property string dismissedPersistenceError: ""
    readonly property bool showSelectionFeedback: selectionStatus.length > 0 && selectionStatusError && selectionStatus !== dismissedSelectionError
    readonly property bool showDesktopFeedback: controller.desktopActionStatus.length > 0 && controller.desktopActionError && controller.desktopActionStatus !== dismissedDesktopError
    readonly property bool showOperationFeedback: !dragActive && controller.operationMessage.length > 0 && controller.operationMessage !== controller.desktopActionStatus && controller.operationError && controller.operationMessage !== dismissedOperationError
    readonly property bool showPersistenceFeedback: controller.persistenceStatus.length > 0 && (controller.readOnly || controller.persistenceError) && controller.persistenceStatus !== dismissedPersistenceError

    function dismissFeedback(): void {
        const restoreFocus = feedbackCloseButton.activeFocus;
        dismissedSelectionError = selectionStatus;
        dismissedDesktopError = controller.desktopActionStatus;
        dismissedOperationError = controller.operationMessage;
        dismissedPersistenceError = controller.persistenceStatus;
        if (restoreFocus && visible && !presentationSuspended && !internalPopupVisible)
            searchField.forceActiveFocus(Qt.OtherFocusReason);
    }
    property string integrationStatus: ""
    property bool winKeyCaptureAvailable: false
    property bool winKeyCaptureActive: false
    property bool winKeyCaptureBusy: false
    property string winKeyCaptureStatus: qsTr("Win 键唤起未启用。")
    property bool startButtonCaptureAvailable: false
    property bool startButtonCaptureActive: false
    property string startButtonCaptureStatus: qsTr("开始按钮使用系统入口。")
    readonly property bool compactLayout: panelWidth - shortcutRail.width - 2 * tokens.layout.contentMargin < tokens.layout.catalogWidth + tokens.layout.contentSpacing + controller.defaultGridWidth
    readonly property real tileViewportWidth: Math.max(0, panelWidth - shortcutRail.width - 2 * tokens.layout.contentMargin - (compactLayout ? 0 : tokens.layout.catalogWidth + tokens.layout.contentSpacing))
    readonly property real resizeWidthBaseDip: shortcutRail.width + 2 * tokens.layout.contentMargin + (compactLayout ? 0 : tokens.layout.catalogWidth + tokens.layout.contentSpacing) - controller.gridGap
    readonly property int resizeWidthPitchDip: controller.gridPitch
    onResizeWidthBaseDipChanged: {
        if (layoutWidthReady)
            scheduleLayoutColumns();
    }
    property bool layoutWidthPending: false
    property bool layoutWidthReady: false
    onWidthChanged: {
        if (!layoutWidthReady)
            return;
        if (dragActive)
            cancelDrag();
        scheduleLayoutColumns();
    }
    property int observedGridColumns: controller.gridColumns

    function scheduleLayoutColumns(): void {
        layoutWidthPending = true;
        layoutWidthCommit.restart();
    }

    function commitLayoutColumns(): void {
        if (!layoutWidthPending || !visible || presentationSuspended || controller.readOnly)
            return;
        if (controller.importCommitting || dragActive) {
            layoutWidthCommit.restart();
            return;
        }
        const available = tileViewportWidth;
        if (available > 0) {
            layoutWidthPending = false;
            controller.setGridColumnsForWidth(available);
        }
    }

    Timer {
        id: layoutWidthCommit
        interval: 500
        onTriggered: window.commitLayoutColumns()
    }
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
        if (!animationsEnabled) {
            finishCatalogFolderNavigation();
            return;
        }
        folderFadeOut.restart();
        folderTransition.start();
    }

    function finishCatalogFolderNavigation(): void {
        if (pendingCatalogFolder === "..")
            applicationBrowser.back();
        else
            applicationBrowser.openFolder(pendingCatalogFolder);
        applicationsList.positionViewAtBeginning();
        applicationsList.currentIndex = -1;
        if (animationsEnabled)
            folderFadeIn.restart();
        else
            applicationsList.opacity = 1;
    }

    Timer {
        id: folderTransition
        interval: 100
        onTriggered: window.finishCatalogFolderNavigation()
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
    property Item pinMenuReturnTarget: null
    property string focusedTileId: ""
    property string dismissedTileId: ""
    property bool dismissalFocusSaved: false
    property string focusedGroupId: ""
    property bool groupFocusPending: false
    property int popupFocusGeneration: 0
    property string managedGroupId: ""
    property string namingGroupId: ""
    property string namingTileId: ""
    property string tileNameError: ""
    readonly property var contextTile: tileById(resizeTileId)
    property string groupNameError: ""
    readonly property var managedGroup: controller.groups.find(group => group.id === managedGroupId) || null
    readonly property int managedGroupIndex: controller.groups.findIndex(group => group.id === managedGroupId)
    property string draggedTileId: ""
    property string draggedApplicationId: ""
    readonly property bool dragActive: draggedTileId.length > 0 || draggedApplicationId.length > 0
    property bool dragPreviewReady: false
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
    readonly property int dismissAnimationDuration: animationsEnabled ? 120 : 0
    property real revealProgress: 0
    property bool presentationStaged: false
    property int presentationGeneration: 0
    property bool presentationComponentReady: false
    property bool presentationFocusPending: false
    property bool trayRecoveryUnavailable: false
    property string trayRecoveryMessage: ""
    NumberAnimation {
        id: revealMotion
        target: window
        property: "revealProgress"
        from: 0
        to: 1
        duration: window.animationsEnabled ? 180 : 0
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: dismissMotion
        target: window
        property: "revealProgress"
        to: 0
        duration: window.animationsEnabled ? 120 : 0
        easing.type: Easing.InCubic
    }
    function animateDismissal(): void {
        revealMotion.stop();
        if (animationsEnabled)
            dismissMotion.restart();
        else
            revealProgress = 0;
    }
    onAnimationsEnabledChanged: {
        if (animationsEnabled || !presentationComponentReady)
            return;
        revealMotion.complete();
        dismissMotion.complete();
        if (folderTransition.running) {
            folderTransition.stop();
            folderFadeOut.stop();
            finishCatalogFolderNavigation();
        }
        folderFadeIn.complete();
        applicationsList.opacity = 1;
        removalMotion.stop();
    }
    property bool dismissalManaged: false
    property bool presentationSuspended: false
    readonly property bool internalPopupVisible: resizeMenu.visible || pinMenu.visible || powerMenu.visible || aboutPopup.visible || groupMenu.visible || groupNameDialog.visible || moreMenu.visible || tileNameDialog.visible || layoutFileDialog.visible || layoutImportDialog.visible
    onInternalPopupVisibleChanged: {
        if (!internalPopupVisible && presentationFocusPending)
            schedulePresentationFocus();
    }
    readonly property bool dismissalBlocked: dragActive || internalPopupVisible
    readonly property bool hintsPermitted: visible && !presentationSuspended && !internalPopupVisible && !dragActive && !alphabetOpen && !folderTransition.running
    readonly property bool contextShortcutsEnabled: visible && !presentationSuspended && !internalPopupVisible && !dragActive && !controller.searchActive && !searchField.inputMethodComposing && !alphabetOpen && !folderTransition.running
    readonly property TileButton tileContextTarget: activeFocusItem as TileButton

    signal dismissRequested
    signal winKeyCaptureToggleRequested
    signal startButtonCaptureToggleRequested
    signal trayRecoveryRetryRequested

    objectName: "prototypeWindow"
    width: tokens.defaultMenuSizeDip.width + 2 * shadowInset
    height: tokens.defaultMenuSizeDip.height + 2 * shadowInset
    minimumWidth: 480 + 2 * shadowInset
    minimumHeight: 480 + 2 * shadowInset
    visible: true
    title: qsTr("XDStarter")
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    background: MenuChrome {
        opacity: window.revealProgress
        transform: Translate {
            y: (1 - window.revealProgress) * 10
        }
        radius: window.tokens.cornerRadiusDip.menu
        color: window.theme.opaqueFallback
        railColor: window.highContrast ? window.theme.opaqueFallback : Qt.darker(window.theme.opaqueFallback, window.darkTheme ? 1.12 : 1.035)
        borderWidth: window.tokens.menuChrome.borderWidthDip
        borderColor: window.theme.menuBorder
        shadowInset: window.shadowInset
        shadowEnabled: !window.highContrast
    }

    palette.window: theme.opaqueFallback
    palette.windowText: theme.foreground
    palette.base: theme.opaqueFallback
    palette.text: theme.foreground
    palette.button: theme.surface
    palette.buttonText: theme.foreground
    palette.toolTipBase: surfaceTheme.card
    palette.toolTipText: theme.foreground
    palette.mid: theme.controlBorder
    palette.highlight: highContrast ? controller.highContrastColors.highlight : tokens.brand.blue
    palette.highlightedText: highContrast ? controller.highContrastColors.highlightText : tokens.themes.dark.foreground

    font.pixelSize: 14

    component CategoryGlyph: Item {
        id: categoryGlyph
        required property string label
        property bool disabled: false
        implicitWidth: 32
        implicitHeight: 32
        TextMetrics {
            id: categoryMetrics
            text: categoryLetter.text
            font: categoryLetter.font
        }
        Text {
            id: categoryLetter
            // Center the visible glyph, excluding line leading and side bearings.
            x: (categoryGlyph.width - categoryMetrics.tightBoundingRect.width) / 2 - categoryMetrics.tightBoundingRect.x
            y: (categoryGlyph.height - categoryMetrics.tightBoundingRect.height) / 2 - baselineOffset - categoryMetrics.tightBoundingRect.y
            text: categoryGlyph.label === "🌐" ? "" : categoryGlyph.label
            font.family: window.font.family
            font.pixelSize: 22
            color: categoryGlyph.disabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.secondary
        }
        Image {
            anchors.centerIn: parent
            width: 32
            height: 32
            visible: categoryGlyph.label === "🌐" && !window.highContrast
            source: "qrc:/xdstarter/ui/" + (window.darkTheme ? "dark" : "light") + "/globe.svg"
            sourceSize.width: 64
            sourceSize.height: 64
        }
        MenuGlyph {
            anchors.centerIn: parent
            width: 32
            height: 32
            visible: categoryGlyph.label === "🌐" && window.highContrast
            paths: window.tokens.interactionSurface.glyphs
            name: "globe"
            color: categoryGlyph.disabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.foreground
        }
    }

    component ActionButton: Button {
        id: actionButton

        implicitHeight: window.tokens.layout.controlSize
        leftPadding: 14
        rightPadding: 14
        focusPolicy: Qt.StrongFocus
        opacity: enabled || window.highContrast ? 1 : 0.45
        contentItem: Text {
            text: actionButton.text
            textFormat: Text.PlainText
            color: !actionButton.enabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.foreground
            font: actionButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: actionButton.down ? window.theme.pressed : actionButton.hovered ? window.theme.hover : window.theme.surface
            border.width: window.highContrast && actionButton.down ? 3 : (actionButton.activeFocus && window.controller.keyboardNavigation) || (window.highContrast && actionButton.hovered) ? 2 : 1
            border.color: (actionButton.activeFocus && window.controller.keyboardNavigation) ? window.theme.focusControl : window.theme.controlBorder
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
        icon.color: !enabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.foreground
        focusPolicy: Qt.StrongFocus
        opacity: enabled || window.highContrast ? 1 : 0.45
        Hint {
            contrastTheme: window.highContrast ? window.theme : null
            visible: window.hintsPermitted && iconButton.enabled && (iconButton.hovered || (window.controller.keyboardNavigation && iconButton.activeFocus)) && text.length > 0
            text: iconButton.hintText
        }
        background: Rectangle {
            radius: iconButton.cornerRadius
            color: iconButton.down ? window.theme.pressed : iconButton.checked && !window.highContrast ? Qt.alpha(window.tokens.brand.blue, 0.14) : iconButton.hovered ? window.theme.hover : "transparent"
            border.width: window.highContrast && iconButton.down ? 2 : (iconButton.activeFocus && window.controller.keyboardNavigation) || iconButton.checked || (window.highContrast && iconButton.hovered) ? 1 : 0
            border.color: window.highContrast || (iconButton.activeFocus && window.controller.keyboardNavigation) ? window.theme.focusControl : window.tokens.brand.blue
        }
    }

    component ActionMenu: Menu {
        padding: 6
        overlap: -8
        font.family: window.font.family
        palette.highlight: window.surfaceTheme.hover
        palette.highlightedText: window.theme.foreground
        delegate: ActionMenuItem {}
        enter: Transition {
            enabled: window.animationsEnabled
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 120
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.98
                    to: 1
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }
        exit: Transition {
            enabled: window.animationsEnabled
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: 90
                }
                NumberAnimation {
                    property: "scale"
                    to: 0.98
                    duration: 90
                }
            }
        }
        background: FloatingSurface {
            radius: window.tokens.interactionSurface.menuRadiusDip
            color: window.surfaceTheme.card
            borderColor: window.theme.controlBorder
            darkTheme: window.darkTheme
            shadowEnabled: !window.highContrast
        }
    }

    component ActionMenuItem: MenuItem {
        id: menuAction
        hoverEnabled: true
        property string actionIcon: subMenu !== null ? (subMenu.objectName === "tileDimensionsMenu" ? "tile-size" : subMenu.objectName === "appearanceMenu" ? "theme" : "") : ""
        objectName: subMenu !== null ? (subMenu.objectName === "appearanceMenu" ? "themeButton" : subMenu.objectName + "Item") : ""
        font.family: window.font.family
        palette.highlight: window.surfaceTheme.hover
        palette.highlightedText: window.theme.foreground
        implicitHeight: 36
        leftPadding: 38
        rightPadding: 32
        indicator: MenuGlyph {
            objectName: "menuLeadingGlyph"
            paths: window.tokens.interactionSurface.glyphs
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: menuAction.checkable ? (menuAction.checked ? "check" : "") : menuAction.actionIcon
            color: menuAction.enabled ? window.theme.foreground : window.surfaceTheme.disabled
        }
        arrow: MenuGlyph {
            paths: window.tokens.interactionSurface.glyphs
            name: "chevron-right"
            visible: menuAction.subMenu !== null
            width: 14
            height: 14
            x: menuAction.width - width - 13
            anchors.verticalCenter: parent.verticalCenter
            color: menuAction.enabled ? window.theme.foreground : window.surfaceTheme.disabled
        }
        contentItem: Text {
            text: menuAction.text
            textFormat: Text.PlainText
            font: menuAction.font
            color: menuAction.enabled ? window.theme.foreground : window.surfaceTheme.disabled
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: window.tokens.interactionSurface.rowRadiusDip
            color: !menuAction.enabled ? "transparent" : menuAction.down ? window.surfaceTheme.pressed : menuAction.highlighted || menuAction.hovered ? window.surfaceTheme.hover : "transparent"
            border.width: (menuAction.activeFocus && window.controller.keyboardNavigation) || (window.highContrast && menuAction.enabled && (menuAction.hovered || menuAction.highlighted || menuAction.down)) ? 1 : 0
            border.color: window.surfaceTheme.primary
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
        pinMenuReturnTarget = source;
        const position = source.mapToItem(contentItem, 0, source.height);
        pinMenu.x = Math.max(shadowInset, Math.min(position.x, width - shadowInset - pinMenu.width));
        pinMenu.y = Math.max(shadowInset, Math.min(position.y, height - shadowInset - pinMenu.height));
        pinMenu.open();
    }

    function restoreApplicationMenuFocus(): void {
        const target = pinMenuReturnTarget;
        pinMenuReturnTarget = null;
        if (!visible || presentationSuspended || internalPopupVisible || dragActive)
            return;
        const focused = activeFocusItem;
        // Preserve newer focus even if the source disappeared or another
        // queued close already consumed the return target. A catalog whose
        // focused delegate disappeared leaves only the ListView focus scope.
        if (focused && focused !== contentItem && focused !== applicationsList && focused !== target && focused.visible && focused.enabled)
            return;
        if (target && target.visible && target.enabled)
            target.forceActiveFocus(Qt.OtherFocusReason);
        else
            moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
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
        } else if (animationsEnabled) {
            removalMotion.restart();
        }
    }

    function pinCurrentApplication(): void {
        pinMenu.close();
        if (controller.isApplicationPinned(pinApplicationId))
            controller.unpinApplication(pinApplicationId);
        else
            controller.pinApplication(pinApplicationId);
    }

    function ensureTileVisible(tileItem: Item): void {
        groupsColumn.forceLayout();
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

    function restoreTileContextFocus(): void {
        if (!visible || presentationSuspended || internalPopupVisible || dragActive || controller.searchActive)
            return;
        if (resizeTileId.length > 0 && !tileById(resizeTileId)) {
            focusedTileId = "";
            moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
        } else {
            restoreTileFocus();
        }
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
        // Explicit search supersedes older popup and group-focus restores.
        ++popupFocusGeneration;
        groupFocusPending = false;
        pinMenuReturnTarget = null;
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

    function selectTheme(preference: string): void {
        moreMenu.close();
        if (!controller.readOnly)
            controller.themePreference = preference;
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
        presentationFocusPending = false;
        if (!presentationSuspended) {
            ++presentationGeneration;
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

    function stagePresentation(): void {
        presentationFocusPending = false;
        ++presentationGeneration;
        dismissMotion.stop();
        revealMotion.stop();
        revealProgress = 0;
        presentationStaged = true;
        presentationSuspended = true;
        closeInternalPopups();
        cancelDrag();
    }

    function restorePresentationFocus(): void {
        const resumingDismissal = presentationSuspended;
        dismissMotion.stop();
        revealMotion.stop();
        revealProgress = 1;
        presentationStaged = false;
        presentationSuspended = false;
        presentationFocusPending = resumingDismissal;
        if (!internalPopupVisible)
            searchField.forceActiveFocus(Qt.OtherFocusReason);
        if (presentationFocusPending)
            schedulePresentationFocus();
    }

    function schedulePresentationFocus(): void {
        const generation = presentationGeneration;
        Qt.callLater(function () {
            if (window.presentationFocusPending && generation === window.presentationGeneration && window.visible && !window.presentationSuspended && !window.internalPopupVisible) {
                window.presentationFocusPending = false;
                searchField.forceActiveFocus(Qt.OtherFocusReason);
            }
        });
    }

    function queuePopupFocusRestore(action: var): void {
        // A popup's visibleChanged precedes its closed signal. Presentation
        // focus wins over callbacks queued by the old popup during dismissal.
        if (presentationSuspended || presentationFocusPending)
            return;
        const generation = presentationGeneration;
        const focusGeneration = popupFocusGeneration;
        Qt.callLater(function () {
            if (generation === window.presentationGeneration && focusGeneration === window.popupFocusGeneration && window.visible && !window.presentationSuspended && !window.presentationFocusPending && !window.internalPopupVisible)
                action();
        });
    }

    function restoreMoreMenuFocus(): void {
        if (groupFocusPending)
            restoreGroupFocus();
        else if (visible && !presentationSuspended && !internalPopupVisible) {
            const focused = activeFocusItem;
            // A queued close must not reclaim focus after a later interaction
            // has already moved it to a visible, enabled control.
            if (!focused || focused === contentItem || focused === moreMenuButton || !focused.visible || !focused.enabled)
                moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
        }
    }

    function prepareForPresentation(): void {
        if (!visible)
            return;
        if (!presentationStaged) {
            if (presentationSuspended)
                restorePresentationFocus();
            return;
        }
        presentationStaged = false;
        presentationSuspended = false;
        if (animationsEnabled)
            revealMotion.restart();
        else
            revealProgress = 1;
        searchField.forceActiveFocus(Qt.OtherFocusReason);
        presentationFocusPending = true;
        schedulePresentationFocus();
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
        dragPreviewReady = false;
        draggedTileId = tileId;
        focusedTileId = tileId;
        dragOffsetX = offsetX;
        dragOffsetY = offsetY;
        draggedWidth = tileWidth;
        draggedHeight = tileHeight;
        updateDrag(sceneX, sceneY);
        dragPreviewReady = true;
    }

    function startApplicationDrag(applicationId: string, sceneX: real, sceneY: real): void {
        if (controller.readOnly || controller.importCommitting || controller.isApplicationPinned(applicationId))
            return;
        dragPreviewReady = false;
        draggedApplicationId = applicationId;
        focusedTileId = "";
        draggedWidth = controller.previewApplicationPlacement(applicationId, "", 0, 0).width;
        draggedHeight = draggedWidth;
        dragOffsetX = draggedWidth / 2;
        dragOffsetY = draggedHeight / 2;
        updateDrag(sceneX, sceneY);
        dragPreviewReady = true;
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
            const placement = draggedApplicationId.length > 0 ? controller.previewApplicationPlacement(draggedApplicationId, dragTargetGroupId, dragColumn, dragRow) : controller.previewPlacement(draggedTileId, dragTargetGroupId, dragColumn, dragRow);
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
        const applicationId = draggedApplicationId;
        const targetId = dragTargetGroupId;
        const valid = dragValid;
        const column = dragColumn;
        const row = dragRow;
        const message = dragMessage;
        clearDragPreview();
        if (valid) {
            if (applicationId.length > 0) {
                if (controller.pinApplicationAt(applicationId, targetId, column, row) && compactLayout)
                    showApplications = false;
            } else
                controller.moveTile(tileId, targetId, column, row);
        }
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
        dragPreviewReady = false;
        dragValid = false;
        dragTargetGroupId = "";
        dragMessage = "";
        previewX = 0;
        previewY = 0;
        previewWidth = 0;
        previewHeight = 0;
        draggedTileId = "";
        draggedApplicationId = "";
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
            if (window.observedGridColumns !== window.controller.gridColumns) {
                window.cancelDrag();
                window.observedGridColumns = window.controller.gridColumns;
            }
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
            window.dismissedSelectionError = "";
            window.selectionStatus = qsTr("启动失败：%1").arg(message);
            window.selectionStatusError = true;
        }

        function onOperationMessageChanged(): void {
            if (!window.controller.operationError)
                window.dismissedOperationError = "";
            if (window.visible && !window.presentationSuspended)
                operationFeedbackTimer.restart();
        }

        function onPersistenceStatusChanged(): void {
            if (!window.controller.readOnly && !window.controller.persistenceError)
                window.dismissedPersistenceError = "";
            if (window.visible && !window.presentationSuspended)
                persistenceFeedbackTimer.restart();
        }

        function onDesktopActionStateChanged(): void {
            if (!window.controller.desktopActionError)
                window.dismissedDesktopError = "";
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
            hintsEnabled: window.hintsPermitted

            Layout.preferredWidth: window.tokens.layout.railWidth
            Layout.fillHeight: true
            darkTheme: window.darkTheme
            tokens: window.tokens
            themeOverride: window.theme
            highContrast: window.highContrast
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
                            border.color: (searchField.activeFocus && window.controller.keyboardNavigation) ? window.theme.focusControl : window.theme.controlBorder
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
                            visible: !window.highContrast
                            Accessible.ignored: true
                        }
                        MenuGlyph {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            visible: window.highContrast
                            paths: window.tokens.interactionSurface.glyphs
                            name: "search"
                            color: window.theme.foreground
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
                        visible: !window.compactLayout || window.showApplications || window.draggedApplicationId.length > 0
                        Layout.preferredWidth: window.compactLayout ? (window.draggedApplicationId.length > 0 ? 96 : -1) : window.tokens.layout.catalogWidth
                        Layout.maximumWidth: window.compactLayout ? (window.draggedApplicationId.length > 0 ? 96 : Number.POSITIVE_INFINITY) : window.tokens.layout.catalogWidth
                        Layout.fillWidth: window.compactLayout && window.draggedApplicationId.length === 0
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
                            Item {
                                id: folderHeader
                                objectName: "catalogFolderHeader"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: window.applicationBrowser && window.applicationBrowser.inFolder ? 44 : 0
                                visible: height > 0
                                RowLayout {
                                    anchors.fill: parent
                                    IconButton {
                                        objectName: "folderBackButton"
                                        contentItem: Item {
                                            MenuGlyph {
                                                objectName: "folderBackIcon"
                                                paths: window.tokens.interactionSurface.glyphs
                                                name: "chevron-left"
                                                color: window.theme.foreground
                                                anchors.centerIn: parent
                                            }
                                        }
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
                            ListView {
                                id: applicationsList

                                objectName: "applicationList"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: folderHeader.bottom
                                anchors.bottom: parent.bottom
                                enabled: !window.alphabetOpen && !folderTransition.running
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
                                        padding: 0
                                        text: catalogSection.section
                                        font.pixelSize: 22
                                        Accessible.name: qsTr("选择应用分类 %1").arg(text)
                                        contentItem: CategoryGlyph {
                                            label: sectionButton.text
                                        }
                                        background: Rectangle {
                                            radius: 6
                                            color: sectionButton.hovered ? window.theme.hover : "transparent"
                                            border.width: window.highContrast && (sectionButton.hovered || sectionButton.activeFocus) ? 1 : 0
                                            border.color: window.theme.focusControl
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
                                    readonly property string displayType: kind === "folder" ? qsTr("文件夹") : kind === "packaged" ? qsTr("打包应用") : qsTr("开始菜单快捷方式")

                                    objectName: "appRow-" + appId
                                    width: applicationsList.width
                                    height: window.tokens.layout.catalogRowHeight

                                    function focusApplication(): void {
                                        appButton.forceActiveFocus(Qt.TabFocusReason);
                                    }

                                    ItemDelegate {
                                        id: appButton
                                        hoverEnabled: true

                                        function requestContextMenu(): void {
                                            if (appRow.kind !== "folder")
                                                window.openPinMenu(appRow.appId, appRow.displayName, appRow.pinned, appButton);
                                        }

                                        objectName: "applicationLaunch-" + appRow.appId
                                        focus: true
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        enabled: appRow.available !== false
                                        padding: 6
                                        Accessible.name: appRow.displayName
                                        Accessible.description: appRow.kind === "folder" ? qsTr("文件夹。按 Enter 展开。") : qsTr("%1。按 Enter 启动，拖动或按 Shift 加 F10 管理固定磁贴。").arg(appRow.displayType)
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
                                            contrastTheme: window.highContrast ? window.theme : null
                                            objectName: "applicationToolTip-" + appRow.appId
                                            delay: 600
                                            font.family: window.font.family

                                            visible: window.visible && !window.presentationSuspended && !window.internalPopupVisible && !window.alphabetOpen && !window.dragActive && !folderTransition.running && (appButton.hovered || (appButton.activeFocus && window.controller.keyboardNavigation))
                                            text: appRow.displayName + "\n" + appRow.displayType
                                            timeout: -1
                                            contentItem: Text {
                                                text: applicationToolTip.text
                                                textFormat: Text.PlainText
                                                font: applicationToolTip.font
                                                color: applicationToolTip.foregroundColor
                                                wrapMode: Text.Wrap
                                            }
                                        }

                                        background: Rectangle {
                                            color: appButton.down ? window.theme.pressed : appButton.hovered ? window.theme.hover : "transparent"
                                            radius: 6
                                            border.width: (appButton.activeFocus && window.controller.keyboardNavigation) || (window.highContrast && appButton.down) ? 2 : window.highContrast && appButton.hovered ? 1 : 0
                                            border.color: window.theme.focusControl
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
                                                    visible: !window.highContrast
                                                }

                                                Label {
                                                    objectName: "catalogIconFallback"
                                                    anchors.centerIn: parent
                                                    visible: window.highContrast || catalogIcon.status !== Image.Ready
                                                    text: window.highContrast && appRow.displayName.length > 0 ? String.fromCodePoint(appRow.displayName.codePointAt(0)) : appRow.displayName.slice(0, 1)
                                                    textFormat: Text.PlainText
                                                    font.pixelSize: 22
                                                    font.bold: true
                                                    color: !appButton.enabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.foreground
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: appRow.displayName
                                                textFormat: Text.PlainText
                                                font.pixelSize: window.tokens.font.body
                                                color: !appButton.enabled && window.highContrast ? window.controller.highContrastColors.grayText : window.theme.foreground
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            id: applicationPointer
                                            property point pressPosition: Qt.point(0, 0)
                                            property bool suppressClick: false
                                            anchors.fill: parent
                                            acceptedButtons: Qt.RightButton | (appRow.kind !== "folder" && !window.controller.readOnly && !window.controller.importCommitting ? Qt.LeftButton : Qt.NoButton)
                                            preventStealing: appRow.kind !== "folder"
                                            onPressed: mouse => {
                                                appButton.forceActiveFocus(Qt.MouseFocusReason);
                                                pressPosition = Qt.point(mouse.x, mouse.y);
                                                suppressClick = false;
                                            }
                                            onPositionChanged: mouse => {
                                                if (!(pressedButtons & Qt.LeftButton))
                                                    return;
                                                const dx = mouse.x - pressPosition.x;
                                                const dy = mouse.y - pressPosition.y;
                                                const position = appButton.mapToItem(null, mouse.x, mouse.y);
                                                if (!suppressClick && dx * dx + dy * dy >= drag.threshold * drag.threshold) {
                                                    suppressClick = true;
                                                    window.startApplicationDrag(appRow.appId, position.x, position.y);
                                                } else if (window.draggedApplicationId === appRow.appId) {
                                                    window.updateDrag(position.x, position.y);
                                                }
                                            }
                                            onReleased: mouse => {
                                                if (mouse.button !== Qt.LeftButton)
                                                    return;
                                                if (window.draggedApplicationId === appRow.appId) {
                                                    const position = appButton.mapToItem(null, mouse.x, mouse.y);
                                                    window.updateDrag(position.x, position.y);
                                                    window.finishDrag();
                                                } else if (!suppressClick) {
                                                    window.activateApplication(appRow.appId);
                                                }
                                            }
                                            onClicked: mouse => {
                                                if (mouse.button === Qt.RightButton)
                                                    appButton.requestContextMenu();
                                            }
                                            onCanceled: {
                                                if (window.draggedApplicationId === appRow.appId)
                                                    window.cancelDrag();
                                            }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    width: Math.max(0, parent.width - 24)
                                    spacing: 10
                                    visible: applicationsList.count === 0
                                    MenuGlyph {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 36
                                        Layout.preferredHeight: 36
                                        paths: window.tokens.interactionSurface.glyphs
                                        name: "folder"
                                        color: window.theme.secondary
                                        visible: !window.controller.catalogBusy && window.applicationBrowser && window.applicationBrowser.inFolder
                                    }
                                    Label {
                                        objectName: "emptyCatalogHint"
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: window.controller.catalogBusy ? qsTr("正在读取应用目录…") : window.applicationBrowser && window.applicationBrowser.inFolder ? qsTr("此文件夹为空。") : qsTr("暂未找到可用应用。可刷新目录后重试。")
                                        textFormat: Text.PlainText
                                        color: window.theme.secondary
                                        wrapMode: Text.WordWrap
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        visible: !window.controller.catalogBusy && window.applicationBrowser && window.applicationBrowser.inFolder
                                        text: qsTr("使用上方返回按钮查看全部应用。")
                                        color: window.theme.secondary
                                        wrapMode: Text.WordWrap
                                    }
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
                                readonly property real targetOpacity: window.alphabetOpen ? 1 : 0
                                readonly property real targetScale: window.alphabetOpen ? 1 : 0.97
                                opacity: targetOpacity
                                scale: targetScale
                                Behavior on opacity {
                                    enabled: window.animationsEnabled
                                    onEnabledChanged: {
                                        if (!enabled)
                                            alphabetSelector.opacity = Qt.binding(function () { return alphabetSelector.targetOpacity; });
                                    }
                                    NumberAnimation {
                                        duration: 130
                                    }
                                }
                                Behavior on scale {
                                    enabled: window.animationsEnabled
                                    onEnabledChanged: {
                                        if (!enabled)
                                            alphabetSelector.scale = Qt.binding(function () { return alphabetSelector.targetScale; });
                                    }
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
                                                padding: 0
                                                text: modelData
                                                font.pixelSize: 20
                                                enabled: window.alphabetOpen && window.applicationBrowser && window.applicationBrowser.sections.indexOf(modelData) >= 0
                                                contentItem: CategoryGlyph {
                                                    label: alphabetButton.text
                                                    disabled: !alphabetButton.enabled
                                                    opacity: alphabetButton.enabled || window.highContrast ? 1 : 0.25
                                                }
                                                background: Rectangle {
                                                    radius: 7
                                                    color: alphabetButton.down ? window.theme.pressed : alphabetButton.hovered ? window.theme.hover : window.theme.surface
                                                    border.width: window.highContrast && (alphabetButton.hovered || alphabetButton.activeFocus || alphabetButton.down) ? 1 : 0
                                                    border.color: window.theme.foreground
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
                        // RowLayout owns the viewport width. Its size hint must
                        // not come back from the viewport-dependent content.
                        implicitWidth: 0
                        Layout.minimumWidth: groupVerticalScrollBar.width
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // Keep the content aligned with the toolbar. The bar
                        // occupies the existing outer content margin instead
                        // of permanently subtracting its width from the grid.
                        Layout.rightMargin: -groupVerticalScrollBar.width
                        visible: !window.compactLayout || !window.showApplications || window.draggedApplicationId.length > 0
                        contentWidth: groupsFlickable.contentWidth
                        contentHeight: groupsFlickable.contentHeight
                        clip: true
                        focusPolicy: Qt.NoFocus
                        rightPadding: groupVerticalScrollBar.width
                        bottomPadding: effectiveScrollBarHeight

                        ScrollBar.horizontal: ScrollBar {
                            parent: groupsScroll
                            x: groupsScroll.leftPadding
                            y: groupsScroll.height - height
                            width: groupsScroll.availableWidth
                            policy: ScrollBar.AsNeeded
                            focusPolicy: Qt.NoFocus
                        }
                        ScrollBar.vertical: ScrollBar {
                            id: groupVerticalScrollBar
                            objectName: "groupVerticalScrollBar"
                            parent: groupsScroll
                            x: groupsScroll.width - width
                            y: groupsScroll.topPadding
                            height: groupsScroll.availableHeight
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

                                objectName: "groupsColumn"
                                width: Math.max(window.tileViewportWidth, window.controller.previewWidth)
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
                                        hintsEnabled: window.hintsPermitted
                                        animationsEnabled: window.animationsEnabled
                                        required property var modelData

                                        width: groupsColumn.width
                                        keyboardNavigation: window.controller.keyboardNavigation
                                        tileTransitions: window.controller.tileTransitions
                                        group: modelData
                                        tokens: window.tokens
                                        darkTheme: window.darkTheme
                                        themeOverride: window.theme
                                        highContrast: window.highContrast
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
                            opacity: modelData.available === false && !window.highContrast ? 0.65 : 1
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
                                contrastTheme: window.highContrast ? window.theme : null
                                delay: 600
                                font.family: window.font.family

                                objectName: "searchResultToolTip"
                                visible: window.hintsPermitted && (searchRow.hovered || (window.controller.keyboardNavigation && searchRow.activeFocus))
                                text: searchRow.modelData.displayName + "\n" + (searchRow.modelData.available === false ? searchRow.modelData.unavailableReason : searchRow.displayType)
                                timeout: -1
                                contentItem: Text {
                                    text: searchResultToolTip.text
                                    textFormat: Text.PlainText
                                    font: searchResultToolTip.font
                                    wrapMode: Text.Wrap
                                    color: searchResultToolTip.foregroundColor
                                }
                            }

                            background: Rectangle {
                                radius: 6
                                color: searchRow.down ? window.theme.pressed : searchRow.hovered || searchRow.index === searchResultsList.currentIndex ? window.theme.hover : "transparent"
                                border.width: (searchRow.activeFocus && window.controller.keyboardNavigation) || (window.highContrast && searchRow.down) ? 2 : window.highContrast && (searchRow.hovered || searchRow.index === searchResultsList.currentIndex) ? 1 : 0
                                border.color: window.theme.focusControl
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
                                        visible: !window.highContrast
                                    }

                                    Label {
                                        objectName: "searchIconFallback"
                                        anchors.centerIn: parent
                                        visible: window.highContrast || searchIcon.status !== Image.Ready
                                        text: window.highContrast && searchRow.modelData.displayName.length > 0 ? String.fromCodePoint(searchRow.modelData.displayName.codePointAt(0)) : searchRow.modelData.displayName.slice(0, 1)
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
                                        text: searchRow.modelData.available === false ? searchRow.modelData.unavailableReason : searchRow.displayType + (searchRow.modelData.pinned ? qsTr("，已固定") : "")
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
        leftPadding: 48
        rightPadding: 44
        readonly property bool feedbackShown: !window.trayRecoveryUnavailable && !window.internalPopupVisible && (window.showSelectionFeedback || window.showDesktopFeedback || window.showOperationFeedback || window.showPersistenceFeedback)
        onFeedbackShownChanged: {
            if (!feedbackShown && feedbackCloseButton.activeFocus && window.visible && !window.presentationSuspended && !window.internalPopupVisible)
                searchField.forceActiveFocus(Qt.OtherFocusReason);
        }
        readonly property real targetOpacity: feedbackShown ? 1 : 0
        opacity: targetOpacity
        visible: opacity > 0
        Behavior on opacity {
            enabled: window.animationsEnabled
            onEnabledChanged: {
                if (!enabled)
                    feedbackPanel.opacity = Qt.binding(function () { return feedbackPanel.targetOpacity; });
            }
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutCubic
            }
        }
        background: FloatingSurface {
            radius: 8
            color: window.surfaceTheme.card
            borderColor: window.theme.controlBorder
            darkTheme: window.darkTheme
            shadowEnabled: !window.highContrast
            MenuGlyph {
                x: 16
                y: 18
                width: 22
                height: 22
                paths: window.tokens.interactionSurface.glyphs
                name: "error"
                color: window.theme.errorForeground
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
        Button {
            id: feedbackCloseButton
            objectName: "feedbackCloseButton"
            enabled: feedbackPanel.feedbackShown
            parent: feedbackPanel
            x: parent.width - width - 8
            y: 8
            width: 28
            height: 28
            padding: 6
            Accessible.name: qsTr("关闭提示")
            onClicked: window.dismissFeedback()
            Keys.onReturnPressed: window.dismissFeedback()
            Keys.onEnterPressed: window.dismissFeedback()
            background: Rectangle { radius: 4; color: feedbackCloseButton.hovered ? window.surfaceTheme.hover : "transparent"; border.width: window.highContrast && (feedbackCloseButton.hovered || feedbackCloseButton.activeFocus) ? 1 : 0; border.color: window.theme.foreground }
            contentItem: MenuGlyph { paths: window.tokens.interactionSurface.glyphs; name: "close"; color: window.theme.secondary }
        }
    }

    Pane {
        objectName: "trayRecoveryPanel"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: window.shadowInset + 24
        anchors.rightMargin: window.shadowInset + 24
        anchors.bottomMargin: window.shadowInset + 24
        z: 950
        visible: window.trayRecoveryUnavailable
        padding: 20
        background: FloatingSurface {
            color: window.surfaceTheme.card
            borderColor: window.theme.controlBorder
            darkTheme: window.darkTheme
            shadowEnabled: !window.highContrast
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: qsTr("托盘图标暂时不可用")
                font.bold: true
                color: window.theme.foreground
            }
            Label {
                Layout.fillWidth: true
                text: window.trayRecoveryMessage
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: window.theme.secondary
            }
            RowLayout {
                ActionButton { objectName: "retryTrayButton"; text: qsTr("重试托盘"); onClicked: window.trayRecoveryRetryRequested() }
                ActionButton { objectName: "recoveryExitButton"; text: qsTr("退出 XDStarter"); onClicked: window.controller.requestExit() }
            }
        }
    }

    ActionMenu {
        id: moreMenu

        objectName: "moreMenu"
        parent: window.contentItem
        popupType: Popup.Item
        title: qsTr("更多选项")
        ActionMenu {
            id: appearanceMenu
            objectName: "appearanceMenu"
            title: qsTr("外观主题")
            width: 204
            enabled: !window.controller.readOnly && !window.controller.importCommitting
            ActionMenuItem {
                objectName: "appearanceSystemItem"
                text: qsTr("跟随系统")
                checkable: true
                autoExclusive: true
                checked: window.controller.themePreference === "system"
                onTriggered: window.selectTheme("system")
            }
            ActionMenuItem {
                objectName: "appearanceLightItem"
                text: qsTr("浅色")
                checkable: true
                autoExclusive: true
                checked: window.controller.themePreference === "light"
                onTriggered: window.selectTheme("light")
            }
            ActionMenuItem {
                objectName: "appearanceDarkItem"
                text: qsTr("深色")
                checkable: true
                autoExclusive: true
                checked: window.controller.themePreference === "dark"
                onTriggered: window.selectTheme("dark")
            }
        }

        ActionMenuItem {
            id: newGroupButton
            objectName: "newGroupButton"
            actionIcon: "add"
            text: qsTr("新建分组…")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && window.controller.groups.length < window.controller.maximumGroups
            onTriggered: window.openGroupNameDialog("")
            Keys.onReturnPressed: window.openGroupNameDialog("")
            Keys.onEnterPressed: window.openGroupNameDialog("")
        }
        ActionSeparator {}


        width: 240
        onClosed: window.queuePopupFocusRestore(window.restoreMoreMenuFocus)



        ActionMenuItem {
            objectName: "saveLayoutButton"
            actionIcon: "save"
            text: window.controller.saving ? qsTr("正在保存") : qsTr("保存布局")
            enabled: !window.controller.readOnly && !window.controller.importCommitting && !window.controller.saving && !window.dragActive
            onTriggered: window.saveLayout()
            Keys.onReturnPressed: window.saveLayout()
            Keys.onEnterPressed: window.saveLayout()
        }

        ActionSeparator {}

        ActionMenuItem {
            objectName: "importLayoutButton"
            actionIcon: "import"
            text: qsTr("导入布局…")
            enabled: window.controller.layoutImportSupported && !window.controller.readOnly && !window.controller.importCommitting && !window.controller.importBusy && !window.dragActive
            onTriggered: window.openLayoutImport()
            Keys.onReturnPressed: window.openLayoutImport()
            Keys.onEnterPressed: window.openLayoutImport()
        }

        ActionSeparator {}

        ActionMenuItem {
            objectName: "aboutButton"
            actionIcon: "info"
            text: qsTr("关于 XDStarter")
            onTriggered: window.openAbout()
            Keys.onReturnPressed: window.openAbout()
            Keys.onEnterPressed: window.openAbout()
        }
    }

    LayoutFilePicker {
        id: layoutFileDialog

        objectName: "layoutFileDialog"
        title: qsTr("选择布局文件")
        parentWindow: window
        onAccepted: {
            if (window.controller.stageLayoutImport(selectedFile))
                layoutImportDialog.open();
        }
        onRejected: window.controller.cancelLayoutImport()
        onFailed: {
            window.controller.cancelLayoutImport();
            window.dismissedSelectionError = "";
            window.selectionStatus = qsTr("无法打开布局文件选择窗口，请重试。");
            window.selectionStatusError = true;
        }
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
        width: Math.min(448, window.panelWidth - 48)
        padding: 24
        focus: true
        modal: true
        Overlay.modal: Item {
            Rectangle {
                x: window.shadowInset
                y: window.shadowInset
                width: window.panelWidth
                height: window.panelHeight
                radius: window.tokens.cornerRadiusDip.menu
                color: window.highContrast ? "transparent" : Qt.rgba(0, 0, 0, 0.4)
            }
        }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: window.queuePopupFocusRestore(function () {
            if (window.visible && !window.presentationSuspended && !window.internalPopupVisible)
                moreMenuButton.forceActiveFocus(Qt.OtherFocusReason);
        })

        contentItem: ColumnLayout {
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                spacing: 20
                Image {
                    source: "qrc:/xdstarter/app/xdstarter-app-icon.svg"
                    visible: !window.highContrast
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    sourceSize.width: 104
                    sourceSize.height: 104
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: "XDStarter"; font.pixelSize: 24; font.bold: true; color: window.theme.foreground }
                    Label { text: qsTr("Windows 磁贴开始菜单"); color: window.theme.secondary; font.pixelSize: 13 }
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: aboutDismissButton
                    objectName: "aboutDismissButton"
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    padding: 6
                    Accessible.name: qsTr("关闭关于")
                    onClicked: aboutPopup.close()
                    background: Rectangle { radius: 4; color: aboutDismissButton.down ? window.surfaceTheme.pressed : aboutDismissButton.hovered ? window.surfaceTheme.hover : "transparent"; border.width: window.highContrast && (aboutDismissButton.hovered || aboutDismissButton.activeFocus) ? 1 : 0; border.color: window.theme.foreground }
                    contentItem: MenuGlyph { paths: window.tokens.interactionSurface.glyphs; name: "close"; color: window.theme.secondary }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.theme.separator }
            RowLayout {
                Label { text: qsTr("版本"); color: window.theme.secondary }
                Item { Layout.fillWidth: true }
                Label { objectName: "aboutVersion"; text: Qt.application.version.length > 0 ? qsTr("%1（开发版）").arg(Qt.application.version) : qsTr("开发版"); color: window.theme.foreground }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("通过拖动和右键管理常用应用。\n默认在后台运行，可从托盘管理。")
                textFormat: Text.PlainText
                color: window.theme.foreground
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 46
                radius: 6
                color: window.surfaceTheme.hover
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    MenuGlyph {
                        paths: window.tokens.interactionSurface.glyphs
                        name: window.startButtonCaptureActive ? "check" : "info"
                        color: window.surfaceTheme.primary
                    }
                    Label { text: qsTr("开始按钮接管"); color: window.theme.foreground; font.pixelSize: 13 }
                    Item { Layout.fillWidth: true }
                    Label { text: window.startButtonCaptureActive ? qsTr("已启用") : qsTr("未启用"); color: window.theme.secondary; font.pixelSize: 13 }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.theme.separator }
            RowLayout {
                Button {
                    id: aboutHomepageButton
                    objectName: "aboutHomepageButton"
                    text: qsTr("项目主页")
                    flat: true
                    leftPadding: 0
                    rightPadding: 0
                    palette.buttonText: window.surfaceTheme.primary
                    contentItem: Text {
                        objectName: "aboutHomepageText"
                        text: aboutHomepageButton.text
                        font: aboutHomepageButton.font
                        color: aboutHomepageButton.palette.buttonText
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: Qt.openUrlExternally("https://github.com/xiaodaozhi/xdstarter")
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    id: aboutCloseButton
                    objectName: "closeAboutButton"
                    text: qsTr("关闭")
                    palette.buttonText: window.surfaceTheme.onPrimary
                    contentItem: Text { text: aboutCloseButton.text; font: aboutCloseButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: window.surfaceTheme.onPrimary }
                    background: Rectangle { radius: 6; color: window.highContrast ? window.surfaceTheme.primary : aboutCloseButton.down ? Qt.darker(window.surfaceTheme.primary, 1.12) : aboutCloseButton.hovered ? Qt.lighter(window.surfaceTheme.primary, 1.06) : window.surfaceTheme.primary; border.width: window.highContrast && (aboutCloseButton.hovered || aboutCloseButton.activeFocus || aboutCloseButton.down) ? (aboutCloseButton.down ? 3 : 2) : 0; border.color: window.surfaceTheme.onPrimary }
                    onClicked: aboutPopup.close()
                    Keys.onReturnPressed: aboutPopup.close()
                    Keys.onEnterPressed: aboutPopup.close()
                }
            }
        }
        background: FloatingSurface {
            radius: 12
            color: window.surfaceTheme.card
            borderColor: window.theme.controlBorder
            darkTheme: window.darkTheme
            elevation: 2
            shadowEnabled: !window.highContrast
        }
    }

    ActionMenu {
        id: groupMenu

        objectName: "groupMenu"
        title: qsTr("管理分组")
        parent: window.contentItem
        popupType: Popup.Item
        width: Math.min(300, window.panelWidth - 32)
        onClosed: window.queuePopupFocusRestore(window.restoreGroupFocus)

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
                color: window.highContrast ? "transparent" : Qt.rgba(0, 0, 0, 0.4)
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
            window.queuePopupFocusRestore(window.restoreGroupFocus);
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
                color: window.highContrast || window.controller.operationError || window.controller.persistenceError ? window.theme.errorForeground : window.tokens.brand.blue
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
                    border.color: (groupNameField.activeFocus && window.controller.keyboardNavigation) ? window.theme.focusControl : window.theme.controlBorder
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
                color: window.highContrast ? "transparent" : Qt.rgba(0, 0, 0, 0.4)
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
            window.queuePopupFocusRestore(window.restoreTileFocus);
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
                color: window.highContrast || window.controller.operationError || window.controller.persistenceError ? window.theme.errorForeground : window.tokens.brand.blue
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
                    border.color: (tileNameField.activeFocus && window.controller.keyboardNavigation) ? window.theme.focusControl : window.theme.controlBorder
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
            objectName: "startButtonCaptureItem"
            text: window.startButtonCaptureActive ? qsTr("暂停开始按钮接管") : qsTr("启用开始按钮接管")
            enabled: window.startButtonCaptureAvailable || window.startButtonCaptureActive
            Accessible.description: window.startButtonCaptureStatus
            onTriggered: {
                powerMenu.close();
                window.startButtonCaptureToggleRequested();
            }
            Keys.onReturnPressed: {
                powerMenu.close();
                window.startButtonCaptureToggleRequested();
            }
            Keys.onEnterPressed: {
                powerMenu.close();
                window.startButtonCaptureToggleRequested();
            }
        }

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
        onClosed: window.queuePopupFocusRestore(window.restoreTileContextFocus)

        ActionMenuItem {
            objectName: "openContextTileItem"
            text: qsTr("打开")
            actionIcon: "open"
            enabled: window.contextTile !== null && !window.controller.launchBusy
            onTriggered: { resizeMenu.close(); window.activateTile(window.resizeTileId); }
        }

        ActionMenu {
            objectName: "tileDimensionsMenu"
            title: qsTr("磁贴大小")
            width: 232
            SizeMenuItem {
                objectName: "sizeSmallItem"
                text: qsTr("小磁贴 (1×1)")
                checked: window.resizeTileSize === "small"
                onTriggered: window.resizeCurrentTile("small")
            }
            SizeMenuItem {
                objectName: "sizeStandardItem"
                text: qsTr("中磁贴 (2×2)")
                checked: window.resizeTileSize === "standard"
                onTriggered: window.resizeCurrentTile("standard")
            }
            SizeMenuItem {
                objectName: "sizeWideItem"
                text: qsTr("宽磁贴 (4×2)")
                checked: window.resizeTileSize === "wide"
                onTriggered: window.resizeCurrentTile("wide")
            }
        }

        ActionMenuItem {
            objectName: "renameTileItem"
            actionIcon: "edit"
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

        ActionMenuItem {
            id: unpinOption

            objectName: "unpinTileItem"
            text: qsTr("取消固定磁贴")
            actionIcon: "unpin"
            enabled: !window.controller.readOnly && !window.controller.importCommitting
            onTriggered: window.unpinCurrentTile()
            Keys.onReturnPressed: window.unpinCurrentTile()
            Keys.onEnterPressed: window.unpinCurrentTile()
        }
    }

    ActionMenu {
        id: pinMenu

        objectName: "pinApplicationMenu"
        title: qsTr("应用")
        parent: window.contentItem
        popupType: Popup.Item
        width: Math.min(236, window.panelWidth - 32)
        onClosed: window.queuePopupFocusRestore(window.restoreApplicationMenuFocus)

        ActionMenuItem {
            objectName: "applicationOpenAction"
            text: qsTr("打开")
            actionIcon: "open"
            enabled: !window.controller.launchBusy
            onTriggered: { pinMenu.close(); window.controller.launchApplication(window.pinApplicationId); }
        }

        ActionMenuItem {
            objectName: "applicationPinAction"
            text: window.pinApplicationPinned ? qsTr("取消固定磁贴") : qsTr("固定为磁贴")
            actionIcon: window.pinApplicationPinned ? "unpin" : "pin"
            Accessible.description: window.pinApplicationPinned ? qsTr("取消 %1 的全部固定磁贴").arg(window.pinApplicationName) : qsTr("将 %1 固定到最后一个分组").arg(window.pinApplicationName)
            enabled: !window.controller.readOnly && !window.controller.importCommitting
            onTriggered: window.pinCurrentApplication()
            Keys.onReturnPressed: window.pinCurrentApplication()
            Keys.onEnterPressed: window.pinCurrentApplication()
        }
    }

    Rectangle {
        id: dragPlacementPreview
        objectName: "dragPlacementPreview"
        z: 1000
        visible: window.dragActive && window.dragPreviewReady
        scale: window.dragActive ? 1.025 : 1
        readonly property color targetColor: window.highContrast ? window.theme.opaqueFallback : Qt.alpha(window.dragValid ? window.tokens.brand.cyan : window.tokens.status.error, window.dragValid ? 0.2 : 0.27)
        Behavior on x {
            enabled: window.animationsEnabled && window.dragPreviewReady
            onEnabledChanged: {
                if (!enabled)
                    dragPlacementPreview.x = Qt.binding(function () { return window.previewX; });
            }
            NumberAnimation {
                duration: 75
                easing.type: Easing.OutCubic
            }
        }
        Behavior on y {
            enabled: window.animationsEnabled && window.dragPreviewReady
            onEnabledChanged: {
                if (!enabled)
                    dragPlacementPreview.y = Qt.binding(function () { return window.previewY; });
            }
            NumberAnimation {
                duration: 75
                easing.type: Easing.OutCubic
            }
        }
        Behavior on color {
            enabled: window.animationsEnabled && !window.highContrast
            onEnabledChanged: {
                if (!enabled)
                    dragPlacementPreview.color = Qt.binding(function () { return dragPlacementPreview.targetColor; });
            }
            ColorAnimation {
                duration: 100
            }
        }
        x: window.previewX
        y: window.previewY
        width: window.previewWidth
        height: window.previewHeight
        radius: window.tokens.cornerRadiusDip.tile
        color: targetColor
        border.width: 2
        border.color: window.highContrast ? window.theme.foreground : window.dragValid ? window.tokens.brand.cyan : window.tokens.status.error
        Text {
            objectName: "dragContrastStatus"
            anchors.centerIn: parent
            visible: window.highContrast
            text: window.dragValid ? qsTr("可放") : qsTr("不可放")
            color: window.theme.foreground
            font.pixelSize: 12
        }
    }

    onClosing: prepareForDismissal()

    onSelectionStatusChanged: {
        selectionStatusError = false;
        selectionFeedbackTimer.restart();
    }

    onVisibleChanged: {
        if (visible && layoutWidthReady)
            scheduleLayoutColumns();
        if (!visible) {
            prepareForDismissal();
            ++presentationGeneration;
            presentationStaged = false;
            revealMotion.stop();
            dismissMotion.stop();
            revealProgress = 0;
        } else if (!dismissalManaged && presentationComponentReady) {
            if (!presentationStaged)
                stagePresentation();
            const generation = presentationGeneration;
            Qt.callLater(function () {
                if (window.visible && !window.dismissalManaged && generation === window.presentationGeneration)
                    window.prepareForPresentation();
            });
        }
    }

    Component.onCompleted: {
        layoutWidthReady = true;
        observedGridColumns = controller.gridColumns;
        if (visible)
            scheduleLayoutColumns();
        presentationComponentReady = true;
        if (!presentationStaged)
            stagePresentation();
        if (!dismissalManaged && visible)
            prepareForPresentation();
    }
}
