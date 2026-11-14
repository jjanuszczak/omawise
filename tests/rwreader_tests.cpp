#include "backend.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void listAndFeedUseExpectedCliArguments();
    void searchMergesLocalAndRemoteResults();
    void documentNoteUsesBulkMetadataCommand();
    void documentNoteReportsPerItemFailure();
    void markdownIsFormattedAsRichText();

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
              "printf '%s\\n' \"$*\" >> \"$RWREADER_TEST_LOG\"\n"
              "case \"$*\" in\n"
              "  *reader-bulk-edit-document-metadata*) if [ \"$RWREADER_NOTE_FAILURE\" = 1 ]; then printf '%s\\n' '{\"results\":[{\"id\":\"doc-42\",\"success\":false,\"error\":\"Document not found\"}]}' ; else printf '%s\\n' '{\"results\":[{\"id\":\"doc-42\",\"success\":true}]}' ; fi ;;\n"
              "  *reader-search-documents*) printf '%s\\n' '{\"results\":[{\"id\":\"remote\",\"title\":\"Remote result\"}]}' ;;\n"
              "  *) printf '%s\\n' '{\"results\":[{\"id\":\"local\",\"title\":\"Local article\",\"author\":\"A. Writer\"}]}' ;;\n"
              "esac\n");
    cli.close();
    QVERIFY(cli.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    m_originalPath = qgetenv("PATH");
    qputenv("PATH", (m_binDir.toLocal8Bit() + ":" + m_originalPath));
    qputenv("RWREADER_TEST_LOG", m_logPath.toLocal8Bit());
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
    QCOMPARE(backend.documents().size(), 2);
    QCOMPARE(backend.documents().at(0).toMap().value("id").toString(), QString("local"));
    QCOMPARE(backend.documents().at(1).toMap().value("id").toString(), QString("remote"));
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
    qputenv("RWREADER_NOTE_FAILURE", "1");
    Backend backend;
    backend.saveDocumentNote("doc-42", "A careful note");
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QVERIFY(backend.error().contains("Document not found"));
    QVERIFY(backend.selectedNote().isEmpty());
    qunsetenv("RWREADER_NOTE_FAILURE");
}

void BackendTests::markdownIsFormattedAsRichText() {
    Backend backend;
    const QString html = backend.formattedMarkdown("# Heading\n\nA [link](https://example.com).", "#12ab34");
    QVERIFY(html.contains("Heading"));
    QVERIFY(html.contains("href=\"https://example.com\""));
    QVERIFY(html.contains("#12ab34", Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(BackendTests)
#include "rwreader_tests.moc"
