#include "backend.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QTest>
#include <QtTest>

#include "theme.h"

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void listAndFeedUseExpectedCliArguments();
    void searchMergesLocalAndRemoteResults();
    void moveDocumentUsesSupportedLocationCommand();
    void authenticationErrorsAreDetectedAndClearedOnRetry();
    void documentNoteUsesBulkMetadataCommand();
    void documentNoteReportsPerItemFailure();
    void highlightMetadataUsesSupportedCliCommands();
    void markdownIsFormattedAsRichText();
    void detailAcceptsMarkdownJsonString();
    void qmlSearchActivationAndReaderSelection();

private:
    QTemporaryDir m_tempDir;
    QString m_binDir;
    QString m_logPath;
    QByteArray m_originalPath;
};

void BackendTests::initTestCase() {
    QVERIFY(m_tempDir.isValid());
    m_binDir = m_tempDir.filePath("bin");
    QVERIFY(QDir().mkpath(m_binDir));
    m_logPath = m_tempDir.filePath("cli-args.log");

    QFile cli(m_binDir + "/readwise");
    QVERIFY(cli.open(QIODevice::WriteOnly | QIODevice::Text));
    cli.write("#!/bin/sh\n"
              "printf '%s\\n' \"$*\" >> \"$OMAWISE_TEST_LOG\"\n"
              "case \"$*\" in\n"
              "  *reader-bulk-edit-document-metadata*) if [ \"$OMAWISE_NOTE_FAILURE\" = 1 ]; then printf '%s\\n' '{\"results\":[{\"id\":\"doc-42\",\"success\":false,\"error\":\"Document not found\"}]}' ; else printf '%s\\n' '{\"results\":[{\"id\":\"doc-42\",\"success\":true}]}' ; fi ;;\n"
              "  *--title-search*) printf '%s\\n' '{\"results\":[{\"id\":\"title-only\",\"title\":\"Title-only result\"}]}' ;;\n"
              "  *reader-search-documents*) printf '%s\\n' '{\"results\":[{\"id\":\"remote\",\"title\":\"Remote result\"}]}' ;;\n"
              "  *reader-list-documents*) if [ \"$OMAWISE_AUTH_FAILURE\" = 1 ]; then printf '%s\\n' '401 Unauthorized' >&2; exit 1; else printf '%s\\n' '{\"results\":[{\"id\":\"local\",\"title\":\"Local article\",\"author\":\"A. Writer\"}]}' ; fi ;;\n"
              "  *reader-get-document-details*) if [ \"$OMAWISE_DETAIL_JSON_STRING\" = 1 ]; then printf '%s\\n' '\"# JSON article\\n\\nBody paragraph.\"' ; else printf '%s\\n' '# Article' '' 'First paragraph for selection.' '' 'Second paragraph for keyboard navigation.' ; fi ;;\n"
              "  *reader-set-highlight-notes*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-add-tags-to-highlight*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-remove-tags-from-highlight*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-move-documents*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-get-document-highlights*) printf '%s\\n' '{\"results\":[{\"id\":\"highlight-1\",\"text\":\"Saved passage\",\"notes\":\"Existing highlight note\",\"tags\":[\"research\"]}]}' ;;\n"
              "  *reader-create-highlight*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *) printf '%s\\n' '{\"results\":[{\"id\":\"local\",\"title\":\"Local article\",\"author\":\"A. Writer\"}]}' ;;\n"
              "esac\n");
    cli.close();
    QVERIFY(cli.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    m_originalPath = qgetenv("PATH");
    qputenv("PATH", (m_binDir.toLocal8Bit() + ":" + m_originalPath));
    qputenv("OMAWISE_TEST_LOG", m_logPath.toLocal8Bit());
}

void BackendTests::listAndFeedUseExpectedCliArguments() {
    Backend backend;
    QSignalSpy changed(&backend, &Backend::documentsChanged);

    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().size(), 1);
    QCOMPARE(backend.documents().first().toMap().value("id").toString(), QString("local"));

    backend.setLocation("feed");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.location(), QString("feed"));
    QVERIFY(changed.count() >= 2);

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray calls = log.readAll();
    QVERIFY(calls.contains("reader-list-documents --location new --limit 50"));
    QVERIFY(calls.contains("reader-list-documents --location feed --limit 50"));
}

void BackendTests::searchMergesLocalAndRemoteResults() {
    Backend backend;
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    backend.search("local");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().size(), 3);
    QCOMPARE(backend.documents().at(0).toMap().value("id").toString(), QString("local"));
    QCOMPARE(backend.documents().at(1).toMap().value("id").toString(), QString("remote"));
    QCOMPARE(backend.documents().at(2).toMap().value("id").toString(), QString("title-only"));

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(log.readAll().contains("reader-search-documents --query local --title-search local --limit 50"));
}

void BackendTests::moveDocumentUsesSupportedLocationCommand() {
    Backend backend;
    backend.moveDocument("doc-42", "later");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.status(), QString("Document moved to later"));

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(log.readAll().contains("reader-move-documents --document-ids doc-42 --location later"));
}

void BackendTests::authenticationErrorsAreDetectedAndClearedOnRetry() {
    qputenv("OMAWISE_AUTH_FAILURE", "1");
    Backend backend;
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.authenticationRequired());
    QVERIFY(backend.error().contains("401 Unauthorized"));

    qunsetenv("OMAWISE_AUTH_FAILURE");
    backend.refresh();
    QVERIFY(!backend.authenticationRequired());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
}

void BackendTests::documentNoteUsesBulkMetadataCommand() {
    Backend backend;
    backend.saveDocumentNote("doc-42", "A careful note");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedNote(), QString("A careful note"));
    QCOMPARE(backend.status(), QString("Note saved"));

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(log.readAll().contains(
        "reader-bulk-edit-document-metadata --documents [{\"document_id\":\"doc-42\",\"notes\":\"A careful note\"}]"));
}

void BackendTests::documentNoteReportsPerItemFailure() {
    qputenv("OMAWISE_NOTE_FAILURE", "1");
    Backend backend;
    backend.saveDocumentNote("doc-42", "A careful note");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.error().contains("Document not found"));
    QVERIFY(backend.selectedNote().isEmpty());
    qunsetenv("OMAWISE_NOTE_FAILURE");
}

void BackendTests::highlightMetadataUsesSupportedCliCommands() {
    Backend backend;
    backend.openDocument("doc-42", "Article", "", "", {"retained-tag"}, "retained note");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(backend.selectedHighlights().size(), 1, 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"retained-tag"}));
    QCOMPARE(backend.selectedNote(), QString("retained note"));

    backend.saveHighlightNote("doc-42", "highlight-1", "A useful annotation");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedHighlights().first().toMap().value("notes").toString(), QString("A useful annotation"));

    backend.addHighlightTags("doc-42", "highlight-1", {"field notes", "Research"});
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QStringList tags = backend.selectedHighlights().first().toMap().value("tags").toStringList();
    QVERIFY(tags.contains("research"));
    QVERIFY(tags.contains("field notes"));
    QVERIFY(!tags.contains("Research"));

    backend.removeHighlightTags("doc-42", "highlight-1", {"research"});
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    tags = backend.selectedHighlights().first().toMap().value("tags").toStringList();
    QVERIFY(!tags.contains("research"));

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray calls = log.readAll();
    QVERIFY(calls.contains("reader-set-highlight-notes --document-id doc-42 --highlight-document-id highlight-1 --notes A useful annotation"));
    QVERIFY(calls.contains("reader-add-tags-to-highlight --document-id doc-42 --highlight-document-id highlight-1 --tag-names [\"field notes\",\"Research\"]"));
    QVERIFY(calls.contains("reader-remove-tags-from-highlight --document-id doc-42 --highlight-document-id highlight-1 --tag-names [\"research\"]"));
}

void BackendTests::markdownIsFormattedAsRichText() {
    Backend backend;
    const QString html = backend.formattedMarkdown("# Heading\n\nA [link](https://example.com).", "#12ab34");
    QVERIFY(html.contains("Heading"));
    QVERIFY(html.contains("href=\"https://example.com\""));
    QVERIFY(html.contains("#12ab34", Qt::CaseInsensitive));
}

void BackendTests::detailAcceptsMarkdownJsonString() {
    qputenv("OMAWISE_DETAIL_JSON_STRING", "1");
    Backend backend;
    backend.openDocument("doc-json", "JSON detail", "", "");
    QTRY_COMPARE_WITH_TIMEOUT(backend.selectedContent(), QString("# JSON article\n\nBody paragraph."), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    qunsetenv("OMAWISE_DETAIL_JSON_STRING");
}

void BackendTests::qmlSearchActivationAndReaderSelection() {
    Backend backend;
    Theme theme;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    Q_INIT_RESOURCE(resources);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "Main.qml failed to load");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->show();
    const auto clickObject = [window](QObject *object) {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item)
            return false;
        const QPoint point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        return true;
    };

    QObject *search = window->findChild<QObject *>(QStringLiteral("searchField"));
    QObject *list = window->findChild<QObject *>(QStringLiteral("documentList"));
    QObject *openButton = window->findChild<QObject *>(QStringLiteral("openSelectedDocumentButton"));
    QObject *signInButton = window->findChild<QObject *>(QStringLiteral("authSignInButton"));
    QVERIFY(search);
    QVERIFY(list);
    QVERIFY(openButton);
    QVERIFY(signInButton);
    QVERIFY(!signInButton->property("visible").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    QVERIFY(QMetaObject::invokeMethod(search, "forceActiveFocus"));
    search->setProperty("text", QStringLiteral("Remote"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().size(), 2);
    QCOMPARE(backend.documents().first().toMap().value("id").toString(), QStringLiteral("remote"));
    QCOMPARE(backend.documents().at(1).toMap().value("id").toString(), QStringLiteral("title-only"));
    QCOMPARE(engine.rootObjects().first()->property("reading").toBool(), false);
    QVERIFY(window->findChild<QObject *>(QStringLiteral("documentMetadataPane")));

    QVERIFY(QMetaObject::invokeMethod(list, "forceActiveFocus"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(backend.selectedHighlights().size(), 1, 3000);
    QCOMPARE(backend.selectedContent().contains(QStringLiteral("First paragraph")), true);
    QCOMPARE(engine.rootObjects().first()->property("reading").toBool(), true);
    QObject *article = window->findChild<QObject *>(QStringLiteral("articleText"));
    QVERIFY(article);
    QVERIFY(QMetaObject::invokeMethod(article, "forceActiveFocus"));
    QTRY_VERIFY_WITH_TIMEOUT(article->property("activeFocus").toBool(), 1000);
    QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Down);
    QTRY_VERIFY_WITH_TIMEOUT(article->property("selectedText").toString().contains(QStringLiteral("First paragraph")), 1000);
    QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(article->property("selectedText").toString().size() > 0);

    QVERIFY(QMetaObject::invokeMethod(article, "select", Q_ARG(int, 0), Q_ARG(int, 12)));
    QVERIFY(article->property("selectedText").toString().size() > 0);
    QTest::keyClick(window, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    // Mouse activation verifies the cached-selection path, which must survive
    // the toolbar button taking focus before the click handler runs.
    QVERIFY(QMetaObject::invokeMethod(article, "select", Q_ARG(int, 0), Q_ARG(int, 12)));
    QObject *highlightButtonObject = window->findChild<QObject *>(QStringLiteral("highlightSelectionButton"));
    auto *highlightButton = qobject_cast<QQuickItem *>(highlightButtonObject);
    QVERIFY(highlightButton);
    const QPoint highlightPoint = highlightButton->mapToScene(
        QPointF(highlightButton->width() / 2, highlightButton->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, highlightPoint);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    QObject *noteEditor = window->findChild<QObject *>(QStringLiteral("documentNoteEditor"));
    QObject *noteSave = window->findChild<QObject *>(QStringLiteral("saveDocumentNoteButton"));
    QObject *metadataTabs = window->findChild<QObject *>(QStringLiteral("metadataTabs"));
    QVERIFY(noteEditor);
    QVERIFY(noteSave);
    QVERIFY(metadataTabs);
    metadataTabs->setProperty("currentIndex", 1);
    auto *noteSaveItem = qobject_cast<QQuickItem *>(noteSave);
    QVERIFY(noteSaveItem);
    const auto noteSavePoint = [window, noteSaveItem] {
        return noteSaveItem->mapToScene(
            QPointF(noteSaveItem->width() / 2, noteSaveItem->height() / 2)).toPoint();
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        QRect(QPoint(0, 0), QSize(window->width(), window->height())).contains(noteSavePoint()), 2000);
    noteEditor->setProperty("text", QStringLiteral("A note from the metadata pane"));
    QVERIFY(clickObject(noteSave));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedNote(), QString("A note from the metadata pane"));

    QObject *tagEditor = window->findChild<QObject *>(QStringLiteral("newDocumentTagEditor"));
    QObject *tagSave = window->findChild<QObject *>(QStringLiteral("addDocumentTagButton"));
    metadataTabs->setProperty("currentIndex", 0);
    QVERIFY(tagEditor);
    QVERIFY(tagSave);
    tagEditor->setProperty("text", QStringLiteral("alpha"));
    QVERIFY(clickObject(tagSave));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"alpha"}));
    tagEditor->setProperty("text", QStringLiteral("beta"));
    QVERIFY(clickObject(tagSave));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"alpha", "beta"}));

    QObject *highlightRepeater = window->findChild<QObject *>(QStringLiteral("highlightRepeater"));
    QVERIFY(highlightRepeater);
    QTRY_COMPARE_WITH_TIMEOUT(highlightRepeater->property("count").toInt(), 1, 2000);

    engine.rootObjects().first()->setProperty("showHelp", true);
    QTRY_VERIFY(window->findChild<QObject *>(QStringLiteral("helpCloseButton")) != nullptr);
    QObject *helpCloseObject = window->findChild<QObject *>(QStringLiteral("helpCloseButton"));
    QVERIFY(clickObject(helpCloseObject));
    QTRY_VERIFY(!engine.rootObjects().first()->property("showHelp").toBool());

    engine.rootObjects().first()->setProperty("reading", false);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(openButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.rootObjects().first()->property("reading").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    engine.rootObjects().first()->setProperty("reading", false);
    qputenv("OMAWISE_AUTH_FAILURE", "1");
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.authenticationRequired());
    QVERIFY(signInButton->property("visible").toBool());
    qunsetenv("OMAWISE_AUTH_FAILURE");
    QVERIFY(clickObject(signInButton));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray calls = log.readAll();
    QVERIFY(calls.count("reader-create-highlight --document-id remote") >= 2);
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    BackendTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "omawise_tests.moc"
