import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    title: "Reader · Omarchy UX prototype"
    color: "#17191d"
    Material.theme: Material.Dark
    Material.accent: "#d7ff62"

    property int selectedIndex: 0
    property bool reading: false
    property bool showHelp: false
    property bool showPalette: false
    property string query: ""

    ListModel {
        id: documents
        ListElement { title: "The shape of a useful personal knowledge system"; author: "Mira Patel"; source: "mira.blog"; kind: "ARTICLE"; age: "12 min"; location: "new"; summary: "A practical case for keeping capture, reading, and retrieval close together." }
        ListElement { title: "A field guide to small language models"; author: "Research Notes"; source: "research.example"; kind: "PDF"; age: "28 min"; location: "new"; summary: "An overview of compact models, local inference, and trade-offs in deployment." }
        ListElement { title: "The quiet craft of paying attention"; author: "Nora Ellis"; source: "longform.weekly"; kind: "NEWSLETTER"; age: "8 min"; location: "later"; summary: "Notes on attention, reading habits, and creating space for sustained thought." }
        ListElement { title: "Why interfaces should stay fast"; author: "Systems Journal"; source: "systems.example"; kind: "ARTICLE"; age: "6 min"; location: "new"; summary: "Speed changes the feel of a tool: interaction becomes continuous instead of procedural." }
        ListElement { title: "Designing for keyboard fluency"; author: "Studio Almanac"; source: "studio.example"; kind: "VIDEO"; age: "19 min"; location: "shortlist"; summary: "A conversation about shortcuts, focus, and making commands discoverable." }
    }

    function currentDocument() {
        return documents.get(selectedIndex)
    }

    function moveSelection(delta) {
        selectedIndex = Math.max(0, Math.min(documents.count - 1, selectedIndex + delta))
    }

    function command(label) {
        showPalette = false
        if (label === "Open selected document") reading = true
        else if (label === "Back to library") reading = false
        else if (label === "Show keyboard shortcuts") showHelp = true
        else if (label === "Archive selected document") {
            documents.setProperty(selectedIndex, "location", "archive")
            toast.open()
        } else if (label === "Focus search") searchField.forceActiveFocus()
    }

    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: { showPalette = true; paletteField.text = ""; paletteField.forceActiveFocus() } }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: showHelp = !showHelp }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { if (showPalette) showPalette = false; else if (showHelp) showHelp = false; else reading = false } }
    Shortcut { sequence: "j"; context: Qt.ApplicationShortcut; onActivated: if (!reading) moveSelection(1) }
    Shortcut { sequence: "k"; context: Qt.ApplicationShortcut; onActivated: if (!reading) moveSelection(-1) }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; onActivated: reading = true }
    Shortcut { sequence: "a"; context: Qt.ApplicationShortcut; onActivated: command("Archive selected document") }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: "#1d2025"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 18
                RowLayout {
                    spacing: 10
                    Rectangle { width: 29; height: 29; radius: 8; color: "#d7ff62"; Text { anchors.centerIn: parent; text: "R"; color: "#17191d"; font.bold: true } }
                    Text { text: "reader"; color: "#f0f1ed"; font.pixelSize: 21; font.weight: Font.DemiBold }
                }
                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "Search library  /"
                    selectByMouse: true
                    onTextChanged: window.query = text.toLowerCase()
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: [ ["Inbox", "⌂", "new"], ["Later", "◷", "later"], ["Shortlist", "☆", "shortlist"], ["Archive", "▣", "archive"], ["Feed", "≋", "feed"] ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            height: 38
                            radius: 7
                            color: modelData[2] === "new" ? "#30352a" : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                Text { text: modelData[1]; color: "#d7ff62"; font.pixelSize: 17 }
                                Text { text: modelData[0]; color: "#e4e5e0"; font.pixelSize: 14; Layout.fillWidth: true }
                                Text { text: modelData[2] === "new" ? "3" : ""; color: "#a7aaa2"; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#34373c" }
                Text { text: "Sample account · mock data"; color: "#a7aaa2"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        ColumnLayout {
            Layout.preferredWidth: 390
            Layout.fillHeight: true
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Text { text: reading ? "Reading" : "Inbox"; color: "#f0f1ed"; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { text: "5 documents"; color: "#969a93"; font.pixelSize: 12 }
            }
            Repeater {
                model: documents
                delegate: Rectangle {
                    required property int index
                    required property string title
                    required property string author
                    required property string source
                    required property string kind
                    required property string age
                    required property string location
                    required property string summary
                    visible: (!window.query || (title + author + source + kind).toLowerCase().includes(window.query)) && (location !== "archive")
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 112 : 0
                    color: index === window.selectedIndex ? "#292d32" : "transparent"
                    Rectangle { width: 3; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: index === window.selectedIndex ? "#d7ff62" : "transparent" }
                    MouseArea { anchors.fill: parent; onClicked: { window.selectedIndex = index; window.reading = true } }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 16
                        anchors.topMargin: 13
                        anchors.bottomMargin: 12
                        spacing: 5
                        Text { text: title; color: "#e9eae5"; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: author + "  ·  " + source; color: "#aaaDA6"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: summary; color: "#858a83"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: kind; color: "#d7ff62"; font.pixelSize: 9; font.bold: true }
                            Text { text: age; color: "#858a83"; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: location.toUpperCase(); color: "#aeb1aa"; font.pixelSize: 9 }
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Text { Layout.leftMargin: 22; Layout.bottomMargin: 14; text: "j/k move     ↵ open     a archive     ? shortcuts"; color: "#858a83"; font.pixelSize: 10 }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: "#303238" }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: reading
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 64
                anchors.rightMargin: 64
                anchors.topMargin: 44
                anchors.bottomMargin: 30
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "←  Library"; color: "#adb1a8"; font.pixelSize: 12; Layout.fillWidth: true; MouseArea { anchors.fill: parent; onClicked: reading = false } }
                    Button { text: "Later"; flat: true }
                    Button { text: "Archive  A"; flat: true; onClicked: command("Archive selected document") }
                }
                Text { text: currentDocument().source + "  ·  " + currentDocument().age + " read"; color: "#b4b7af"; font.pixelSize: 12 }
                Text { text: currentDocument().title; color: "#f1f2ed"; font.pixelSize: 31; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Text { text: "By " + currentDocument().author; color: "#a4a8a0"; font.pixelSize: 13 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#383b40" }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: articleColumn.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: articleColumn
                        width: parent.width
                        spacing: 18
                        Text { text: currentDocument().summary; color: "#d7ff62"; font.pixelSize: 17; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: "Good reading tools make the distance between finding an idea and doing something with it feel very small. The interface stays out of the way, but keeps the next useful action close at hand."; color: "#dedfd9"; font.pixelSize: 17; lineHeight: 1.5; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: "A keyboard-first workflow is more than a collection of shortcuts. It is a way to keep attention on the material: move through a queue, open a document, mark a passage, capture a thought, and return to the larger context without losing momentum."; color: "#dedfd9"; font.pixelSize: 17; lineHeight: 1.5; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Rectangle {
                            Layout.fillWidth: true
                            height: quote.implicitHeight + 34
                            color: "#30352a"
                            radius: 8
                            Text { id: quote; anchors.fill: parent; anchors.margins: 17; text: "“The best interface is the one that helps you stay with the thing you came to do.”"; color: "#e7eddb"; font.pixelSize: 17; font.italic: true; wrapMode: Text.Wrap }
                        }
                        Text { text: "In a full client, this is where document-specific rendering, stable highlight anchors, reading progress, and annotation controls meet. This prototype uses sample text to explore hierarchy and keyboard flow only."; color: "#dedfd9"; font.pixelSize: 17; lineHeight: 1.5; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        RowLayout {
                            Button { text: "Highlight  H"; onClicked: toast.open() }
                            Button { text: "Add note  N"; onClicked: toast.open() }
                            Item { Layout.fillWidth: true }
                            Text { text: "0% read"; color: "#969a93"; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
        Item { Layout.fillWidth: true; Layout.fillHeight: true; visible: !reading }
    }

    Label {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        topPadding: 5
        leftPadding: 10
        rightPadding: 10
        text: "UX PROTOTYPE · FICTIONAL DATA · NOT CONNECTED TO READWISE"
        color: "#17191d"
        font.pixelSize: 9
        font.bold: true
        background: Rectangle { color: "#d7ff62"; radius: 0 }
    }

    Dialog {
        id: paletteDialog
        visible: showPalette
        modal: true
        x: (window.width - width) / 2
        y: 95
        width: 520
        padding: 12
        background: Rectangle { color: "#25282d"; border.color: "#4a4e54"; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 8
            TextField { id: paletteField; Layout.fillWidth: true; placeholderText: "Type a command…"; onTextChanged: commandList.model = commandNames.filter(function(item) { return item.toLowerCase().includes(text.toLowerCase()) }) }
            Repeater {
                id: commandList
                model: commandNames
                delegate: ItemDelegate {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    onClicked: command(modelData)
                }
            }
        }
    }

    property var commandNames: ["Focus search", "Open selected document", "Back to library", "Archive selected document", "Show keyboard shortcuts"]

    Dialog {
        visible: showHelp
        modal: true
        anchors.centerIn: parent
        width: 470
        title: "Keyboard shortcuts"
        standardButtons: Dialog.Ok
        contentItem: Column {
            spacing: 10
            Label { text: "j / k                         Move through the list" }
            Label { text: "Enter                         Open selected document" }
            Label { text: "/                               Focus library search" }
            Label { text: "Ctrl+K                       Open command palette" }
            Label { text: "a                               Archive selected document" }
            Label { text: "Escape                      Return / close overlay" }
            Label { text: "?                               Show this shortcut list" }
        }
    }

    Popup {
        id: toast
        anchors.centerIn: Overlay.overlay
        width: 260
        height: 48
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        background: Rectangle { color: "#30352a"; radius: 8 }
        contentItem: Label { text: "Prototype action · not synced"; color: "#e9eae5"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        onOpened: closeTimer.start()
        Timer { id: closeTimer; interval: 1800; onTriggered: toast.close() }
    }
}
