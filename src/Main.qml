import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1360
    height: 880
    minimumWidth: 980
    minimumHeight: 650
    title: qsTr("Readwise Reader")
    color: "#17191d"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: Material.NotRounded

    property int selectedIndex: 0
    property bool reading: false
    property bool showHelp: false
    property bool showPalette: false
    property string pendingHighlightText: ""
    property string pendingHighlightDocumentId: ""
    property int activeParagraphIndex: -1
    property int keyboardSelectionAnchor: -1
    property int keyboardSelectionCursor: -1
    property bool keyboardSelecting: false

    function selectedDocument() {
        if (selectedIndex < 0 || selectedIndex >= backend.documents.length)
            return ({})
        return backend.documents[selectedIndex]
    }

    function openSelected() {
        const doc = selectedDocument()
        if (doc.id) {
            articleFlickable.contentY = 0
            reading = true
            backend.openDocument(doc.id, doc.title || "", doc.author || "", doc.url || "")
        }
    }

    function activateDocument(index, id) {
        selectedIndex = index
        documentList.currentIndex = index
        articleFlickable.contentY = 0
        reading = true
        const doc = selectedDocument()
        backend.openDocument(id, doc.title || "", doc.author || "", doc.url || "")
    }

    function setLocationAndFocus(location) {
        backend.setLocation(location)
        documentList.forceActiveFocus()
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
        else if (command === "back") reading = false
    }

    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: showPalette = true }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !reading && !searchField.activeFocus && !showPalette; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus && !showPalette; onActivated: showHelp = !showHelp }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus; onActivated: { if (showPalette) showPalette = false; else if (showHelp) showHelp = false; else if (reading) { reading = false; documentList.forceActiveFocus() } } }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; enabled: !showPalette && !backend.busy; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; enabled: !reading && !showPalette && !searchField.activeFocus; onActivated: navList.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; enabled: !reading && !showPalette && !searchField.activeFocus; onActivated: documentList.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Shift+H"; context: Qt.ApplicationShortcut; enabled: reading && !showPalette && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && articleText.selectedText.trim().length > 0; onActivated: createSelectedHighlight() }
    Shortcut { sequence: "Ctrl+Shift+Left"; context: Qt.ApplicationShortcut; enabled: reading && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && !paletteDialog.visible; onActivated: extendArticleWordSelection(-1) }
    Shortcut { sequence: "Ctrl+Shift+Right"; context: Qt.ApplicationShortcut; enabled: reading && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && !paletteDialog.visible; onActivated: extendArticleWordSelection(1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: reading && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && !paletteDialog.visible; onActivated: moveArticleParagraph(-1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: reading && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && !paletteDialog.visible; onActivated: moveArticleParagraph(1) }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: reading && articleText.selectedText.trim().length > 0 && !noteDialog.visible && !tagDialog.visible && !highlightsDialog.visible && !helpDialog.visible && !paletteDialog.visible; onActivated: createSelectedHighlight() }

    Component.onCompleted: {
        backend.refresh()
        documentList.forceActiveFocus()
    }
    onSelectedIndexChanged: documentList.currentIndex = selectedIndex

    Connections {
        target: backend
        function onDocumentsChanged() {
            selectedIndex = backend.documents.length > 0 ? 0 : -1
            documentList.currentIndex = selectedIndex
        }
        function onLocationChanged() {
            for (let i = 0; i < navList.model.length; i++) {
                if (navList.model[i].location === backend.location) {
                    navList.currentIndex = i
                    break
                }
            }
        }
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
                    Rectangle {
                        width: 30; height: 30; radius: Math.min(2, theme.radius)
                        color: theme.accent
                        Text { anchors.centerIn: parent; text: "R"; color: theme.accentForeground; font.bold: true; font.family: theme.uiFont; font.pixelSize: 17 }
                    }
                    Text { text: qsTr("Reader"); color: "#f0f1ed"; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
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
                Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Connect / sign in"); visible: !reading; Layout.fillWidth: true; onClicked: backend.login() }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        ColumnLayout {
            Layout.preferredWidth: 410
            Layout.minimumWidth: 320
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            visible: !reading
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                Layout.leftMargin: 20
                Layout.rightMargin: 16
                Text { text: reading ? qsTr("Library") : locationLabel(backend.location); color: "#f0f1ed"; font.family: theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ToolButton { Material.roundedScale: Material.NotRounded; text: "↻"; font.family: theme.uiFont; font.pixelSize: 17; enabled: !backend.busy; onClicked: backend.refresh(); ToolTip.visible: hovered; ToolTip.text: qsTr("Refresh (R)") }
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
                    enabled: !backend.busy
                    focus: true
                    activeFocusOnTab: true
                    currentIndex: 0
                    highlightMoveDuration: 80
                    delegate: ItemDelegate { font.family: theme.uiFont;
                    id: documentDelegate
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
                        const row = backend.documents[index]
                        activateDocument(index, row ? row.id : id)
                    }
                    }
                    onCurrentIndexChanged: if (currentIndex >= 0) selectedIndex = currentIndex
                    Keys.onPressed: function(event) {
                        if (backend.busy) return
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
                    visible: backend.busy
                    color: "#d917191d"
                    z: 2
                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 76
                        height: 76
                        running: backend.busy
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

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238"; visible: reading }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 360
            Layout.preferredWidth: 700
            visible: reading
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.topMargin: 42; anchors.bottomMargin: 22
                spacing: 13
                RowLayout {
                    Layout.fillWidth: true
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: "←  " + qsTr("Library"); flat: true; onClicked: reading = false }
                    Item { Layout.fillWidth: true }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Open in Reader"); flat: true; enabled: backend.selectedUrl.length > 0; onClicked: Qt.openUrlExternally(backend.selectedUrl) }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                        text: qsTr("Note")
                        flat: true
                        enabled: !!selectedDocument().id && !backend.busy
                        onClicked: { noteEditor.text = backend.selectedNote; noteDialog.open() }
                    }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Archive"); enabled: !backend.busy; onClicked: backend.archiveDocument(selectedDocument().id || "") }
                }
                Text { text: backend.selectedAuthor; color: "#aeb2aa"; font.family: theme.uiFont; font.pixelSize: 12 }
                Text { text: backend.selectedTitle || qsTr("Select a document"); color: "#f1f2ed"; font.family: theme.uiFont; font.pixelSize: 26; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: backend.selectedTags.length > 0
                            ? qsTr("Tags: %1").arg(backend.selectedTags.join(", "))
                            : qsTr("No tags")
                        color: "#9da198"
                        font.family: theme.uiFont; font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                        text: qsTr("Edit tags")
                        enabled: !backend.busy
                        onClicked: {
                            tagEditor.text = backend.selectedTags.join(", ")
                            tagDialog.open()
                        }
                    }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                        text: qsTr("Highlight selection")
                        enabled: !backend.busy && pendingHighlightText.trim().length > 0
                            && pendingHighlightDocumentId === (selectedDocument().id || "")
                        onPressed: cacheArticleSelection()
                        onClicked: createSelectedHighlight()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Save selection as a highlight (Ctrl+Shift+H)")
                    }
                    Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                        text: qsTr("Highlights (%1)").arg(backend.selectedHighlights.length)
                        enabled: !backend.busy
                        onClicked: highlightsDialog.open()
                    }
                }
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
                        contentHeight: articleText.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        TextArea { Material.roundedScale: Material.NotRounded;
                            id: articleText
                            text: backend.busy ? qsTr("Loading document…") : backend.formattedMarkdown(backend.selectedContent, theme.accent)
                            textFormat: TextEdit.RichText
                            width: Math.min(articleScroll.width, 760)
                            x: Math.max(0, (articleScroll.width - width) / 2)
                            height: Math.max(contentHeight + topPadding + bottomPadding, articleScroll.height)
                            padding: 0
                            topPadding: 24
                            bottomPadding: 24
                            readOnly: true
                            selectByMouse: true
                            activeFocusOnTab: true
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
                            font.family: theme.uiFont; font.pixelSize: 17
                            background: null
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
                    Rectangle {
                        anchors.fill: parent
                        visible: backend.busy
                        color: "#d917191d"
                        z: 2
                        BusyIndicator {
                            anchors.centerIn: parent
                            width: 76
                            height: 76
                            running: backend.busy
                            palette.highlight: theme.accent
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
                    visible: backend.busy
                    running: backend.busy
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

    Dialog { Material.roundedScale: Material.NotRounded;
        id: noteDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(560, window.width - 48)
        title: qsTr("Document note")
        standardButtons: Dialog.NoButton
        onAccepted: backend.saveDocumentNote(selectedDocument().id || "", noteEditor.text)
        contentItem: ColumnLayout {
            Label { font.family: theme.uiFont;
                text: qsTr("Saving replaces the current note. Leave it empty to clear it.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextArea { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                id: noteEditor
                objectName: "noteEditor"
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                placeholderText: qsTr("Write a note about this document…")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Cancel"); flat: true; onClicked: noteDialog.reject() }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Save"); onClicked: noteDialog.accept() }
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: tagDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, window.width - 48)
        title: qsTr("Document tags")
        standardButtons: Dialog.NoButton
        onAccepted: backend.saveDocumentTags(selectedDocument().id || "", tagEditor.text.split(","))
        contentItem: ColumnLayout {
            Label { font.family: theme.uiFont;
                text: qsTr("Enter tag names separated by commas. Saving replaces this document’s tag list.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextField { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont;
                id: tagEditor
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. design, research")
                selectByMouse: true
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Cancel"); flat: true; onClicked: tagDialog.reject() }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Save"); onClicked: tagDialog.accept() }
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: highlightsDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(700, window.width - 48)
        height: Math.min(620, window.height - 80)
        title: qsTr("Article highlights")
        standardButtons: Dialog.NoButton
        contentItem: ListView {
            clip: true
            spacing: 8
            model: backend.selectedHighlights
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: highlightColumn.implicitHeight + 24
                radius: Math.min(2, theme.radius)
                color: "#24272c"
                Column {
                    id: highlightColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 7
                    Text {
                        text: modelData.text
                        color: "#e5e6e0"
                        font.family: theme.uiFont; font.pixelSize: 13
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                    Text {
                        visible: !!modelData.notes
                        text: modelData.notes || ""
                        color: "#aeb2aa"
                        font.family: theme.uiFont; font.pixelSize: 11
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                    Text {
                        visible: modelData.tags && modelData.tags.length > 0
                        text: modelData.tags ? qsTr("Tags: %1").arg(modelData.tags.join(", ")) : ""
                        color: theme.accent
                        font.family: theme.uiFont; font.pixelSize: 10
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: backend.selectedHighlights.length === 0
                text: qsTr("No highlights yet. Select text in the article and choose Highlight selection.")
                color: "#9da198"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                width: parent.width - 40
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Close"); onClicked: highlightsDialog.accept() }
        }
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
                    { label: qsTr("Connect or sign in"), key: "login" },
                    { label: qsTr("Keyboard shortcuts"), key: "help" },
                    { label: qsTr("Back to library"), key: "back" }
                ]
                delegate: ItemDelegate { font.family: theme.uiFont;
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    onClicked: runCommand(modelData.key)
                }
            }
        }
    }

    Dialog { Material.roundedScale: Material.NotRounded;
        id: helpDialog
        visible: showHelp
        modal: true
        anchors.centerIn: parent
        width: 460
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.NoButton
        onAccepted: showHelp = false
        contentItem: Column {
            spacing: 9
            Label { font.family: theme.uiFont; text: "j / k                         " + qsTr("Move through documents") }
            Label { font.family: theme.uiFont; text: "Enter                         " + qsTr("Open selected document") }
            Label { font.family: theme.uiFont; text: "/                               " + qsTr("Focus search") }
            Label { font.family: theme.uiFont; text: "Ctrl+K                       " + qsTr("Open command palette") }
            Label { font.family: theme.uiFont; text: "Escape                      " + qsTr("Clear search and return to list") }
            Label { font.family: theme.uiFont; text: "a                               " + qsTr("Archive selected document") }
            Label { font.family: theme.uiFont; text: "↑ / ↓ in article           " + qsTr("Select previous / next paragraph") }
            Label { font.family: theme.uiFont; text: "Shift+Arrow               " + qsTr("Select text by character") }
            Label { font.family: theme.uiFont; text: "Ctrl+Shift+← / →       " + qsTr("Extend selection by word") }
            Label { font.family: theme.uiFont; text: "h                               " + qsTr("Highlight the current selection") }
            Label { font.family: theme.uiFont; text: "Ctrl+Shift+H            " + qsTr("Save selected article text as a highlight") }
            Label { font.family: theme.uiFont; text: "Ctrl+R                       " + qsTr("Refresh current view") }
            Label { font.family: theme.uiFont; text: "Escape                      " + qsTr("Return or close overlay") }
            Label { font.family: theme.uiFont; text: "?                               " + qsTr("Show this shortcut list") }
            Label { font.family: theme.uiFont; text: "Ctrl+← / Ctrl+→        " + qsTr("Focus navigation / document list") }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.NotRounded; font.family: theme.uiFont; text: qsTr("Close"); onClicked: helpDialog.accept() }
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
