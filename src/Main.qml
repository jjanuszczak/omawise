import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1360
    height: 880
    minimumWidth: 1140
    minimumHeight: 650
    title: qsTr("Omawise")
    color: "#17191d"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: Material.NotRounded

    property int selectedIndex: 0
    property bool reading: false
    property bool listItemSelected: false
    property bool pendingInspection: false
    property string lastInspectedDocumentId: ""
    property bool showHelp: false
    property bool showPalette: false
    property string pendingHighlightText: ""
    property string pendingHighlightDocumentId: ""
    property int activeParagraphIndex: -1
    property int keyboardSelectionAnchor: -1
    property int keyboardSelectionCursor: -1
    property bool keyboardSelecting: false
    property bool refocusDocumentTagEditorAfterSave: false

    function selectedDocument() {
        if (selectedIndex < 0 || selectedIndex >= backend.documents.length)
            return ({})
        return backend.documents[selectedIndex]
    }

    function documentTags(doc) {
        const values = doc.tags || []
        const tags = []
        for (let i = 0; i < values.length; i++) {
            const tag = typeof values[i] === "string" ? values[i] : values[i].name
            if (tag)
                tags.push(tag)
        }
        return tags
    }

    function documentTagSuggestions(query) {
        const normalizedQuery = query.trim().toLowerCase()
        const selected = backend.selectedTags.map(function(tag) { return tag.toLowerCase() })
        const allTags = []
        backend.availableDocumentTags.forEach(function(tag) {
            if (!allTags.some(function(existing) { return existing.toLowerCase() === tag.toLowerCase() }))
                allTags.push(tag)
        })
        backend.documents.forEach(function(doc) {
            documentTags(doc).forEach(function(tag) {
                if (!allTags.some(function(existing) { return existing.toLowerCase() === tag.toLowerCase() }))
                    allTags.push(tag)
            })
        })
        allTags.sort(function(left, right) { return left.localeCompare(right) })
        const suggestions = allTags.filter(function(tag) {
            return selected.indexOf(tag.toLowerCase()) < 0
                && (!normalizedQuery || tag.toLowerCase().indexOf(normalizedQuery) >= 0)
        }).map(function(tag) { return ({ name: tag, create: false }) })
        const known = allTags.concat(backend.selectedTags).some(function(tag) {
            return tag.toLowerCase() === normalizedQuery
        })
        if (normalizedQuery && !known)
            suggestions.push({ name: query.trim(), create: true })
        return suggestions
    }

    function documentLocation() {
        const location = selectedDocument().location || backend.location
        return location === "inbox" ? "new" : location
    }

    function inspectSelectedDocument() {
        const doc = selectedDocument()
        if (!doc.id || reading || doc.id === lastInspectedDocumentId)
            return
        lastInspectedDocumentId = doc.id
        backend.selectDocument(doc.id, doc.title || "", doc.author || "", doc.url || "", documentTags(doc), doc.notes || "")
        if (metadataTabs.currentIndex === 1)
            notebookLoadTimer.restart()
    }

    function scheduleSelectedDocumentInspection() {
        pendingInspection = true
        Qt.callLater(function() {
            if (!pendingInspection)
                return
            pendingInspection = false
            inspectSelectedDocument()
        })
    }

    function addDocumentTag() {
        const tag = newDocumentTagEditor.text.trim()
        if (!tag)
            return
        const tags = backend.selectedTags.slice()
        if (!tags.some(function(value) { return value.toLowerCase() === tag.toLowerCase() }))
            tags.push(tag)
        refocusDocumentTagEditorAfterSave = true
        backend.saveDocumentTags(selectedDocument().id || "", tags)
        newDocumentTagEditor.clear()
        tagSuggestionsPopup.close()
    }

    function removeDocumentTag(tag) {
        const tags = backend.selectedTags.filter(function(value) { return value.toLowerCase() !== tag.toLowerCase() })
        backend.saveDocumentTags(selectedDocument().id || "", tags)
    }

    function openSelected() {
        const doc = selectedDocument()
        if (doc.id) {
            articleFlickable.contentY = 0
            reading = true
            backend.openDocument(doc.id, doc.title || "", doc.author || "", doc.url || "", documentTags(doc), doc.notes || "")
        }
    }

    function activateDocument(index, id) {
        selectedIndex = index
        documentList.currentIndex = index
        articleFlickable.contentY = 0
        reading = true
        const doc = selectedDocument()
        backend.openDocument(id, doc.title || "", doc.author || "", doc.url || "", documentTags(doc), doc.notes || "")
    }

    function setLocationAndFocus(location) {
        backend.setLocation(location)
        documentList.forceActiveFocus()
    }

    function returnToLibrary() {
        const movedFromCurrentView = documentLocation() !== backend.location
        reading = false
        if (movedFromCurrentView)
            backend.refresh()
        Qt.callLater(function() { documentList.forceActiveFocus() })
    }

    function scrollToHeading(position) {
        Qt.callLater(function() {
            const headingRect = articleText.positionToRectangle(position)
            articleFlickable.contentY = Math.max(0, articleText.y + headingRect.y - 14)
        })
    }

    function createSelectedHighlight() {
        const documentId = selectedDocument().id || ""
        const currentSelection = articleText.selectedText
        const selected = currentSelection.trim().length > 0
            ? currentSelection
            : pendingHighlightDocumentId === documentId ? pendingHighlightText : ""
        if (documentId && selected.trim().length > 0) {
            pendingHighlightText = ""
            pendingHighlightDocumentId = ""
            backend.createHighlight(documentId, selected)
        }
    }

    function cacheArticleSelection() {
        const selected = articleText.selectedText
        if (selected.trim().length > 0) {
            pendingHighlightText = selected
            pendingHighlightDocumentId = selectedDocument().id || ""
        }
    }

    function copySelectedArticleMarkdown() {
        const metadata = Object.assign({}, selectedDocument())
        metadata.id = metadata.id || selectedDocument().document_id || ""
        metadata.title = backend.selectedTitle || metadata.title || ""
        metadata.author = backend.selectedAuthor || metadata.author || ""
        metadata.url = backend.selectedUrl || metadata.url || metadata.source_url || ""
        metadata.tags = backend.selectedTags.length > 0 ? backend.selectedTags : documentTags(metadata)
        backend.copyArticleMarkdown(backend.selectedContent, metadata)
    }

    function moveArticleParagraph(direction) {
        const paragraphs = backend.selectedParagraphs
        if (paragraphs.length === 0)
            return
        let index = activeParagraphIndex
        if (index < 0) {
            const cursor = articleText.cursorPosition
            index = 0
            for (let i = 0; i < paragraphs.length; i++) {
                const paragraph = paragraphs[i]
                if (cursor >= paragraph.position && cursor <= paragraph.position + paragraph.length) {
                    index = i
                    break
                }
            }
        } else {
            index = Math.max(0, Math.min(paragraphs.length - 1, index + direction))
        }
        activeParagraphIndex = index
        const paragraph = paragraphs[index]
        keyboardSelectionAnchor = -1
        keyboardSelectionCursor = -1
        articleText.forceActiveFocus()
        articleText.select(paragraph.position, paragraph.position + paragraph.length)
        Qt.callLater(function() {
            const rect = articleText.positionToRectangle(paragraph.position)
            articleFlickable.contentY = Math.max(0, articleText.y + rect.y - 100)
        })
    }

    function extendArticleWordSelection(direction) {
        const paragraphs = backend.selectedParagraphs
        if (paragraphs.length === 0)
            return
        const selectionStart = articleText.selectionStart
        const selectionEnd = articleText.selectionEnd
        let cursor = keyboardSelectionCursor >= 0 ? keyboardSelectionCursor : articleText.cursorPosition
        let anchor = keyboardSelectionAnchor >= 0 ? keyboardSelectionAnchor : cursor
        if (keyboardSelectionAnchor < 0 && selectionStart !== selectionEnd)
            anchor = cursor === selectionStart ? selectionEnd : selectionStart
        const paragraph = paragraphs.find(function(item) {
            return cursor >= item.position && cursor <= item.position + item.length
        }) || paragraphs[0]
        const localCursor = Math.max(0, Math.min(paragraph.length, cursor - paragraph.position))
        const wordPattern = /[A-Za-z0-9_]+/g
        const words = []
        let match
        while ((match = wordPattern.exec(paragraph.text)) !== null)
            words.push({index: match.index, length: match[0].length})
        let target = paragraph.position + localCursor
        if (direction > 0) {
            const word = words.find(function(item) { return item.index + item.length > localCursor })
            if (word)
                target = paragraph.position + word.index + word.length
            else {
                const next = paragraphs.find(function(item) { return item.position > paragraph.position })
                if (next) target = next.position
            }
        } else {
            for (let i = words.length - 1; i >= 0; i--) {
                if (words[i].index < localCursor) {
                    target = paragraph.position + words[i].index
                    break
                }
            }
            if (target === paragraph.position + localCursor) {
                const previous = paragraphs.slice().reverse().find(function(item) { return item.position < paragraph.position })
                if (previous) target = previous.position + previous.length
            }
        }
        keyboardSelectionAnchor = anchor
        keyboardSelectionCursor = target
        keyboardSelecting = true
        articleText.forceActiveFocus()
        articleText.select(anchor, target)
        keyboardSelecting = false
    }

    function extendArticleCharacterSelection(direction) {
        const selectionStart = articleText.selectionStart
        const selectionEnd = articleText.selectionEnd
        let cursor = keyboardSelectionCursor >= 0 ? keyboardSelectionCursor : articleText.cursorPosition
        let anchor = keyboardSelectionAnchor >= 0 ? keyboardSelectionAnchor : cursor
        if (keyboardSelectionAnchor < 0 && selectionStart !== selectionEnd)
            anchor = cursor === selectionStart ? selectionEnd : selectionStart
        const target = Math.max(0, cursor + direction)
        keyboardSelectionAnchor = anchor
        keyboardSelectionCursor = target
        keyboardSelecting = true
        articleText.forceActiveFocus()
        articleText.select(anchor, target)
        keyboardSelecting = false
    }

    function runCommand(command) {
        showPalette = false
        if (command === "open") openSelected()
        else if (command === "refresh") backend.refresh()
        else if (command === "archive") {
            const doc = selectedDocument()
            if (doc.id) backend.archiveDocument(doc.id)
        } else if (command === "help") showHelp = true
        else if (command === "login") backend.login()
        else if (command === "connectApi") apiTokenDialog.open()
        else if (command === "forgetApi") backend.forgetApiToken()
        else if (command === "clearCache") backend.clearArticleCache()
        else if (command === "back") returnToLibrary()
    }

    function updateReaderStyle(key, value) {
        const updated = Object.assign({}, theme.readerStyle)
        updated[key] = value
        theme.saveReaderStyle(updated)
    }

    function resetReaderStyle() {
        theme.saveReaderStyle({
            fontFamily: "monospace",
            fontSizePx: 12,
            lineSpacing: 1.2,
            paragraphSpacingPt: 18,
            contentWidth: 760
        })
    }

    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: showPalette = true }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; enabled: !reading && listItemSelected && !!selectedDocument().id && (!backend.busy || backend.metadataBusy) && !showHelp && !showPalette && !searchField.activeFocus && !newDocumentTagEditor.activeFocus && !documentNoteEditor.activeFocus; onActivated: openSelected() }
    Shortcut { sequence: "Enter"; context: Qt.ApplicationShortcut; enabled: !reading && listItemSelected && !!selectedDocument().id && (!backend.busy || backend.metadataBusy) && !showHelp && !showPalette && !searchField.activeFocus && !newDocumentTagEditor.activeFocus && !documentNoteEditor.activeFocus; onActivated: openSelected() }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !reading && !searchField.activeFocus && !showPalette; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus && !showPalette; onActivated: showHelp = !showHelp }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus; onActivated: { if (showPalette) showPalette = false; else if (showHelp) showHelp = false; else if (reading) returnToLibrary() } }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; enabled: !showPalette && !backend.busy; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; enabled: !reading && !showPalette && !searchField.activeFocus; onActivated: navList.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; enabled: !reading && !showPalette && !searchField.activeFocus; onActivated: documentList.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Shift+H"; context: Qt.ApplicationShortcut; enabled: reading && !showPalette && !helpDialog.visible && articleText.selectedText.trim().length > 0; onActivated: createSelectedHighlight() }
    Shortcut { sequence: "Ctrl+Shift+Left"; context: Qt.ApplicationShortcut; enabled: reading && !helpDialog.visible && !paletteDialog.visible; onActivated: extendArticleWordSelection(-1) }
    Shortcut { sequence: "Ctrl+Shift+Right"; context: Qt.ApplicationShortcut; enabled: reading && !helpDialog.visible && !paletteDialog.visible; onActivated: extendArticleWordSelection(1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: reading && !helpDialog.visible && !paletteDialog.visible; onActivated: moveArticleParagraph(-1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: reading && !helpDialog.visible && !paletteDialog.visible; onActivated: moveArticleParagraph(1) }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: reading && articleText.selectedText.trim().length > 0 && !helpDialog.visible && !paletteDialog.visible; onActivated: createSelectedHighlight() }

    Component.onCompleted: {
        backend.refresh()
        documentList.forceActiveFocus()
    }
    Connections {
        target: backend
        function onBusyChanged() {
            if (!backend.busy && window.refocusDocumentTagEditorAfterSave) {
                window.refocusDocumentTagEditorAfterSave = false
                if (newDocumentTagEditor.enabled)
                    newDocumentTagEditor.forceActiveFocus()
            }
        }
    }
    onSelectedIndexChanged: {
        documentList.currentIndex = selectedIndex
        if (!reading) {
            scheduleSelectedDocumentInspection()
            if (metadataTabs.currentIndex === 1)
                notebookLoadTimer.restart()
        }
    }

    Connections {
        target: backend
        function onDocumentsChanged() {
            const previousId = selectedDocument().id || ""
            let nextIndex = -1
            if (previousId) {
                for (let i = 0; i < backend.documents.length; i++) {
                    if (backend.documents[i].id === previousId) {
                        nextIndex = i
                        break
                    }
                }
            }
            if (nextIndex < 0 && backend.documents.length > 0)
                nextIndex = 0
            selectedIndex = nextIndex
            documentList.currentIndex = selectedIndex
            const nextId = selectedDocument().id || ""
            if (!reading && nextId !== previousId) {
                lastInspectedDocumentId = ""
                scheduleSelectedDocumentInspection()
            }
        }
        function onBusyChanged() {
            if (!backend.busy && pendingInspection)
                scheduleSelectedDocumentInspection()
        }
        function onLocationChanged() {
            for (let i = 0; i < navList.model.length; i++) {
                if (navList.model[i].location === backend.location) {
                    navList.currentIndex = i
                    break
                }
            }
        }
        function onSelectedDocumentChanged() {
            documentNoteEditor.text = backend.selectedNote
        }
        function onApiTokenStateChanged() {
            if (backend.apiTokenAvailable) {
                apiTokenField.clear()
                apiTokenDialog.close()
            }
        }
    }

    Timer {
        id: notebookLoadTimer
        interval: 140
        repeat: false
        onTriggered: if (metadataTabs.currentIndex === 1 && selectedDocument().id) backend.loadHighlights()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

        Rectangle {
            Layout.preferredWidth: 224
            Layout.fillHeight: true
            color: "#1d2025"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 17
                spacing: 17
                RowLayout {
                    spacing: 10
                    Image { source: "qrc:/assets/omawise-mark.svg"; sourceSize.width: 36; sourceSize.height: 36; width: 36; height: 36; smooth: false }
                    Text { text: qsTr("Omawise"); color: "#f0f1ed"; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
                }
                TextField { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                    id: searchField
                    objectName: "searchField"
                    Layout.fillWidth: true
                    visible: !reading
                    placeholderText: qsTr("Search library… (Enter)")
                    selectByMouse: true
                    enabled: !backend.busy
                    activeFocusOnTab: true
                    onAccepted: {
                        listItemSelected = false
                        backend.search(text)
                        documentList.forceActiveFocus()
                    }
                    Keys.onEscapePressed: function(event) {
                        event.accepted = true
                        if (text.length > 0) {
                            clear()
                            backend.refresh()
                        }
                        documentList.forceActiveFocus()
                    }
                }
                ListView {
                    id: navList
                    objectName: "navList"
                    visible: !reading
                    onActiveFocusChanged: if (activeFocus) listItemSelected = false
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    implicitHeight: contentHeight
                    clip: true
                    focus: true
                    activeFocusOnTab: true
                    currentIndex: 0
                    model: [
                        { label: qsTr("Inbox"), location: "new", glyph: "⌂" },
                        { label: qsTr("Later"), location: "later", glyph: "◷" },
                        { label: qsTr("Shortlist"), location: "shortlist", glyph: "☆" },
                        { label: qsTr("Archive"), location: "archive", glyph: "▣" },
                        { label: qsTr("Feed"), location: "feed", glyph: "◉" }
                    ]
                    delegate: ItemDelegate { font.family: theme.uiFont;
                        required property var modelData
                        width: navList.width
                        height: 40
                        padding: 0
                        enabled: !backend.busy
                        highlighted: backend.location === modelData.location
                        background: Rectangle {
                            radius: Math.min(2, theme.radius)
                            color: highlighted ? "#30352a" : navDelegate.activeFocus ? "#292d32" : "transparent"
                        }
                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 9
                            Text { text: modelData.glyph; color: theme.accent; font.family: theme.uiFont; font.pixelSize: 17 }
                            Text { text: modelData.label; color: "#e4e5e0"; font.family: theme.uiFont; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: backend.location === modelData.location ? "•" : ""; color: theme.accent; font.family: theme.uiFont; font.pixelSize: 13 }
                        }
                        onClicked: {
                            searchField.clear()
                            setLocationAndFocus(modelData.location)
                        }
                        id: navDelegate
                    }
                    Keys.onPressed: function(event) {
                        if (backend.busy) return
                        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
                            incrementCurrentIndex(); event.accepted = true
                        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
                            decrementCurrentIndex(); event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            const row = model[currentIndex]
                            if (row) setLocationAndFocus(row.location)
                            event.accepted = true
                        }
                    }
                }
                Text {
                    text: qsTr("CONTENTS")
                    visible: reading
                    color: "#858a83"
                    font.family: theme.uiFont; font.pixelSize: 10
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                ListView {
                    id: outlineList
                    objectName: "outlineList"
                    visible: reading
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    activeFocusOnTab: true
                    model: backend.selectedHeadings
                    delegate: ItemDelegate { font.family: theme.uiFont;
                        required property var modelData
                        width: outlineList.width
                        height: 38
                        leftPadding: 10 + Math.max(0, modelData.level - 1) * 12
                        rightPadding: 8
                        contentItem: Text {
                            text: modelData.text
                            color: "#d9dbd5"
                            font.family: theme.uiFont; font.pixelSize: 12
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(2, theme.radius)
                            color: parent.hovered || parent.activeFocus ? "#292d32" : "transparent"
                        }
                        onClicked: scrollToHeading(modelData.position)
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - 18
                        height: 78
                        radius: Math.min(2, theme.radius)
                        color: "#272a2f"
                        visible: outlineList.count === 0
                        Text {
                            anchors.fill: parent
                            anchors.margins: 10
                            text: qsTr("No headings in this article")
                            color: "#9da198"
                            font.family: theme.uiFont; font.pixelSize: 11
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                Text { text: reading ? qsTr("↑/↓ paragraph · H highlight · select with Shift+Arrow") : qsTr("j/k move · Enter open · Tab changes pane · ? shortcuts"); color: "#93978f"; font.family: theme.uiFont; font.pixelSize: 9; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Rectangle {
                    visible: backend.apiTokenReady && !backend.apiTokenAvailable
                    Layout.fillWidth: true
                    implicitHeight: apiConnectPrompt.implicitHeight + 18
                    color: "#272a2f"
                    border.color: backend.needsApiToken ? "#8b5149" : "#41483d"
                    radius: Math.min(2, theme.radius)
                    ColumnLayout {
                        id: apiConnectPrompt
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 7
                        Text {
                            text: backend.needsApiToken
                                  ? qsTr("The saved API token was rejected.")
                                  : qsTr("Connect the direct API for faster lists and article loading.")
                            color: "#e4dfce"; font.family: theme.uiFont; font.pixelSize: 10
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        Button {
                            Material.roundedScale: Material.NotRounded
                            font.family: theme.uiFont
                            text: qsTr("Connect API token")
                            objectName: "connectApiTokenButton"
                            Layout.fillWidth: true
                            enabled: !backend.busy && !backend.apiLoginBusy
                            onClicked: apiTokenDialog.open()
                        }
                    }
                }
                Rectangle {
                    visible: backend.authenticationRequired
                    Layout.fillWidth: true
                    implicitHeight: authPrompt.implicitHeight + 18
                    color: "#272a2f"
                    border.color: "#514a35"
                    radius: Math.min(2, theme.radius)
                    ColumnLayout {
                        id: authPrompt
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 7
                        Text { text: qsTr("Readwise sign-in required"); color: "#e4dfce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Sign in"); objectName: "authSignInButton"; Layout.fillWidth: true; enabled: !backend.busy; onClicked: backend.login() }
                    }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        ColumnLayout {
            Layout.preferredWidth: reading ? 0 : 410
            Layout.minimumWidth: reading ? 0 : 320
            Layout.maximumWidth: reading ? 0 : 16777215
            Layout.fillWidth: !reading
            Layout.fillHeight: true
            spacing: 0
            visible: !reading
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                Layout.leftMargin: 20
                Layout.rightMargin: 16
                Text { text: reading ? qsTr("Library") : locationLabel(backend.location); color: "#f0f1ed"; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ToolButton {
                    objectName: "openSelectedDocumentButton"
                    implicitWidth: 34; implicitHeight: 34; padding: 0
                    text: "↗"; font.family: theme.uiFont; font.pixelSize: 17
                    enabled: !!selectedDocument().id && (!backend.busy || backend.metadataBusy)
                    onClicked: openSelected()
                    background: Rectangle { color: parent.hovered ? "#343940" : "transparent"; radius: 0 }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open selected article (Enter)")
                }
                ToolButton {
                    implicitWidth: 34; implicitHeight: 34; padding: 0
                    text: "⟳"; font.family: theme.uiFont; font.pixelSize: 17
                    enabled: !backend.busy
                    onClicked: backend.refresh()
                    background: Rectangle { color: parent.hovered ? "#343940" : "transparent"; radius: 0 }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Refresh (R)")
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    id: documentList
                    objectName: "documentList"
                    anchors.fill: parent
                    clip: true
                    model: backend.documents
                    enabled: !backend.busy || backend.metadataBusy
                    focus: true
                    activeFocusOnTab: true
                    currentIndex: 0
                    highlightMoveDuration: 80
                    onActiveFocusChanged: if (activeFocus && currentIndex >= 0) listItemSelected = true
                    delegate: ItemDelegate { font.family: theme.uiFont;
                    id: documentDelegate
                    objectName: "documentRow"
                    required property int index
                    required property string id
                    required property string title
                    required property string author
                    required property string source
                    required property string site_name
                    required property string category
                    required property string location
                    required property string summary
                    required property string reading_time
                    required property string listening_time
                    required property string first_opened_at
                    width: ListView.view.width
                    height: 112
                    padding: 0
                    background: Rectangle {
                        color: index === documentList.currentIndex ? "#292d32" : documentDelegate.activeFocus ? "#24272c" : "transparent"
                        Rectangle { width: 3; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: index === documentList.currentIndex ? theme.accent : "transparent" }
                    }
                    contentItem: ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 17; anchors.rightMargin: 14; anchors.topMargin: 12; anchors.bottomMargin: 11
                        spacing: 5
                        Text { text: title || qsTr("Untitled document"); color: "#e9eae5"; font.family: theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: [author, site_name || source].filter(function(value) { return !!value }).join(" · "); color: "#aaaDA6"; font.family: theme.uiFont; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: summary || ""; color: "#858a83"; font.family: theme.uiFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: category.toUpperCase(); color: theme.accent; font.family: theme.uiFont; font.pixelSize: 9; font.bold: true }
                            Text { text: reading_time || listening_time; color: "#858a83"; font.family: theme.uiFont; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: first_opened_at ? "" : qsTr("UNREAD"); color: "#abb0a4"; font.family: theme.uiFont; font.pixelSize: 8 }
                        }
                    }
                    onClicked: {
                        documentList.currentIndex = index
                        selectedIndex = index
                        listItemSelected = true
                        documentList.forceActiveFocus()
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: function(eventPoint, button) {
                            documentList.currentIndex = documentDelegate.index
                            selectedIndex = documentDelegate.index
                            activateDocument(documentDelegate.index, documentDelegate.id)
                        }
                    }
                    Keys.onPressed: function(event) {
                        if ((!backend.busy || backend.metadataBusy) && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                            openSelected()
                            event.accepted = true
                        }
                    }
                    }
                    footer: Item {
                        width: documentList.width
                        height: backend.hasMoreDocuments ? 58 : 0
                        visible: backend.hasMoreDocuments
                        Button {
                            anchors.centerIn: parent
                            enabled: !backend.busy
                            text: backend.busy ? qsTr("Loading…") : qsTr("Load more documents")
                            font.family: theme.uiFont
                            onClicked: backend.loadMoreDocuments()
                            Material.roundedScale: Material.NotRounded
                        }
                    }
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0) {
                            selectedIndex = currentIndex
                            listItemSelected = true
                        }
                    }
                    Keys.onPressed: function(event) {
                        if (backend.busy && !backend.metadataBusy) return
                        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
                            incrementCurrentIndex(); event.accepted = true
                        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
                            decrementCurrentIndex(); event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            openSelected(); event.accepted = true
                        } else if (event.key === Qt.Key_A) {
                            runCommand("archive"); event.accepted = true
                        }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: backend.busy && !backend.metadataBusy
                    color: "#d917191d"
                    z: 2
                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 76
                        height: 76
                        running: backend.busy && !backend.metadataBusy
                        palette.highlight: theme.accent
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.margins: 12
                height: 70
                radius: Math.min(2, theme.radius)
                color: "#272a2f"
                visible: backend.documents.length === 0
                Column {
                    anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text { text: backend.error || backend.status; color: backend.error.length > 0 ? "#ffb4ab" : "#e5e6e0"; font.family: theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }
                }
            }
            Text { Layout.leftMargin: 18; Layout.bottomMargin: 12; text: qsTr("j/k move   ↵ open   Ctrl+← nav   ? shortcuts"); color: "#858a83"; font.family: theme.uiFont; font.pixelSize: 9 }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        Item {
            Layout.fillWidth: reading
            Layout.fillHeight: true
            Layout.minimumWidth: reading ? 620 : 270
            Layout.preferredWidth: reading ? 1000 : 310
            visible: true
            RowLayout {
                anchors.fill: parent
                spacing: 0
                ColumnLayout {
                    visible: reading
                    Layout.fillWidth: true
                    Layout.minimumWidth: 360
                    Layout.fillHeight: true
                    Layout.leftMargin: 30
                    Layout.rightMargin: 25
                    Layout.topMargin: 25
                    Layout.bottomMargin: 18
                    spacing: 13
                    RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        text: qsTr("Back to library"); icon.source: "qrc:/icons/back.svg"
                        icon.width: 22; icon.height: 22; icon.color: "#aeb2aa"
                        display: AbstractButton.IconOnly
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Back to library")
                        onClicked: returnToLibrary()
                    }
                    ToolButton {
                        objectName: "readerAppearanceButton"
                        text: "Aa"; font.family: theme.uiFont; font.pixelSize: 16
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Reader appearance")
                        onClicked: appearanceDialog.open()
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        objectName: "inboxLocationButton"
                        text: qsTr("Inbox"); icon.source: "qrc:/icons/inbox.svg"
                        icon.width: 24; icon.height: 24; icon.color: checked ? theme.accent : "#aeb2aa"
                        display: AbstractButton.IconOnly; checkable: true
                        checked: documentLocation() === "new"
                        enabled: !backend.busy && !checked
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Inbox")
                        onClicked: backend.moveDocument(selectedDocument().id || "", "new")
                    }
                    ToolButton {
                        objectName: "laterLocationButton"
                        text: qsTr("Later"); icon.source: "qrc:/icons/later.svg"
                        icon.width: 24; icon.height: 24; icon.color: checked ? theme.accent : "#aeb2aa"
                        display: AbstractButton.IconOnly; checkable: true
                        checked: documentLocation() === "later"
                        enabled: !backend.busy && !checked
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Later")
                        onClicked: backend.moveDocument(selectedDocument().id || "", "later")
                    }
                    ToolButton {
                        objectName: "archiveLocationButton"
                        text: qsTr("Archive"); icon.source: "qrc:/icons/archive.svg"
                        icon.width: 24; icon.height: 24; icon.color: checked ? theme.accent : "#aeb2aa"
                        display: AbstractButton.IconOnly; checkable: true
                        checked: documentLocation() === "archive"
                        enabled: !backend.busy && !checked
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Archive")
                        onClicked: backend.moveDocument(selectedDocument().id || "", "archive")
                    }
                    ToolButton {
                        id: articleMoreButton
                        objectName: "articleMoreButton"
                        text: "⋯"; font.family: theme.uiFont; font.pixelSize: 24
                        ToolTip.visible: hovered; ToolTip.text: qsTr("More article options")
                        onPressed: cacheArticleSelection()
                        onClicked: articleMoreMenu.popup(articleMoreButton,
                            articleMoreButton.width - articleMoreMenu.width,
                            articleMoreButton.height + 4)
                    }
                    }
                    Text { text: backend.selectedAuthor; color: "#aeb2aa"; font.family: theme.uiFont; font.pixelSize: 12 }
                    Text { text: backend.selectedTitle || qsTr("Select a document"); color: "#f1f2ed"; font.family: theme.uiFont; font.pixelSize: 26; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { font.family: theme.uiFont;
                    visible: backend.error.length > 0
                    text: backend.error
                    color: "#ffb4ab"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#383b40" }
                    Item {
                    id: articleScroll
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.topMargin: 6
                    Flickable {
                        id: articleFlickable
                        anchors.fill: parent
                        contentWidth: width
                        contentHeight: Math.max(articleText.y + articleText.height + 24, height)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        TextEdit {
                            id: articleText
                            objectName: "articleText"
                            text: backend.busy && backend.selectedContent.length === 0 ? qsTr("Loading document…") : backend.formattedMarkdown(backend.selectedContent, theme.accent, theme.readerStyle)
                            textFormat: TextEdit.RichText
                            width: Math.min(articleFlickable.width, theme.readerStyle.contentWidth)
                            x: Math.max(0, (articleFlickable.width - width) / 2)
                            y: 24
                            height: Math.max(contentHeight, articleFlickable.height - 48)
                            readOnly: true
                            selectByMouse: true
                            activeFocusOnTab: true
                            onLinkActivated: function(link) { backend.openExternalUrl(link) }
                            onCursorPositionChanged: {
                                if (!keyboardSelecting) {
                                    keyboardSelectionAnchor = -1
                                    keyboardSelectionCursor = -1
                                }
                            }
                            onSelectedTextChanged: cacheArticleSelection()
                            wrapMode: TextEdit.Wrap
                            color: "#dedfd9"
                            selectionColor: theme.accent
                            selectedTextColor: theme.accentForeground
                            font.family: theme.readerStyle.fontFamily; font.pixelSize: theme.readerStyle.fontSizePx
                            Keys.onPressed: function(event) {
                                if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                                    && (event.modifiers & Qt.ShiftModifier)) {
                                    const direction = event.key === Qt.Key_Right ? 1 : -1
                                    if (!(event.modifiers & Qt.ControlModifier))
                                        extendArticleCharacterSelection(direction)
                                    if (!(event.modifiers & Qt.ControlModifier))
                                        event.accepted = true
                                } else if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                                           && event.modifiers === Qt.NoModifier) {
                                    keyboardSelectionAnchor = -1
                                    keyboardSelectionCursor = -1
                                }
                            }
                        }
                    }
                    Connections {
                        target: backend
                        function onSelectedDocumentChanged() {
                            articleFlickable.contentY = 0
                            activeParagraphIndex = -1
                            keyboardSelectionAnchor = -1
                            keyboardSelectionCursor = -1
                            pendingHighlightText = ""
                            pendingHighlightDocumentId = ""
                            if (reading)
                                Qt.callLater(function() { articleText.forceActiveFocus() })
                        }
                    }
                    }
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: "#34373c"; visible: reading }
                ColumnLayout {
                    id: metadataPane
                    objectName: "documentMetadataPane"
                    visible: !!selectedDocument().id
                    Layout.preferredWidth: 310
                    Layout.minimumWidth: 270
                    Layout.maximumWidth: 360
                    Layout.fillHeight: true
                    spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    TabBar {
                        id: metadataTabs
                        objectName: "metadataTabs"
                        Layout.fillWidth: true
                        background: Rectangle { color: "#1d2025" }
                        onCurrentIndexChanged: if (currentIndex === 1) notebookLoadTimer.restart()
                        TabButton { text: qsTr("Info"); font.family: theme.uiFont; objectName: "metadataInfoTab" }
                        TabButton { text: backend.selectedHighlights.length > 0 ? qsTr("Notebook · %1").arg(backend.selectedHighlights.length) : qsTr("Notebook"); font.family: theme.uiFont; objectName: "metadataNotebookTab" }
                    }
                    BusyIndicator {
                        width: 20; height: 20
                        visible: backend.metadataBusy
                        running: backend.metadataBusy
                        palette.highlight: theme.accent
                        Layout.rightMargin: 8
                    }
                    }
                    ScrollView {
                        id: metadataScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        background: Rectangle { color: "#1d2025" }
                        ColumnLayout {
                            x: 17
                            y: 17
                            width: metadataScroll.availableWidth - 34
                            spacing: 14
                            ColumnLayout {
                                visible: metadataTabs.currentIndex === 0
                                Layout.fillWidth: true
                                spacing: 14
                        Text { text: qsTr("DOCUMENT DETAILS"); color: "#91968e"; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                        Text { text: backend.selectedTitle || qsTr("Untitled document"); color: "#e6e8e1"; font.family: theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { visible: !!selectedDocument().summary; text: selectedDocument().summary || ""; color: "#c1c4bd"; font.family: theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 6
                            Layout.fillWidth: true
                            Text { text: qsTr("Author"); color: "#8d928a"; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: backend.selectedAuthor || "—"; color: "#d2d5ce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Source"); color: "#8d928a"; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().site_name || selectedDocument().source || "—"; color: "#d2d5ce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Type"); color: "#8d928a"; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().category || "—"; color: "#d2d5ce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Location"); color: "#8d928a"; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().location || backend.location; color: "#d2d5ce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Reading time"); color: "#8d928a"; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().reading_time || selectedDocument().listening_time || "—"; color: "#d2d5ce"; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                        Text { text: qsTr("TAGS"); color: "#91968e"; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: backend.selectedTags
                                delegate: Rectangle {
                                    required property string modelData
                                    width: tagLabel.implicitWidth + (removeTag.visible ? 31 : 18)
                                    height: 26
                                    radius: 13
                                    color: tagHover.hovered ? "#39402f" : "#2c3228"
                                    border.color: "#46513b"
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Text { id: tagLabel; text: modelData; color: "#cbd8b8"; font.family: theme.uiFont; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                                        ToolButton {
                                            id: removeTag
                                            text: "×"
                                            visible: tagHover.hovered
                                            width: visible ? 18 : 0
                                            height: 20
                                            padding: 0
                                            font.pixelSize: 15
                                            onClicked: removeDocumentTag(modelData)
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("Remove %1").arg(modelData)
                                        }
                                    }
                                    HoverHandler { id: tagHover }
                                }
                            }
                            Text { visible: backend.selectedTags.length === 0; text: qsTr("No tags"); color: "#858a83"; font.family: theme.uiFont; font.pixelSize: 10 }
                        }
                        TextField { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                            id: newDocumentTagEditor
                            objectName: "newDocumentTagEditor"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Find or create a document tag…")
                            selectByMouse: true
                            enabled: !backend.busy
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    backend.loadDocumentTags()
                                if (activeFocus && documentTagSuggestions(text).length > 0)
                                    tagSuggestionsPopup.open()
                            }
                            onTextEdited: {
                                backend.loadDocumentTags()
                                if (documentTagSuggestions(text).length > 0)
                                    tagSuggestionsPopup.open()
                                else
                                    tagSuggestionsPopup.close()
                            }
                            onAccepted: {
                                const matches = documentTagSuggestions(text)
                                if (matches.length > 0) {
                                    newDocumentTagEditor.text = matches[0].name
                                    addDocumentTag()
                                    tagSuggestionsPopup.close()
                                }
                            }
                        }
                            }
                            ColumnLayout {
                                visible: metadataTabs.currentIndex === 1
                                Layout.fillWidth: true
                                spacing: 14
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                        Text { text: qsTr("DOCUMENT NOTE"); color: "#91968e"; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                        TextArea { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                            id: documentNoteEditor
                            objectName: "documentNoteEditor"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            placeholderText: qsTr("Add a note about this document…")
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                        }
                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                            text: qsTr("Save note")
                            objectName: "saveDocumentNoteButton"
                            enabled: !backend.busy
                            Layout.fillWidth: true
                            onClicked: backend.saveDocumentNote(selectedDocument().id || "", documentNoteEditor.text)
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("HIGHLIGHTS"); color: "#91968e"; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                            Text { text: backend.selectedHighlights.length; color: theme.accent; font.family: theme.uiFont; font.pixelSize: 10 }
                        }
                        Repeater {
                            id: highlightRepeater
                            objectName: "highlightRepeater"
                            model: backend.selectedHighlights
                            delegate: Rectangle {
                                id: highlightCard
                                objectName: "highlightCard"
                                required property var modelData
                                property bool editingNote: false
                                property bool editingTags: false
                                Layout.fillWidth: true
                                implicitHeight: highlightColumn.implicitHeight + 20
                                color: "#25292e"
                                border.color: "#383d43"
                                radius: Math.min(2, theme.radius)
                                ColumnLayout {
                                    id: highlightColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 7
                                    Text {
                                        text: highlightCard.modelData.text || qsTr("Highlight")
                                        color: "#e5e6e0"
                                        font.family: theme.uiFont; font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.max(highlightNotePreview.implicitHeight + 20, 42)
                                        radius: 3
                                        color: "#1d2025"
                                        border.color: "#343940"
                                        visible: !highlightCard.editingNote
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            Text {
                                                id: highlightNotePreview
                                                text: highlightCard.modelData.notes || qsTr("Add a note to this highlight…")
                                                color: highlightCard.modelData.notes ? "#c6c9c2" : "#858a83"
                                                font.family: theme.uiFont; font.pixelSize: 10
                                                wrapMode: Text.Wrap
                                                Layout.fillWidth: true
                                            }
                                            ToolButton {
                                                text: highlightCard.modelData.notes ? qsTr("Edit") : qsTr("Add note")
                                                font.family: theme.uiFont; font.pixelSize: 10
                                                visible: highlightHover.hovered || highlightCard.editingNote
                                                onClicked: { highlightNoteEditor.text = highlightCard.modelData.notes || ""; highlightCard.editingNote = true }
                                            }
                                        }
                                    }
                                    TextArea { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                        id: highlightNoteEditor
                                        objectName: "highlightNoteEditor"
                                        text: highlightCard.modelData.notes || ""
                                        visible: highlightCard.editingNote
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 86
                                        placeholderText: qsTr("Add context or a thought about this passage…")
                                        wrapMode: TextEdit.Wrap
                                        selectByMouse: true
                                    }
                                    RowLayout {
                                        visible: highlightCard.editingNote
                                        Layout.fillWidth: true
                                        Item { Layout.fillWidth: true }
                                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Cancel"); flat: true; onClicked: highlightCard.editingNote = false }
                                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                            text: qsTr("Save note")
                                            objectName: "saveHighlightNoteButton"
                                            enabled: !backend.busy
                                            onClicked: {
                                                backend.saveHighlightNote(selectedDocument().id || "", highlightCard.modelData.id || "", highlightNoteEditor.text)
                                                highlightCard.editingNote = false
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: highlightCard.modelData.tags && highlightCard.modelData.tags.length > 0
                                                ? qsTr("Tags: %1").arg(highlightCard.modelData.tags.join(", "))
                                                : qsTr("No highlight tags")
                                            color: theme.accent
                                            font.family: theme.uiFont; font.pixelSize: 10
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                        ToolButton {
                                            text: highlightCard.editingTags ? qsTr("Done") : qsTr("Edit tags")
                                            font.family: theme.uiFont; font.pixelSize: 10
                                            visible: highlightHover.hovered || highlightCard.editingTags
                                            onClicked: highlightCard.editingTags = !highlightCard.editingTags
                                        }
                                    }
                                    RowLayout {
                                        visible: highlightCard.editingTags
                                        Layout.fillWidth: true
                                        TextField { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                            id: highlightTagEditor
                                            objectName: "highlightTagEditor"
                                            Layout.fillWidth: true
                                            placeholderText: qsTr("Add a tag")
                                            selectByMouse: true
                                        }
                                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                            text: qsTr("Add")
                                            objectName: "addHighlightTagButton"
                                            enabled: !backend.busy && highlightTagEditor.text.trim().length > 0
                                            onClicked: {
                                                backend.addHighlightTags(selectedDocument().id || "", highlightCard.modelData.id || "", [highlightTagEditor.text])
                                                highlightTagEditor.clear()
                                            }
                                        }
                                    }
                                    RowLayout {
                                        visible: highlightCard.editingTags
                                        Layout.fillWidth: true
                                        TextField { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                            id: removeHighlightTagEditor
                                            objectName: "removeHighlightTagEditor"
                                            Layout.fillWidth: true
                                            placeholderText: qsTr("Remove a tag")
                                            selectByMouse: true
                                        }
                                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                                            text: qsTr("Remove")
                                            objectName: "removeHighlightTagButton"
                                            enabled: !backend.busy && removeHighlightTagEditor.text.trim().length > 0
                                            onClicked: {
                                                backend.removeHighlightTags(selectedDocument().id || "", highlightCard.modelData.id || "", [removeHighlightTagEditor.text])
                                                removeHighlightTagEditor.clear()
                                            }
                                        }
                                    }
                                    HoverHandler { id: highlightHover }
                                }
                            }
                        }
                        Text {
                            visible: !backend.busy && backend.selectedHighlights.length === 0
                            text: qsTr("No highlights yet. Select article text and save it with Highlight selection.")
                            color: "#9da198"
                            font.family: theme.uiFont; font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Item { Layout.preferredHeight: 14 }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#1d2025"
            border.color: "#303238"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                BusyIndicator {
                    width: 18
                    height: 18
                    visible: backend.busy && !backend.metadataBusy
                    running: backend.busy && !backend.metadataBusy
                    palette.highlight: theme.accent
                }
                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    visible: !backend.busy
                    color: backend.error.length > 0 ? "#ffb4ab" : "#858a83"
                }
                Text {
                    text: backend.error.length > 0 ? backend.error : backend.status
                    color: backend.error.length > 0 ? "#ffb4ab" : "#b7bbb3"
                    font.family: theme.uiFont; font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: reading ? qsTr("Article") : locationLabel(backend.location)
                    color: "#858a83"
                    font.family: theme.uiFont; font.pixelSize: 10
                }
            }
        }
    }
    }

    Dialog {
        Material.roundedScale: Material.NotRounded
        id: appearanceDialog
        objectName: "readerAppearanceDialog"
        title: qsTr("Reader appearance")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 500
        height: 520
        standardButtons: Dialog.NoButton
        background: Rectangle {
            color: "#25282e"
            border.color: "#3a3d43"
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Changes save to reader.toml and apply while Omawise is running.")
                color: "#b7bbb3"
                font.family: theme.uiFont
            }
            GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 14
                Layout.fillWidth: true
                Label { text: qsTr("Font family"); font.family: theme.uiFont }
                ComboBox {
                    id: readerFontFamilyPicker
                    objectName: "readerFontFamilyPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    editable: true
                    model: ["monospace", "sans-serif", "serif", "Atkinson Hyperlegible", "Inter", "Noto Sans", "Noto Serif"]
                    editText: theme.readerStyle.fontFamily
                    onActivated: updateReaderStyle("fontFamily", currentText)
                    onAccepted: updateReaderStyle("fontFamily", editText)
                }
                Label { text: qsTr("Font size"); font.family: theme.uiFont }
                SpinBox {
                    objectName: "readerFontSizePicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    from: 12
                    to: 48
                    value: theme.readerStyle.fontSizePx
                    editable: true
                    onValueModified: updateReaderStyle("fontSizePx", value)
                }
                Label { text: qsTr("Line spacing"); font.family: theme.uiFont }
                SpinBox {
                    objectName: "readerLineSpacingPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    from: 10
                    to: 22
                    stepSize: 1
                    value: Math.round(theme.readerStyle.lineSpacing * 10)
                    editable: true
                    textFromValue: function(value, locale) { return (value / 10).toFixed(1) }
                    valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
                    onValueModified: updateReaderStyle("lineSpacing", value / 10)
                }
                Label { text: qsTr("Paragraph spacing"); font.family: theme.uiFont }
                SpinBox {
                    objectName: "readerParagraphSpacingPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    from: 0
                    to: 36
                    value: theme.readerStyle.paragraphSpacingPt
                    editable: true
                    onValueModified: updateReaderStyle("paragraphSpacingPt", value)
                }
                Label { text: qsTr("Content width"); font.family: theme.uiFont }
                SpinBox {
                    objectName: "readerContentWidthPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    from: 480
                    to: 1200
                    stepSize: 20
                    value: theme.readerStyle.contentWidth
                    editable: true
                    onValueModified: updateReaderStyle("contentWidth", value)
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Settings file: ~/.config/omawise/reader.toml")
                color: "#858a83"
                font.family: theme.uiFont
                font.pixelSize: 10
            }
            Label {
                visible: theme.readerStyleError.length > 0
                text: theme.readerStyleError
                color: "#ffb4ab"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: Item {
            implicitHeight: appearanceCloseButton.implicitHeight + 24
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 12
                spacing: 8
                Button {
                    text: qsTr("Reset defaults")
                    font.family: theme.uiFont
                    onClicked: resetReaderStyle()
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: appearanceCloseButton
                    objectName: "readerAppearanceCloseButton"
                    text: qsTr("Close")
                    font.family: theme.uiFont
                    onClicked: appearanceDialog.close()
                }
            }
        }
    }

    Menu {
        id: articleMoreMenu
        objectName: "articleMoreMenu"
        width: 220
        MenuItem {
            text: qsTr("Copy Markdown")
            objectName: "copyArticleMarkdownButton"
            enabled: backend.selectedContent.length > 0
            onTriggered: copySelectedArticleMarkdown()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Highlight selection")
            objectName: "highlightSelectionButton"
            enabled: !backend.busy && pendingHighlightText.trim().length > 0
                && pendingHighlightDocumentId === (selectedDocument().id || "")
            onTriggered: createSelectedHighlight()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Open in Reader")
            enabled: backend.selectedUrl.length > 0
            onTriggered: Qt.openUrlExternally(backend.selectedUrl)
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: apiTokenDialog
        objectName: "apiTokenDialog"
        modal: true
        anchors.centerIn: parent
        width: 500
        title: qsTr("Connect Readwise API")
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("Paste a Readwise access token. Omawise validates it and stores it in your desktop keyring; it is never written to app settings or passed in command arguments.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Get an access token from Readwise")
                font.family: theme.uiFont
                onClicked: backend.openExternalUrl("https://readwise.io/access_token")
            }
            TextField {
                id: apiTokenField
                objectName: "apiTokenField"
                Layout.fillWidth: true
                placeholderText: qsTr("Readwise access token")
                echoMode: TextInput.Password
                selectByMouse: true
                enabled: !backend.apiLoginBusy
                onAccepted: if (!backend.apiLoginBusy) backend.saveApiToken(text)
            }
            Label {
                visible: backend.apiLoginError.length > 0
                text: backend.apiLoginError
                color: "#ffb4ab"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            BusyIndicator {
                visible: backend.apiLoginBusy
                running: visible
                Layout.alignment: Qt.AlignHCenter
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                font.family: theme.uiFont
                enabled: !backend.apiLoginBusy
                onClicked: { apiTokenField.clear(); apiTokenDialog.close() }
            }
            Button {
                text: qsTr("Connect")
                font.family: theme.uiFont
                enabled: apiTokenField.text.trim().length > 0 && !backend.apiLoginBusy && !backend.busy
                onClicked: backend.saveApiToken(apiTokenField.text)
            }
        }
        onClosed: apiTokenField.clear()
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: paletteDialog
        visible: showPalette
        modal: true
        x: (window.width - width) / 2
        y: 90
        width: 500
        title: qsTr("Command palette")
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            Repeater {
                model: [
                    { label: qsTr("Refresh current view"), key: "refresh" },
                    { label: qsTr("Open selected document"), key: "open" },
                    { label: qsTr("Archive selected document"), key: "archive" },
                    { label: qsTr("Connect direct Readwise API"), key: "connectApi" },
                    { label: qsTr("Sign in to Readwise CLI"), key: "login" },
                    { label: qsTr("Forget saved API token"), key: "forgetApi", enabled: backend.apiTokenAvailable && !backend.busy },
                    { label: qsTr("Clear cached article content"), key: "clearCache" },
                    { label: qsTr("Keyboard shortcuts"), key: "help" },
                    { label: qsTr("Back to library"), key: "back" }
                ]
                delegate: ItemDelegate { font.family: theme.uiFont;
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    enabled: modelData.enabled !== false
                    onClicked: runCommand(modelData.key)
                }
            }
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: helpDialog
        objectName: "helpDialog"
        visible: showHelp
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(720, window.width - 48)
        height: Math.min(620, window.height - 72)
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.NoButton
        onAccepted: showHelp = false
        contentItem: ScrollView {
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: helpDialog.availableWidth
                spacing: 18
                Repeater {
                    model: [
                        ({ title: qsTr("Library"), rows: [
                            ["j / k", qsTr("Move through documents")],
                            ["Enter", qsTr("Open selected document")],
                            ["/", qsTr("Focus search")],
                            ["Ctrl+K", qsTr("Open command palette")],
                            ["Escape", qsTr("Clear search and return to list")],
                            ["a", qsTr("Archive selected document")],
                            ["Ctrl+← / Ctrl+→", qsTr("Focus navigation or document list")]
                        ]}),
                        ({ title: qsTr("Reading"), rows: [
                            ["↑ / ↓", qsTr("Select the previous or next paragraph")],
                            ["Shift+Arrow", qsTr("Select text by character")],
                            ["Ctrl+Shift+← / →", qsTr("Extend selection by word")],
                            ["h", qsTr("Highlight the current selection")],
                            ["Ctrl+Shift+H", qsTr("Save selected article text as a highlight")]
                        ]}),
                        ({ title: qsTr("General"), rows: [
                            ["Ctrl+R", qsTr("Refresh current view")],
                            ["Escape", qsTr("Return or close overlay")],
                            ["?", qsTr("Show this shortcut list")]
                        ]})
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 7
                        Label {
                            text: modelData.title
                            font.family: theme.uiFont
                            font.bold: true
                            color: theme.accent
                            Layout.bottomMargin: 2
                        }
                        Repeater {
                            model: modelData.rows
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 16
                                Label {
                                    text: modelData[0]
                                    font.family: theme.uiFont
                                    font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 190
                                    Layout.minimumWidth: 160
                                }
                                Label {
                                    text: modelData[1]
                                    font.family: theme.uiFont
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }
        footer: Item {
            implicitHeight: helpCloseButton.implicitHeight + 24
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 12
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    id: helpCloseButton
                    Material.roundedScale: Material.NotRounded
                    font.family: theme.uiFont
                    text: qsTr("Close")
                    objectName: "helpCloseButton"
                    onClicked: helpDialog.accept()
                }
            }
        }
    }

    Popup {
        id: tagSuggestionsPopup
        objectName: "documentTagSuggestionsPopup"
        parent: Overlay.overlay
        x: newDocumentTagEditor.mapToItem(Overlay.overlay, 0, newDocumentTagEditor.height).x
        y: newDocumentTagEditor.mapToItem(Overlay.overlay, 0, newDocumentTagEditor.height).y
        width: Math.max(240, Math.min(320, window.width - x - 12))
        height: Math.min(tagSuggestionList.contentHeight + 2, 360)
        padding: 1
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#29313a"
            radius: 8
            border.color: "#39434d"
        }
        contentItem: ListView {
            id: tagSuggestionList
            objectName: "documentTagSuggestionList"
            clip: true
            model: documentTagSuggestions(newDocumentTagEditor.text)
            implicitHeight: contentHeight
            delegate: ItemDelegate {
                required property var modelData
                width: tagSuggestionList.width
                height: 38
                leftPadding: 16
                rightPadding: 16
                text: modelData.create
                    ? qsTr("Create \"%1\"").arg(modelData.name)
                    : modelData.name
                font.family: theme.uiFont
                font.pixelSize: 12
                onClicked: {
                    newDocumentTagEditor.text = modelData.name
                    addDocumentTag()
                    tagSuggestionsPopup.close()
                    newDocumentTagEditor.forceActiveFocus()
                }
                background: Rectangle {
                    color: parent.hovered ? "#37414b" : "transparent"
                }
            }
        }
    }

    function locationLabel(location) {
        if (location === "new") return qsTr("Inbox")
        if (location === "later") return qsTr("Later")
        if (location === "shortlist") return qsTr("Shortlist")
        if (location === "archive") return qsTr("Archive")
        if (location === "feed") return qsTr("Feed")
        return qsTr("Library")
    }
}
