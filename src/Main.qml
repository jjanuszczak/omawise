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
    onClosing: function(close) {
        if (allowWindowClose || !reading || !backend.selectedContent)
            return
        saveCurrentProgress(true)
        if (backend.busy || backend.progressBusy || Object.keys(queuedProgressSaves).length > 0) {
            pendingCloseAfterProgress = true
            close.accepted = false
        }
    }
    color: theme.colors.window
    Material.theme: theme.colorScheme === "dark" ? Material.Dark : Material.Light
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
    property int highestReadingProgress: 0
    property int lastSavedReadingProgress: 0
    property int currentReadingProgress: 0
    property string progressDocumentId: ""
    property var queuedProgressSaves: ({})
    property bool pendingProgressRestore: false
    property string restoredProgressDocumentId: ""
    property bool pendingCloseAfterProgress: false
    property bool allowWindowClose: false

    function selectedDocument() {
        if (selectedIndex < 0 || selectedIndex >= backend.documents.length)
            return ({})
        return backend.documents[selectedIndex]
    }

    function documentIsSeen(doc) {
        return doc.seen === undefined ? !!doc.first_opened_at : Boolean(doc.seen)
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
        const location = reading
            ? (backend.selectedLocation || selectedDocument().location || backend.location)
            : (selectedDocument().location || backend.location)
        return location === "inbox" ? "new" : location
    }

    function activeDocumentLocation() {
        return backend.pendingMoveLocation || documentLocation()
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
            if (reading)
                saveCurrentProgress(true)
            highestReadingProgress = Math.max(0, Number(doc.reading_progress) || 0)
            lastSavedReadingProgress = highestReadingProgress
            currentReadingProgress = highestReadingProgress
            progressDocumentId = doc.id
            pendingProgressRestore = true
            restoredProgressDocumentId = ""
            articleFlickable.contentY = 0
            reading = true
            backend.openDocument(doc.id, doc.title || "", doc.author || "", doc.url || "", documentTags(doc), doc.notes || "")
        }
    }

    function activateDocument(index, id) {
        if (reading)
            saveCurrentProgress(true)
        selectedIndex = index
        documentList.currentIndex = index
        const doc = selectedDocument()
        highestReadingProgress = Math.max(0, Number(doc.reading_progress) || 0)
        lastSavedReadingProgress = highestReadingProgress
        currentReadingProgress = highestReadingProgress
        progressDocumentId = id
        pendingProgressRestore = true
        restoredProgressDocumentId = ""
        articleFlickable.contentY = 0
        reading = true
        backend.openDocument(id, doc.title || "", doc.author || "", doc.url || "", documentTags(doc), doc.notes || "")
    }

    function setLocationAndFocus(location) {
        backend.setLocation(location)
        documentList.forceActiveFocus()
    }

    function returnToLibrary() {
        if (backend.pendingMoveLocation)
            return
        saveCurrentProgress(true)
        const destination = documentLocation()
        reading = false
        if (destination !== backend.location) {
            searchField.clear()
            backend.setLocation(destination)
        }
        Qt.callLater(function() { documentList.forceActiveFocus() })
    }

    function saveCurrentProgress(force) {
        const id = progressDocumentId || selectedDocument().id || ""
        if (!id || !backend.selectedContent)
            return
        const range = Math.max(1, articleFlickable.contentHeight - articleFlickable.height)
        const current = Math.min(100, Math.max(0, Math.round(articleFlickable.contentY / range * 100)))
        currentReadingProgress = current
        highestReadingProgress = Math.max(highestReadingProgress, current)
        const value = theme.readerStyle.progressMode === "furthest_position"
            ? highestReadingProgress : currentReadingProgress
        if (value !== lastSavedReadingProgress || force)
            queueProgressSave(id, value, force)
    }

    function queueProgressSave(id, value, immediate) {
        if (!id)
            return
        const queued = Object.assign({}, queuedProgressSaves)
        queued[id] = value
        queuedProgressSaves = queued
        if (immediate && !backend.busy) {
            delete queued[id]
            queuedProgressSaves = queued
            backend.saveReadingProgress(id, value)
            if (id === progressDocumentId)
                lastSavedReadingProgress = value
            if (Object.keys(queuedProgressSaves).length > 0)
                progressSaveTimer.restart()
            return
        }
        progressSaveTimer.restart()
    }

    function maybeCloseAfterProgressSave() {
        if (!backend.busy && !backend.progressBusy && pendingCloseAfterProgress
            && Object.keys(queuedProgressSaves).length === 0) {
            pendingCloseAfterProgress = false
            allowWindowClose = true
            window.close()
        }
    }

    function restoreReadingPosition() {
        if (!pendingProgressRestore || !reading || !backend.selectedContent
            || progressDocumentId !== selectedDocument().id || !articleText.height)
            return
        const id = progressDocumentId
        if (restoredProgressDocumentId === id)
            return
        Qt.callLater(function() {
            if (!pendingProgressRestore || !reading || progressDocumentId !== id
                || selectedDocument().id !== id || !articleText.height)
                return
            const range = Math.max(0, articleFlickable.contentHeight - articleFlickable.height)
            const progress = theme.readerStyle.progressMode === "furthest_position"
                ? highestReadingProgress : currentReadingProgress
            articleFlickable.contentY = range * Math.max(0, Math.min(100, progress)) / 100
            restoredProgressDocumentId = id
            pendingProgressRestore = false
        })
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

    function selectedArticleMetadata() {
        const metadata = Object.assign({}, selectedDocument())
        metadata.id = metadata.id || selectedDocument().document_id || ""
        metadata.title = backend.selectedTitle || metadata.title || ""
        metadata.author = backend.selectedAuthor || metadata.author || ""
        metadata.url = backend.selectedUrl || metadata.url || metadata.source_url || ""
        metadata.tags = backend.selectedTags.length > 0 ? backend.selectedTags : documentTags(metadata)
        return metadata
    }

    function copySelectedArticleMarkdown() {
        backend.copyArticleMarkdown(backend.selectedContent, selectedArticleMetadata())
    }

    function openSelectedArticleInEditor(editorId) {
        backend.openArticleInEditor(editorId, backend.selectedContent, selectedArticleMetadata())
    }

    function originalDocumentUrl() {
        const doc = selectedDocument()
        return doc.source_url || doc.sourceUrl || ""
    }

    function resetReadingProgress() {
        const id = progressDocumentId || selectedDocument().id || ""
        if (!id)
            return
        progressSaveTimer.stop()
        const queued = Object.assign({}, queuedProgressSaves)
        delete queued[id]
        queuedProgressSaves = queued
        highestReadingProgress = 0
        currentReadingProgress = 0
        lastSavedReadingProgress = 0
        pendingProgressRestore = false
        restoredProgressDocumentId = id
        articleFlickable.contentY = 0
        backend.saveReadingProgress(id, 0)
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
        else if (command === "account") accountDialog.open()
        else if (command === "connectApi") apiTokenDialog.open()
        else if (command === "forgetApi") backend.forgetApiToken()
        else if (command === "clearCache") backend.clearArticleCache()
        else if (command === "saveUrl") saveUrlDialog.open()
        else if (command === "toggleSeen") {
            const doc = selectedDocument()
            if (doc.id) backend.setDocumentSeen(doc.id, !Boolean(doc.seen))
        }
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
            contentWidth: 760,
            progressMode: "last_position",
            externalEditors: ["neovim", "vscode", "obsidian"]
        })
    }

    function setExternalEditorEnabled(editorId, enabled) {
        const editors = theme.readerStyle.externalEditors.slice()
        const index = editors.indexOf(editorId)
        if (enabled && index < 0)
            editors.push(editorId)
        else if (!enabled && index >= 0)
            editors.splice(index, 1)
        updateReaderStyle("externalEditors", editors)
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
            if (!backend.busy && Object.keys(queuedProgressSaves).length > 0)
                progressSaveTimer.restart()
            maybeCloseAfterProgressSave()
        }
        function onProgressBusyChanged() {
            maybeCloseAfterProgressSave()
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
            color: theme.colors.panel
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 17
                spacing: 17
                RowLayout {
                    visible: !reading
                    spacing: 10
                    Image { source: theme.colorScheme === "light" ? "qrc:/assets/omawise-mark-light.svg" : "qrc:/assets/omawise-mark.svg"; sourceSize.width: 36; sourceSize.height: 36; width: 36; height: 36; smooth: false }
                    Text { text: qsTr("Omawise"); color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        objectName: "saveUrlButton"
                        visible: !reading
                        enabled: !backend.busy
                        implicitWidth: 38
                        implicitHeight: 38
                        padding: 0
                        icon.source: "qrc:/icons/add-document.svg"
                        icon.width: 24
                        icon.height: 24
                        icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        Accessible.name: qsTr("Save URL to Reader")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Save URL to Reader")
                        background: Rectangle { color: parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
                        onClicked: saveUrlDialog.open()
                    }
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
                        { label: qsTr("Inbox"), location: "new", icon: "qrc:/icons/inbox.svg" },
                        { label: qsTr("Later"), location: "later", icon: "qrc:/icons/later.svg" },
                        { label: qsTr("Shortlist"), location: "shortlist", icon: "qrc:/icons/shortlist.svg" },
                        { label: qsTr("Archive"), location: "archive", icon: "qrc:/icons/archive.svg" },
                        { label: qsTr("Feed"), location: "feed", icon: "qrc:/icons/feed.svg" }
                    ]
                    delegate: ItemDelegate { font.family: theme.uiFont;
                        required property var modelData
                        width: navList.width
                        height: 40
                        leftPadding: 10
                        rightPadding: 24
                        topPadding: 0
                        bottomPadding: 0
                        enabled: !backend.busy
                        highlighted: backend.location === modelData.location
                        text: modelData.label
                        icon.source: modelData.icon
                        icon.width: 24
                        icon.height: 24
                        icon.color: highlighted ? theme.accent : theme.colors.textSecondary
                        background: Rectangle {
                            radius: Math.min(2, theme.radius)
                            color: highlighted ? theme.colors.selection : navDelegate.activeFocus ? theme.colors.surfaceAlt : "transparent"
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 9
                                anchors.verticalCenter: parent.verticalCenter
                                text: navDelegate.highlighted ? "•" : ""
                                color: theme.accent
                                font.family: theme.uiFont
                                font.pixelSize: 13
                            }
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
                    color: theme.colors.textMuted
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
                            color: theme.colors.textSecondary
                            font.family: theme.uiFont; font.pixelSize: 12
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(2, theme.radius)
                            color: parent.hovered || parent.activeFocus ? theme.colors.surfaceAlt : "transparent"
                        }
                        onClicked: scrollToHeading(modelData.position)
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - 18
                        height: 78
                        radius: Math.min(2, theme.radius)
                        color: theme.colors.surfaceAlt
                        visible: outlineList.count === 0
                        Text {
                            anchors.fill: parent
                            anchors.margins: 10
                            text: qsTr("No headings in this article")
                            color: theme.colors.textMuted
                            font.family: theme.uiFont; font.pixelSize: 11
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Item { visible: !reading; Layout.fillHeight: !reading }
                Rectangle { visible: !reading; Layout.fillWidth: true; height: 1; color: theme.colors.border }
                Text { visible: !reading; text: qsTr("j/k move · Enter open · Tab changes pane · ? shortcuts"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 9; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Rectangle {
                    visible: !reading && backend.cliSignedIn && backend.apiTokenReady && !backend.apiTokenAvailable
                    Layout.fillWidth: true
                    implicitHeight: apiConnectPrompt.implicitHeight + 18
                    color: theme.colors.surfaceAlt
                    border.color: backend.needsApiToken ? theme.colors.error : theme.colors.border
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
                            color: theme.colors.warning; font.family: theme.uiFont; font.pixelSize: 10
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
                    visible: !reading && backend.authenticationRequired
                    Layout.fillWidth: true
                    implicitHeight: authPrompt.implicitHeight + 18
                    color: theme.colors.surfaceAlt
                    border.color: theme.colors.warning
                    radius: Math.min(2, theme.radius)
                    ColumnLayout {
                        id: authPrompt
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 7
                        Text { text: qsTr("Connect to Readwise to use search, highlights, and annotation features. Omawise will open your browser for sign-in."); color: theme.colors.warning; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Set up Readwise"); objectName: "authSignInButton"; Layout.fillWidth: true; enabled: !backend.busy; onClicked: accountDialog.open() }
                    }
                }
                RowLayout {
                    objectName: "navigationAccountActions"
                    visible: !reading
                    Layout.fillWidth: true
                    spacing: 2
                    ToolButton {
                        objectName: "accountSettingsButton"
                        icon.source: "qrc:/icons/account.svg"
                        icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        Accessible.name: qsTr("Readwise account settings")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Readwise account")
                        Layout.fillWidth: true
                        onClicked: accountDialog.open()
                    }
                    ToolButton {
                        objectName: "readwiseProfileButton"
                        icon.source: "qrc:/icons/profile.svg"
                        icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        Accessible.name: qsTr("Readwise profile")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Readwise profile")
                        Layout.fillWidth: true
                        onClicked: backend.openExternalUrl("https://read.readwise.io/profile")
                    }
                    ToolButton {
                        objectName: "readwiseSettingsButton"
                        icon.source: "qrc:/icons/preferences.svg"
                        icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        Accessible.name: qsTr("Readwise settings")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Readwise settings")
                        Layout.fillWidth: true
                        onClicked: backend.openExternalUrl("https://read.readwise.io/preferences")
                    }
                    ToolButton {
                        objectName: "localSettingsButton"
                        icon.source: "qrc:/icons/settings.svg"
                        icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        Accessible.name: qsTr("Omawise settings")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Omawise settings")
                        Layout.fillWidth: true
                        onClicked: appearanceDialog.open()
                    }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: theme.colors.borderStrong }

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
                Text { text: reading ? qsTr("Library") : locationLabel(backend.location); color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ToolButton {
                    objectName: "openSelectedDocumentButton"
                    implicitWidth: 38; implicitHeight: 38; padding: 0
                    icon.source: "qrc:/icons/open.svg"
                    icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                    display: AbstractButton.IconOnly
                    enabled: !!selectedDocument().id && (!backend.busy || backend.metadataBusy)
                    onClicked: openSelected()
                    background: Rectangle { color: parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open selected article (Enter)")
                }
                ToolButton {
                    objectName: "refreshLibraryButton"
                    implicitWidth: 38; implicitHeight: 38; padding: 0
                    icon.source: "qrc:/icons/refresh.svg"
                    icon.width: 24; icon.height: 24; icon.color: theme.colors.textSecondary
                    display: AbstractButton.IconOnly
                    enabled: !backend.busy
                    onClicked: backend.refresh()
                    background: Rectangle { color: parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
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
                    required property var reading_progress
                    required property var seen
                    width: ListView.view.width
                    height: 112
                    padding: 0
                    background: Rectangle {
                        color: index === documentList.currentIndex ? theme.colors.surfaceAlt : documentDelegate.activeFocus ? theme.colors.surfaceAlt : "transparent"
                        Rectangle { width: 3; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: index === documentList.currentIndex ? theme.accent : "transparent" }
                    }
                    contentItem: ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 17; anchors.rightMargin: 14; anchors.topMargin: 12; anchors.bottomMargin: 11
                        spacing: 5
                        Text { text: title || qsTr("Untitled document"); color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: [author, site_name || source].filter(function(value) { return !!value }).join(" · "); color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: summary || ""; color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: category.toUpperCase(); color: theme.accent; font.family: theme.uiFont; font.pixelSize: 9; font.bold: true }
                            Text { text: reading_time || listening_time; color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: (seen === undefined ? !first_opened_at : !Boolean(seen)) ? qsTr("UNSEEN") : ""; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 8 }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 3
                            color: theme.colors.selection
                            Rectangle { width: parent.width * Math.max(0, Math.min(100, Number(reading_progress) || 0)) / 100; height: parent.height; color: theme.accent }
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
                    color: theme.colors.scrim
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
                color: theme.colors.surfaceAlt
                visible: backend.documents.length === 0
                Column {
                    anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text { text: backend.error || backend.status; color: backend.error.length > 0 ? theme.colors.error : theme.colors.text; font.family: theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }
                }
            }
            Text { Layout.leftMargin: 18; Layout.bottomMargin: 12; text: qsTr("j/k move   ↵ open   Ctrl+← nav   ? shortcuts"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 9 }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: theme.colors.borderStrong }

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
                        icon.width: 22; icon.height: 22; icon.color: theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        enabled: !backend.pendingMoveLocation
                        ToolTip.visible: hovered
                        ToolTip.text: backend.pendingMoveLocation ? qsTr("Wait for the move to finish") : qsTr("Back to library")
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
                        readonly property bool currentLocation: activeDocumentLocation() === "new"
                        icon.width: 24; icon.height: 24; icon.color: currentLocation ? theme.accent : theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        enabled: !backend.busy && !currentLocation
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Inbox")
                        background: Rectangle { color: parent.currentLocation ? theme.colors.selection : parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
                        onClicked: backend.moveDocument(progressDocumentId || selectedDocument().id || "", "new")
                    }
                    ToolButton {
                        objectName: "laterLocationButton"
                        text: qsTr("Later"); icon.source: "qrc:/icons/later.svg"
                        readonly property bool currentLocation: activeDocumentLocation() === "later"
                        icon.width: 24; icon.height: 24; icon.color: currentLocation ? theme.accent : theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        enabled: !backend.busy && !currentLocation
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Later")
                        background: Rectangle { color: parent.currentLocation ? theme.colors.selection : parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
                        onClicked: backend.moveDocument(progressDocumentId || selectedDocument().id || "", "later")
                    }
                    ToolButton {
                        objectName: "archiveLocationButton"
                        text: qsTr("Archive"); icon.source: "qrc:/icons/archive.svg"
                        readonly property bool currentLocation: activeDocumentLocation() === "archive"
                        icon.width: 24; icon.height: 24; icon.color: currentLocation ? theme.accent : theme.colors.textSecondary
                        display: AbstractButton.IconOnly
                        enabled: !backend.busy && !currentLocation
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Move to Archive")
                        background: Rectangle { color: parent.currentLocation ? theme.colors.selection : parent.hovered ? theme.colors.surfaceAlt : "transparent"; radius: 0 }
                        onClicked: backend.moveDocument(progressDocumentId || selectedDocument().id || "", "archive")
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
                    Text { text: backend.selectedAuthor; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 12 }
                    Text { text: backend.selectedTitle || qsTr("Select a document"); color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 26; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { font.family: theme.uiFont;
                    visible: backend.error.length > 0
                    text: backend.error
                    color: theme.colors.error
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.borderStrong }
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
                        onContentYChanged: {
                            if (reading && !pendingProgressRestore)
                                progressSaveTimer.restart()
                            restoreReadingPosition()
                        }
                        onContentHeightChanged: restoreReadingPosition()
                        TextEdit {
                            id: articleText
                            objectName: "articleText"
                            text: backend.busy && backend.selectedContent.length === 0 ? qsTr("Loading document…") : backend.formattedMarkdown(backend.selectedContent, theme.accent, theme.readerStyle, theme.colors)
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
                            color: theme.colors.text
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
                    Timer {
                        id: progressSaveTimer
                        interval: 3500
                        repeat: false
                        onTriggered: {
                            if (backend.busy) {
                                restart()
                                return
                            }
                            const queuedIds = Object.keys(queuedProgressSaves)
                            if (queuedIds.length > 0) {
                                const queuedId = queuedIds[0]
                                const queuedValue = queuedProgressSaves[queuedId]
                                const queued = Object.assign({}, queuedProgressSaves)
                                delete queued[queuedId]
                                queuedProgressSaves = queued
                                backend.saveReadingProgress(queuedId, queuedValue)
                                if (queuedId === progressDocumentId)
                                    lastSavedReadingProgress = queuedValue
                                if (Object.keys(queuedProgressSaves).length > 0)
                                    restart()
                                return
                            }
                            const range = Math.max(1, articleFlickable.contentHeight - articleFlickable.height)
                            currentReadingProgress = Math.min(100, Math.max(0, Math.round(articleFlickable.contentY / range * 100)))
                            highestReadingProgress = Math.max(highestReadingProgress, currentReadingProgress)
                            const furthest = theme.readerStyle.progressMode === "furthest_position"
                            const next = furthest ? highestReadingProgress : currentReadingProgress
                            const shouldSave = furthest
                                ? next >= lastSavedReadingProgress + 5
                                : next !== lastSavedReadingProgress
                            if (reading && shouldSave)
                                queueProgressSave(progressDocumentId || selectedDocument().id || "", next, true)
                        }
                    }
                    Connections {
                        target: backend
                        function onSelectedDocumentChanged() {
                            restoreReadingPosition()
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
                Rectangle { Layout.fillHeight: true; width: 1; color: theme.colors.border; visible: reading }
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
                        background: Rectangle { color: theme.colors.panel }
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
                        background: Rectangle { color: theme.colors.panel }
                        ColumnLayout {
                            x: 17
                            y: 17
                            width: metadataScroll.availableWidth - 34
                            spacing: 14
                            ColumnLayout {
                                visible: metadataTabs.currentIndex === 0
                                Layout.fillWidth: true
                                spacing: 14
                        Text { text: qsTr("DOCUMENT DETAILS"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                        Text { text: backend.selectedTitle || qsTr("Untitled document"); color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { visible: !!selectedDocument().summary; text: selectedDocument().summary || ""; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 6
                            Layout.fillWidth: true
                            Text { text: qsTr("Author"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: backend.selectedAuthor || "—"; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Source"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().site_name || selectedDocument().source || "—"; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Type"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().category || "—"; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Location"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().location || backend.location; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Reading time"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: selectedDocument().reading_time || selectedDocument().listening_time || "—"; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: qsTr("Reading progress"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
                            Text { text: Math.round(Number(selectedDocument().reading_progress) || 0) + "%"; color: theme.colors.textSecondary; font.family: theme.uiFont; font.pixelSize: 10; Layout.fillWidth: true }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: documentIsSeen(selectedDocument()) ? qsTr("Mark unseen") : qsTr("Mark seen")
                            enabled: !!selectedDocument().id && !backend.busy
                            font.family: theme.uiFont
                            Material.roundedScale: Material.NotRounded
                            onClicked: backend.setDocumentSeen(selectedDocument().id || "", !documentIsSeen(selectedDocument()))
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.border }
                        Text { text: qsTr("TAGS"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                                    color: tagHover.hovered ? theme.colors.tagBackground : theme.colors.tagBackground
                                    border.color: theme.colors.border
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Text { id: tagLabel; text: modelData; color: theme.colors.textBright; font.family: theme.uiFont; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
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
                            Text { visible: backend.selectedTags.length === 0; text: qsTr("No tags"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10 }
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
                        Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.border }
                        Text { text: qsTr("DOCUMENT NOTE"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                        Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("HIGHLIGHTS"); color: theme.colors.textMuted; font.family: theme.uiFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                                color: theme.colors.surfaceAlt
                                border.color: theme.colors.borderStrong
                                radius: Math.min(2, theme.radius)
                                ColumnLayout {
                                    id: highlightColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 7
                                    Text {
                                        text: highlightCard.modelData.text || qsTr("Highlight")
                                        color: theme.colors.text
                                        font.family: theme.uiFont; font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.max(highlightNotePreview.implicitHeight + 20, 42)
                                        radius: 3
                                        color: theme.colors.panel
                                        border.color: theme.colors.surfaceAlt
                                        visible: !highlightCard.editingNote
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            Text {
                                                id: highlightNotePreview
                                                text: highlightCard.modelData.notes || qsTr("Add a note to this highlight…")
                                                color: highlightCard.modelData.notes ? theme.colors.textSecondary : theme.colors.textMuted
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
                            color: theme.colors.textMuted
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
            color: theme.colors.panel
            border.color: theme.colors.borderStrong
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
                    color: backend.error.length > 0 ? theme.colors.error : theme.colors.textMuted
                }
                Text {
                    text: backend.error.length > 0 ? backend.error : backend.status
                    color: backend.error.length > 0 ? theme.colors.error : theme.colors.textSecondary
                    font.family: theme.uiFont; font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: reading ? qsTr("Article") : locationLabel(backend.location)
                    color: theme.colors.textMuted
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
        title: qsTr("Omawise settings")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 48)
        height: Math.min(590, window.height - 48)
        padding: 16
        standardButtons: Dialog.NoButton
        background: Rectangle {
            color: theme.colors.surface
            border.color: theme.colors.borderStrong
        }
        contentItem: ScrollView {
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
            width: appearanceDialog.availableWidth
            spacing: 14
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Changes save to reader.toml and apply while Omawise is running.")
                color: theme.colors.textSecondary
                font.family: theme.uiFont
            }
            GridLayout {
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Layout.fillWidth: true
                Label { text: qsTr("Font family"); font.family: theme.uiFont; Layout.minimumWidth: 120; wrapMode: Text.Wrap }
                ComboBox {
                    id: readerFontFamilyPicker
                    objectName: "readerFontFamilyPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 220
                    Layout.preferredHeight: 44
                    editable: true
                    model: ["monospace", "sans-serif", "serif", "Atkinson Hyperlegible", "Inter", "Noto Sans", "Noto Serif"]
                    editText: theme.readerStyle.fontFamily
                    onActivated: updateReaderStyle("fontFamily", currentText)
                    onAccepted: updateReaderStyle("fontFamily", editText)
                }
                Label { text: qsTr("Font size"); font.family: theme.uiFont; Layout.minimumWidth: 120 }
                SpinBox {
                    objectName: "readerFontSizePicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 150
                    Layout.preferredHeight: 44
                    from: 12
                    to: 48
                    value: theme.readerStyle.fontSizePx
                    editable: true
                    onValueModified: updateReaderStyle("fontSizePx", value)
                }
                Label { text: qsTr("Line spacing"); font.family: theme.uiFont; Layout.minimumWidth: 120 }
                SpinBox {
                    objectName: "readerLineSpacingPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 150
                    Layout.preferredHeight: 44
                    from: 10
                    to: 22
                    stepSize: 1
                    value: Math.round(theme.readerStyle.lineSpacing * 10)
                    editable: true
                    textFromValue: function(value, locale) { return (value / 10).toFixed(1) }
                    valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
                    onValueModified: updateReaderStyle("lineSpacing", value / 10)
                }
                Label { text: qsTr("Paragraph spacing"); font.family: theme.uiFont; Layout.minimumWidth: 120; wrapMode: Text.Wrap }
                SpinBox {
                    objectName: "readerParagraphSpacingPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 150
                    Layout.preferredHeight: 44
                    from: 0
                    to: 36
                    value: theme.readerStyle.paragraphSpacingPt
                    editable: true
                    onValueModified: updateReaderStyle("paragraphSpacingPt", value)
                }
                Label { text: qsTr("Content width"); font.family: theme.uiFont; Layout.minimumWidth: 120; wrapMode: Text.Wrap }
                SpinBox {
                    objectName: "readerContentWidthPicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 150
                    Layout.preferredHeight: 44
                    from: 480
                    to: 1200
                    stepSize: 20
                    value: theme.readerStyle.contentWidth
                    editable: true
                    onValueModified: updateReaderStyle("contentWidth", value)
                }
                Label { text: qsTr("Progress behavior"); font.family: theme.uiFont; Layout.minimumWidth: 120; wrapMode: Text.Wrap }
                ComboBox {
                    id: progressModePicker
                    objectName: "readingProgressModePicker"
                    Material.roundedScale: Material.NotRounded
                    Layout.fillWidth: true
                    Layout.minimumWidth: 220
                    Layout.preferredHeight: 44
                    model: [qsTr("Last position"), qsTr("Furthest position")]
                    currentIndex: theme.readerStyle.progressMode === "furthest_position" ? 1 : 0
                    onActivated: updateReaderStyle("progressMode", index === 1 ? "furthest_position" : "last_position")
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("External editors")
                color: theme.colors.text
                font.family: theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Choose which installed editors appear under Open in… . Editors that are not installed stay configured but are hidden from the menu.")
                color: theme.colors.textMuted
                font.family: theme.uiFont
                font.pixelSize: 10
            }
            Repeater {
                model: backend.externalEditorOptions()
                delegate: CheckBox {
                    required property var modelData
                    objectName: "externalEditor_" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.installed
                          ? modelData.label
                          : qsTr("%1 (not installed)").arg(modelData.label)
                    checked: theme.readerStyle.externalEditors.indexOf(modelData.id) >= 0
                    onToggled: setExternalEditorEnabled(modelData.id, checked)
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Settings file: ~/.config/omawise/reader.toml")
                color: theme.colors.textMuted
                font.family: theme.uiFont
                font.pixelSize: 10
            }
            Label {
                visible: theme.readerStyleError.length > 0
                text: theme.readerStyleError
                color: theme.colors.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            }
        }
        footer: Item {
            implicitHeight: 60
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 12
                spacing: 8
                Button {
                    Material.roundedScale: Material.NotRounded
                    text: qsTr("Reset defaults")
                    font.family: theme.uiFont
                    Layout.minimumWidth: 128
                    Layout.preferredHeight: 40
                    onClicked: resetReaderStyle()
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: appearanceCloseButton
                    objectName: "readerAppearanceCloseButton"
                    Material.roundedScale: Material.NotRounded
                    text: qsTr("Close")
                    font.family: theme.uiFont
                    Layout.minimumWidth: 96
                    Layout.preferredHeight: 40
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
        MenuItem {
            text: qsTr("Reset reading progress")
            objectName: "resetReadingProgressButton"
            enabled: !!selectedDocument().id && Math.max(Number(selectedDocument().reading_progress || 0),
                currentReadingProgress, highestReadingProgress) > 0
            onTriggered: resetReadingProgress()
        }
        MenuItem {
            text: qsTr("Open original")
            objectName: "openOriginalArticleButton"
            enabled: originalDocumentUrl().length > 0
            onTriggered: backend.openExternalUrl(originalDocumentUrl())
        }
        MenuItem {
            text: qsTr("Copy document URL")
            objectName: "copyOriginalDocumentUrlButton"
            enabled: originalDocumentUrl().length > 0
            onTriggered: backend.copyDocumentUrl(originalDocumentUrl())
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
        Menu {
            title: qsTr("Open in…")
            objectName: "openArticleInMenu"
            width: 280
            MenuItem {
                text: qsTr("Open in Reader")
                enabled: backend.selectedUrl.length > 0
                onTriggered: Qt.openUrlExternally(backend.selectedUrl)
            }
            MenuSeparator {}
            Repeater {
                model: backend.availableExternalEditors(theme.readerStyle.externalEditors)
                delegate: MenuItem {
                    required property var modelData
                    text: qsTr("Open in %1").arg(modelData.label)
                    onTriggered: openSelectedArticleInEditor(modelData.id)
                }
            }
        }
    }

    Dialog {
        Material.roundedScale: Material.NotRounded
        id: accountDialog
        objectName: "readwiseAccountDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 48)
        padding: 18
        title: qsTr("Readwise account")
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.colors.surface; border.color: theme.colors.borderStrong }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.colors.textSecondary
                font.family: theme.uiFont
                text: qsTr("The official CLI sign-in enables hybrid search, highlights, and annotations. The optional Reader API token speeds up library and article loading and saves reading progress directly. Without the API token, Omawise uses the CLI for library and article loading too.")
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.borderStrong }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Label { text: qsTr("Readwise CLI"); color: theme.colors.text; font.family: theme.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold }
                Label {
                    objectName: "cliAccountStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: backend.authenticationRequired ? theme.colors.error : theme.colors.textSecondary
                    font.family: theme.uiFont
                    text: backend.authenticationRequired
                          ? qsTr("Sign-in required. Continue to Readwise in your browser to connect.")
                          : backend.cliSignedIn
                            ? qsTr("Connected. Omawise uses this sign-in for search, highlights, and annotation features.")
                            : backend.cliConnectionKnown
                              ? qsTr("The CLI connection needs attention. Sign in again to reconnect.")
                              : qsTr("Not connected yet. Sign in to enable search, highlights, and annotation features.")
                }
                Label {
                    visible: backend.error.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: theme.colors.error
                    font.family: theme.uiFont
                    text: backend.error
                }
                Button {
                    objectName: "cliAccountSignInButton"
                    Material.roundedScale: Material.NotRounded
                    font.family: theme.uiFont
                    text: backend.cliSignedIn ? qsTr("Sign in again") : qsTr("Sign in with Readwise")
                    enabled: !backend.busy
                    onClicked: backend.login()
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: theme.colors.textMuted
                    font.family: theme.uiFont
                    font.pixelSize: 10
                    text: qsTr("The official CLI currently has no standalone sign-out command. Its sign-in remains managed by the CLI.")
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.borderStrong }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Label { text: qsTr("Direct Reader API (optional)"); color: theme.colors.text; font.family: theme.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: backend.needsApiToken ? theme.colors.error : theme.colors.textSecondary
                    font.family: theme.uiFont
                    text: backend.apiTokenAvailable
                          ? qsTr("Connected. The token is stored in your desktop keyring.")
                          : backend.needsApiToken
                            ? qsTr("The saved token was rejected. Connect again with a valid token.")
                            : qsTr("Not connected. You can still use Omawise through the CLI; the API token is optional.")
                }
                RowLayout {
                    Button {
                        objectName: "accountConnectApiButton"
                        Material.roundedScale: Material.NotRounded
                        font.family: theme.uiFont
                        text: backend.apiTokenAvailable ? qsTr("Replace token") : qsTr("Connect API token")
                        enabled: backend.apiTokenReady && !backend.busy && !backend.apiLoginBusy
                        onClicked: apiTokenDialog.open()
                    }
                    Button {
                        objectName: "accountDisconnectApiButton"
                        visible: backend.apiTokenAvailable || backend.needsApiToken
                        Material.roundedScale: Material.NotRounded
                        font.family: theme.uiFont
                        text: qsTr("Disconnect")
                        enabled: !backend.busy && !backend.apiLoginBusy
                        onClicked: backend.forgetApiToken()
                    }
                }
            }
            Label {
                visible: backend.apiLoginError.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.colors.error
                font.family: theme.uiFont
                text: backend.apiLoginError
            }
        }
        footer: RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            Layout.bottomMargin: 12
            Item { Layout.fillWidth: true }
            Button {
                objectName: "readwiseAccountCloseButton"
                Material.roundedScale: Material.NotRounded
                font.family: theme.uiFont
                text: qsTr("Close")
                onClicked: accountDialog.close()
            }
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: apiTokenDialog
        objectName: "apiTokenDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 48)
        padding: 18
        title: qsTr("Connect Readwise API")
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.colors.surface; border.color: theme.colors.borderStrong }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                text: qsTr("Paste a Readwise access token. Omawise validates it and stores it in your desktop keyring; it is never written to app settings or passed in command arguments.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: theme.colors.textSecondary
                font.family: theme.uiFont
            }
            Button {
                text: qsTr("Get an access token from Readwise")
                Material.roundedScale: Material.NotRounded
                font.family: theme.uiFont
                Layout.minimumHeight: 40
                onClicked: backend.openExternalUrl("https://readwise.io/access_token")
            }
            TextField {
                id: apiTokenField
                objectName: "apiTokenField"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                Material.roundedScale: Material.NotRounded
                font.family: theme.uiFont
                placeholderText: qsTr("Readwise access token")
                echoMode: TextInput.Password
                selectByMouse: true
                enabled: !backend.apiLoginBusy
                onAccepted: if (!backend.apiLoginBusy) backend.saveApiToken(text)
            }
            Label {
                visible: backend.apiLoginError.length > 0
                text: backend.apiLoginError
                color: theme.colors.error
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
            spacing: 10
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            Layout.bottomMargin: 12
            Item { Layout.fillWidth: true }
            Button {
                Material.roundedScale: Material.NotRounded
                text: qsTr("Cancel")
                font.family: theme.uiFont
                Layout.minimumWidth: 96
                Layout.preferredHeight: 40
                enabled: !backend.apiLoginBusy
                onClicked: { apiTokenField.clear(); apiTokenDialog.close() }
            }
            Button {
                Material.roundedScale: Material.NotRounded
                text: qsTr("Connect")
                font.family: theme.uiFont
                Layout.minimumWidth: 104
                Layout.preferredHeight: 40
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
        padding: 10
        title: qsTr("Command palette")
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.colors.surface; border.color: theme.colors.borderStrong }
        contentItem: ColumnLayout {
            spacing: 2
            Repeater {
                model: [
                    { label: qsTr("Refresh current view"), key: "refresh" },
                    { label: qsTr("Open selected document"), key: "open" },
                    { label: qsTr("Archive selected document"), key: "archive" },
                    { label: qsTr("Save URL to Reader"), key: "saveUrl", enabled: !backend.busy },
                    { label: documentIsSeen(selectedDocument()) ? qsTr("Mark selected document unseen") : qsTr("Mark selected document seen"), key: "toggleSeen", enabled: !!selectedDocument().id && !backend.busy },
                    { label: qsTr("Readwise account settings"), key: "account" },
                    { label: qsTr("Sign in to Readwise CLI"), key: "login" },
                    { label: qsTr("Disconnect direct API token"), key: "forgetApi", enabled: backend.apiTokenAvailable && !backend.busy },
                    { label: qsTr("Clear cached article content"), key: "clearCache" },
                    { label: qsTr("Keyboard shortcuts"), key: "help" },
                    { label: qsTr("Back to library"), key: "back" }
                ]
                delegate: ItemDelegate { font.family: theme.uiFont;
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 42
                    Material.roundedScale: Material.NotRounded
                    text: modelData.label
                    enabled: modelData.enabled !== false
                    onClicked: runCommand(modelData.key)
                }
            }
        }
    }

    Dialog {
        Material.roundedScale: Material.NotRounded
        id: saveUrlDialog
        objectName: "saveUrlDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 48)
        padding: 18
        title: qsTr("Save URL to Reader")
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.colors.surface; border.color: theme.colors.borderStrong }
        contentItem: ColumnLayout {
            spacing: 14
            TextField {
                id: saveUrlField
                objectName: "saveUrlField"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                Material.roundedScale: Material.NotRounded
                font.family: theme.uiFont
                placeholderText: qsTr("https://example.com/article")
                inputMethodHints: Qt.ImhUrlCharactersOnly
                enabled: !backend.busy
                onAccepted: if (!backend.busy) { backend.saveUrl(text); clear(); saveUrlDialog.close() }
            }
        }
        footer: Item {
            implicitHeight: 60
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 8
                anchors.bottomMargin: 12
                spacing: 10
                Item { Layout.fillWidth: true }
                Button {
                    Material.roundedScale: Material.NotRounded
                    text: qsTr("Cancel")
                    font.family: theme.uiFont
                    Layout.minimumWidth: 96
                    Layout.preferredHeight: 40
                    onClicked: saveUrlDialog.close()
                }
                Button {
                    Material.roundedScale: Material.NotRounded
                    text: qsTr("Save")
                    font.family: theme.uiFont
                    Layout.minimumWidth: 96
                    Layout.preferredHeight: 40
                    enabled: saveUrlField.text.trim().length > 0 && !backend.busy
                    onClicked: { backend.saveUrl(saveUrlField.text); saveUrlField.clear(); saveUrlDialog.close() }
                }
            }
        }
        onOpened: saveUrlField.forceActiveFocus()
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: helpDialog
        objectName: "helpDialog"
        visible: showHelp
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(720, window.width - 48)
        height: Math.min(620, window.height - 72)
        padding: 12
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.colors.surface; border.color: theme.colors.borderStrong }
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
                    Layout.minimumWidth: 96
                    Layout.preferredHeight: 40
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
            color: theme.colors.selection
            radius: 8
            border.color: theme.colors.borderStrong
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
                    color: parent.hovered ? theme.colors.surfaceAlt : "transparent"
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
