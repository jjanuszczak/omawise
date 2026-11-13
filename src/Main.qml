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
    Material.roundedScale: theme.radius

    property int selectedIndex: 0
    property bool reading: false
    property bool showHelp: false
    property bool showPalette: false

    function selectedDocument() {
        if (selectedIndex < 0 || selectedIndex >= backend.documents.length)
            return ({})
        return backend.documents[selectedIndex]
    }

    function openSelected() {
        const doc = selectedDocument()
        if (doc.id) {
            reading = true
            backend.openDocument(doc.id, doc.title || "", doc.author || "", doc.url || "")
        }
    }

    function activateDocument(index, id) {
        selectedIndex = index
        documentList.currentIndex = index
        reading = true
        const doc = selectedDocument()
        backend.openDocument(id, doc.title || "", doc.author || "", doc.url || "")
    }

    function setLocationAndFocus(location) {
        backend.setLocation(location)
        documentList.forceActiveFocus()
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
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus && !showPalette; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus && !showPalette; onActivated: showHelp = !showHelp }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !searchField.activeFocus; onActivated: { if (showPalette) showPalette = false; else if (showHelp) showHelp = false; else if (reading) { reading = false; documentList.forceActiveFocus() } } }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; enabled: !showPalette && !backend.busy; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+Left"; context: Qt.ApplicationShortcut; enabled: !showPalette && !searchField.activeFocus; onActivated: navList.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; enabled: !showPalette && !searchField.activeFocus; onActivated: documentList.forceActiveFocus() }

    Timer {
        id: searchDebounce
        interval: 350
        repeat: false
        onTriggered: backend.search(searchField.text)
    }

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

    RowLayout {
        anchors.fill: parent
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
                        width: 30; height: 30; radius: Math.min(6, theme.radius)
                        color: theme.accent
                        Text { anchors.centerIn: parent; text: "R"; color: theme.accentForeground; font.bold: true; font.pixelSize: 17 }
                    }
                    Text { text: qsTr("Reader"); color: "#f0f1ed"; font.pixelSize: 20; font.weight: Font.DemiBold }
                }
                TextField {
                    id: searchField
                    objectName: "searchField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search library…")
                    selectByMouse: true
                    enabled: !backend.busy
                    activeFocusOnTab: true
                    onTextEdited: searchDebounce.restart()
                    onAccepted: {
                        searchDebounce.stop()
                        backend.search(text)
                        documentList.forceActiveFocus()
                    }
                    Keys.onEscapePressed: function(event) {
                        event.accepted = true
                        searchDebounce.stop()
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
                        { label: qsTr("Archive"), location: "archive", glyph: "▣" }
                    ]
                    delegate: ItemDelegate {
                        required property var modelData
                        width: navList.width
                        height: 40
                        padding: 0
                        enabled: !backend.busy
                        highlighted: backend.location === modelData.location
                        background: Rectangle {
                            radius: Math.min(5, theme.radius)
                            color: highlighted ? "#30352a" : navDelegate.activeFocus ? "#292d32" : "transparent"
                        }
                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 9
                            Text { text: modelData.glyph; color: theme.accent; font.pixelSize: 17 }
                            Text { text: modelData.label; color: "#e4e5e0"; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: backend.location === modelData.location ? "•" : ""; color: theme.accent; font.pixelSize: 13 }
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
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                Text { text: qsTr("j/k move · Enter open · Tab changes pane · ? shortcuts"); color: "#93978f"; font.pixelSize: 9; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Button { text: qsTr("Connect / sign in"); Layout.fillWidth: true; onClicked: backend.login() }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        ColumnLayout {
            Layout.preferredWidth: 410
            Layout.minimumWidth: 320
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0
            visible: !reading
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                Layout.leftMargin: 20
                Layout.rightMargin: 16
                Text { text: reading ? qsTr("Library") : locationLabel(backend.location); color: "#f0f1ed"; font.pixelSize: 19; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ToolButton { text: "↻"; font.pixelSize: 17; enabled: !backend.busy; onClicked: backend.refresh(); ToolTip.visible: hovered; ToolTip.text: qsTr("Refresh (R)") }
            }
            ListView {
                id: documentList
                objectName: "documentList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.documents
                enabled: !backend.busy
                focus: true
                activeFocusOnTab: true
                currentIndex: 0
                highlightMoveDuration: 80
                delegate: ItemDelegate {
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
                        Text { text: title || qsTr("Untitled document"); color: "#e9eae5"; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: [author, site_name || source].filter(function(value) { return !!value }).join(" · "); color: "#aaaDA6"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: summary || ""; color: "#858a83"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: category.toUpperCase(); color: theme.accent; font.pixelSize: 9; font.bold: true }
                            Text { text: reading_time || listening_time; color: "#858a83"; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: first_opened_at ? "" : qsTr("UNREAD"); color: "#abb0a4"; font.pixelSize: 8 }
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
                Layout.fillWidth: true
                Layout.margins: 12
                height: 70
                radius: Math.min(7, theme.radius)
                color: "#272a2f"
                visible: backend.documents.length === 0 || backend.error.length > 0
                Column {
                    anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text { text: backend.error || backend.status; color: "#e5e6e0"; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }
                    Text { text: backend.status; color: "#9da198"; font.pixelSize: 10; width: parent.width }
                }
            }
            Text { Layout.leftMargin: 18; Layout.bottomMargin: 12; text: qsTr("j/k move   ↵ open   Ctrl+← nav   ? shortcuts"); color: "#858a83"; font.pixelSize: 9 }
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
                    Button { text: "←  " + qsTr("Library"); flat: true; onClicked: reading = false }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Open in Reader"); flat: true; enabled: backend.selectedUrl.length > 0; onClicked: Qt.openUrlExternally(backend.selectedUrl) }
                    Button { text: qsTr("Archive"); enabled: !backend.busy; onClicked: backend.archiveDocument(selectedDocument().id || "") }
                }
                Text { text: backend.selectedAuthor; color: "#aeb2aa"; font.pixelSize: 12 }
                Text { text: backend.selectedTitle || qsTr("Select a document"); color: "#f1f2ed"; font.pixelSize: 30; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label {
                    visible: backend.error.length > 0
                    text: backend.error
                    color: "#ffb4ab"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#383b40" }
                ScrollView {
                    id: articleScroll
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.topMargin: 6
                    contentWidth: availableWidth
                    clip: true
                    Column {
                        width: articleScroll.availableWidth
                        TextArea {
                            id: articleText
                            text: backend.busy ? qsTr("Loading document…") : backend.formattedMarkdown(backend.selectedContent, theme.accent)
                            textFormat: TextEdit.RichText
                            width: Math.min(articleScroll.availableWidth, 760)
                            x: Math.max(0, (articleScroll.availableWidth - width) / 2)
                            height: Math.max(contentHeight, articleScroll.availableHeight)
                            padding: 0
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: "#dedfd9"
                            selectionColor: theme.accent
                            selectedTextColor: theme.accentForeground
                            font.pixelSize: 18
                            background: null
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
        width: notice.implicitWidth + 18; height: notice.implicitHeight + 8
        color: theme.accent; z: 2
        Text { id: notice; anchors.centerIn: parent; text: backend.status; color: theme.accentForeground; font.pixelSize: 9; font.bold: true }
    }

    Dialog {
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
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    onClicked: runCommand(modelData.key)
                }
            }
        }
    }

    Dialog {
        visible: showHelp
        modal: true
        anchors.centerIn: parent
        width: 460
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.Ok
        contentItem: Column {
            spacing: 9
            Label { text: "j / k                         " + qsTr("Move through documents") }
            Label { text: "Enter                         " + qsTr("Open selected document") }
            Label { text: "/                               " + qsTr("Focus search") }
            Label { text: "Ctrl+K                       " + qsTr("Open command palette") }
            Label { text: "Escape                      " + qsTr("Clear search and return to list") }
            Label { text: "a                               " + qsTr("Archive selected document") }
            Label { text: "Ctrl+R                       " + qsTr("Refresh current view") }
            Label { text: "Escape                      " + qsTr("Return or close overlay") }
            Label { text: "?                               " + qsTr("Show this shortcut list") }
            Label { text: "Ctrl+← / Ctrl+→        " + qsTr("Focus navigation / document list") }
        }
    }

    function locationLabel(location) {
        if (location === "new") return qsTr("Inbox")
        if (location === "later") return qsTr("Later")
        if (location === "shortlist") return qsTr("Shortlist")
        if (location === "archive") return qsTr("Archive")
        return qsTr("Library")
    }
}
