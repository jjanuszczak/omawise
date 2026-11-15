#include "backend.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTextDocument>
#include <QTextBlock>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QClipboard>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QTest>
#include <QtTest>
#include <algorithm>

#include "theme.h"

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void listAndFeedUseExpectedCliArguments();
    void listPaginationAppendsAndDeduplicates();
    void searchMergesLocalAndRemoteResults();
    void moveDocumentUsesSupportedLocationCommand();
    void authenticationErrorsAreDetectedAndClearedOnRetry();
    void documentNoteUsesBulkMetadataCommand();
    void documentNoteReportsPerItemFailure();
    void highlightMetadataUsesSupportedCliCommands();
    void markdownIsFormattedAsRichText();
    void markdownKeepsParagraphSpacingInRichText();
    void copyArticleMarkdownAddsYamlFrontmatter();
    void readerStyleUsesDefaultsAndReloadsBoundedToml();
    void detailAcceptsMarkdownJsonString();
    void persistentArticleCacheLoadsImmediatelyAndClears();
    void persistentLibraryCacheLoadsBeforeRefresh();
    void cachedArticleOpenLatencyDistribution();
    void cachedLibraryFirstFrameLatencyDistribution();
    void qmlSearchActivationAndReaderSelection();

private:
    QTemporaryDir m_tempDir;
    QString m_binDir;
    QString m_logPath;
    QByteArray m_originalPath;
};

void BackendTests::initTestCase() {
    qputenv("XDG_DATA_HOME", m_tempDir.filePath("xdg-data").toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", m_tempDir.filePath("xdg-config").toLocal8Bit());
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/article-cache")).removeRecursively();
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
              "  *--title-search*) printf '%s\\n' '{\"results\":[{\"document_id\":\"title-only\",\"title\":\"Title-only result\"}]}' ;;\n"
              "  *reader-search-documents*) printf '%s\\n' '{\"results\":[{\"id\":\"remote\",\"title\":\"Remote result\"}]}' ;;\n"
              "  *--page-cursor*) if [ \"$OMAWISE_PAGINATION_TEST\" = 1 ]; then printf '%s\\n' '{\"results\":[{\"id\":\"second\",\"title\":\"Second page\"},{\"id\":\"local\",\"title\":\"Duplicate row\"}],\"nextPageCursor\":null}' ; else printf '%s\\n' '{\"results\":[]}' ; fi ;;\n"
              "  *reader-list-documents*) if [ \"$OMAWISE_AUTH_FAILURE\" = 1 ]; then printf '%s\\n' '401 Unauthorized' >&2; exit 1; elif [ \"$OMAWISE_PAGINATION_TEST\" = 1 ]; then printf '%s\\n' '{\"results\":[{\"id\":\"local\",\"title\":\"Local article\",\"author\":\"A. Writer\"}],\"nextPageCursor\":\"cursor-1\"}' ; else printf '%s\\n' '{\"results\":[{\"id\":\"local\",\"title\":\"Local article\",\"author\":\"A. Writer\"}]}' ; fi ;;\n"
              "  *reader-get-document-details*) if [ \"$OMAWISE_DETAIL_FAIL\" = 1 ]; then printf '%s\\n' 'temporary detail failure' >&2; exit 1; elif [ \"$OMAWISE_DETAIL_DELAY\" = 1 ]; then sleep 1; fi; if [ \"$OMAWISE_DETAIL_JSON_STRING\" = 1 ]; then printf '%s\\n' '\"# JSON article\\n\\nBody paragraph.\"' ; elif [ \"$OMAWISE_FORMATTING_FIXTURE\" = 1 ]; then printf '%s\\n' '# Article' '' '## Section heading' '' 'First paragraph for selection with several lines of text to check readable measure and line spacing.' '' 'Second paragraph for keyboard navigation.' '' '> A useful blockquote for the article preview.' '' '- First list item' '- Second list item' '' '`inline code` and [a sample link](https://example.com).' '' '```cpp' 'int answer = 42;' '```' ; else printf '%s\\n' '# Article' '' 'First paragraph for selection.' '' 'Second paragraph for keyboard navigation.' ; fi ;;\n"
              "  *reader-set-highlight-notes*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-add-tags-to-highlight*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-remove-tags-from-highlight*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-move-documents*) printf '%s\\n' '{\"success\":true}' ;;\n"
              "  *reader-get-document-highlights*) printf '%s\\n' '{\"results\":[{\"id\":\"highlight-1\",\"text\":\"Saved passage\",\"notes\":\"Existing highlight note\",\"tags\":[\"research\"]}]}' ;;\n"
              "  *reader-list-tags*) printf '%s\\n' '{\"results\":[{\"key\":\"shared\",\"name\":\"shared\"},{\"key\":\"technology\",\"name\":\"technology\"}]}' ;;\n"
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

void BackendTests::listPaginationAppendsAndDeduplicates() {
    qputenv("OMAWISE_PAGINATION_TEST", "1");
    Backend backend;
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().size(), 1);
    QVERIFY(backend.hasMoreDocuments());

    backend.loadMoreDocuments();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().size(), 2);
    QCOMPARE(backend.documents().at(0).toMap().value("id").toString(), QString("local"));
    QCOMPARE(backend.documents().at(1).toMap().value("id").toString(), QString("second"));
    QVERIFY(!backend.hasMoreDocuments());

    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(log.readAll().contains("reader-list-documents --location new --limit 50 --page-cursor cursor-1"));
    qunsetenv("OMAWISE_PAGINATION_TEST");
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
    const qsizetype editAt = calls.indexOf("reader-set-highlight-notes --document-id doc-42");
    const qsizetype refreshedAt = calls.lastIndexOf("reader-get-document-highlights --document-id doc-42", editAt);
    QVERIFY(refreshedAt >= 0 && editAt > refreshedAt);
}

void BackendTests::markdownIsFormattedAsRichText() {
    Backend backend;
    const QString html = backend.formattedMarkdown("# Heading\n\nA [link](https://example.com).", "#12ab34");
    QVERIFY(html.contains("Heading"));
    QVERIFY(html.contains("href=\"https://example.com\""));
    QVERIFY(html.contains("#12ab34", Qt::CaseInsensitive));
}

void BackendTests::markdownKeepsParagraphSpacingInRichText() {
    Backend backend;
    const QVariantMap style{{"fontSizePx", 12}, {"lineSpacing", 1.2}, {"paragraphSpacingPt", 18}};
    const QString html = backend.formattedMarkdown("First paragraph.\n\nSecond paragraph.", "#12ab34", style);
    QTextDocument rendered;
    rendered.setHtml(html);
    const QTextBlock paragraph = rendered.begin();
    QVERIFY(paragraph.isValid());
    QCOMPARE(paragraph.text(), QStringLiteral("First paragraph."));
    QVERIFY(paragraph.blockFormat().bottomMargin() >= 24.0);
}

void BackendTests::copyArticleMarkdownAddsYamlFrontmatter() {
    Backend backend;
    const QVariantMap metadata{
        {"id", "doc-42"},
        {"title", "A \"quoted\" title"},
        {"author", "A. Writer"},
        {"url", "https://example.com/article"},
        {"site_name", "example.com"},
        {"category", "article"},
        {"location", "new"},
        {"published_date", "2026-11-15"},
        {"tags", QStringList({"research", "long tag"})}
    };
    QVERIFY(backend.copyArticleMarkdown("# Article\n\nBody paragraph.", metadata));
    QString copied = QGuiApplication::clipboard()->text();
    QVERIFY(copied.startsWith("---\nid: \"doc-42\"\ntitle: \"A \\\"quoted\\\" title\"\n"));
    QVERIFY(copied.contains("author: \"A. Writer\""));
    QVERIFY(copied.contains("url: \"https://example.com/article\""));
    QVERIFY(copied.contains("source: \"example.com\""));
    QVERIFY(copied.contains("published_date: \"2026-11-15\""));
    QVERIFY(copied.contains("tags:\n  - \"research\"\n  - \"long tag\""));
    QVERIFY(copied.endsWith("# Article\n\nBody paragraph.\n"));

    QVERIFY(backend.copyArticleMarkdown("<h1>HTML title</h1><p>HTML body.</p>", metadata));
    copied = QGuiApplication::clipboard()->text();
    QVERIFY(copied.contains("# HTML title"));
    QVERIFY(copied.contains("HTML body."));
}

void BackendTests::readerStyleUsesDefaultsAndReloadsBoundedToml() {
    QVERIFY(QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)));
    const QString configDirectory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                   + QStringLiteral("/omawise");
    QDir(configDirectory).removeRecursively();
    Theme theme;
    QCOMPARE(theme.readerStyle().value("fontFamily").toString(), QStringLiteral("monospace"));
    QCOMPARE(theme.readerStyle().value("fontSizePx").toInt(), 12);
    QCOMPARE(theme.readerStyle().value("lineSpacing").toDouble(), 1.2);
    QCOMPARE(theme.readerStyle().value("paragraphSpacingPt").toInt(), 18);
    QCOMPARE(theme.readerStyle().value("contentWidth").toInt(), 760);

    QVERIFY(QDir().mkpath(configDirectory));
    QFile settings(configDirectory + QStringLiteral("/reader.toml"));
    QVariantMap editedStyle = theme.readerStyle();
    editedStyle["fontFamily"] = QStringLiteral("Atkinson Hyperlegible");
    editedStyle["fontSizePx"] = 16;
    editedStyle["lineSpacing"] = 1.6;
    editedStyle["paragraphSpacingPt"] = 24;
    editedStyle["contentWidth"] = 920;
    QVERIFY(theme.saveReaderStyle(editedStyle));
    QTRY_COMPARE_WITH_TIMEOUT(theme.readerStyle().value("fontSizePx").toInt(), 16, 1500);
    QVERIFY(settings.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray savedSettings = settings.readAll();
    settings.close();
    QVERIFY(savedSettings.contains("font_family = \"Atkinson Hyperlegible\""));
    QVERIFY(savedSettings.contains("font_size_px = 16"));
    QVERIFY(savedSettings.contains("line_spacing = 1.6"));
    QVERIFY(savedSettings.contains("paragraph_spacing_pt = 24"));
    QVERIFY(savedSettings.contains("content_width = 920"));

    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    settings.write("[reader]\nfont_family = \"Atkinson Hyperlegible\"\nfont_size_px = 200\nline_spacing = 1.7\ncontent_width = 1300\n");
    settings.close();
    QTRY_COMPARE_WITH_TIMEOUT(theme.readerStyle().value("fontSizePx").toInt(), 48, 1500);
    QCOMPARE(theme.readerStyle().value("fontFamily").toString(), QStringLiteral("Atkinson Hyperlegible"));
    QCOMPARE(theme.readerStyle().value("lineSpacing").toDouble(), 1.7);
    QCOMPARE(theme.readerStyle().value("contentWidth").toInt(), 1200);

    QVERIFY(settings.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    settings.write("[reader]\nfont_size_px = invalid\nline_spacing = 0\n");
    settings.close();
    QTRY_COMPARE_WITH_TIMEOUT(theme.readerStyle().value("fontSizePx").toInt(), 12, 1500);
    QCOMPARE(theme.readerStyle().value("lineSpacing").toDouble(), 1.0);
    QDir(configDirectory).removeRecursively();
}

void BackendTests::detailAcceptsMarkdownJsonString() {
    qputenv("OMAWISE_DETAIL_JSON_STRING", "1");
    Backend backend;
    backend.openDocument("doc-json", "JSON detail", "", "");
    QTRY_COMPARE_WITH_TIMEOUT(backend.selectedContent(), QString("# JSON article\n\nBody paragraph."), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    qunsetenv("OMAWISE_DETAIL_JSON_STRING");
}

void BackendTests::persistentArticleCacheLoadsImmediatelyAndClears() {
    const QString expected = QStringLiteral("# Article\n\nFirst paragraph for selection.\n\nSecond paragraph for keyboard navigation.");
    {
        Backend first;
        first.openDocument("persistent-article", "Persisted title", "Writer", "https://example.com/article");
        QTRY_VERIFY_WITH_TIMEOUT(first.selectedContent().startsWith(expected), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!first.busy(), 3000);
    }
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    const QByteArray key = QCryptographicHash::hash(QByteArrayLiteral("persistent-article"), QCryptographicHash::Sha256).toHex();
    const QString articleFile = cacheRoot + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".json");
    QVERIFY2(QFile::exists(articleFile), qPrintable(articleFile));
    const QFile::Permissions permissions = QFileInfo(articleFile).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));

    Backend second;
    second.openDocument("persistent-article", "", "", "");
    QCOMPARE(second.selectedContent().trimmed(), expected);
    QCOMPARE(second.selectedTitle(), QString("Persisted title"));
    QCOMPARE(second.selectedAuthor(), QString("Writer"));
    QCOMPARE(second.selectedUrl(), QString("https://example.com/article"));
    QTRY_VERIFY_WITH_TIMEOUT(!second.busy(), 3000);

    qputenv("OMAWISE_DETAIL_FAIL", "1");
    Backend staleFallback;
    staleFallback.openDocument("persistent-article", "", "", "");
    QCOMPARE(staleFallback.selectedContent().trimmed(), expected);
    QTRY_VERIFY_WITH_TIMEOUT(!staleFallback.busy(), 3000);
    QVERIFY(staleFallback.error().contains("temporary detail failure"));
    QCOMPARE(staleFallback.selectedContent().trimmed(), expected);
    qunsetenv("OMAWISE_DETAIL_FAIL");

    QFile agedArticle(articleFile);
    QVERIFY(agedArticle.open(QIODevice::ReadWrite));
    QVERIFY(agedArticle.setFileTime(QDateTime::currentDateTime().addDays(-31), QFileDevice::FileModificationTime));
    agedArticle.close();
    Backend expired;
    expired.openDocument("persistent-article", "", "", "");
    QVERIFY(expired.selectedContent().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!expired.busy(), 3000);

    second.clearArticleCache();
    QVERIFY(!QFile::exists(articleFile));
}

void BackendTests::persistentLibraryCacheLoadsBeforeRefresh() {
    {
        Backend first;
        first.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!first.busy(), 3000);
    }
    QList<qint64> samples;
    for (int i = 0; i < 21; ++i) {
        Backend second;
        QElapsedTimer timer;
        timer.start();
        second.refresh();
        QVERIFY(!second.documents().isEmpty());
        QCOMPARE(second.documents().first().toMap().value("id").toString(), QString("local"));
        samples.append(timer.nsecsElapsed() / 1000000);
        QTRY_VERIFY_WITH_TIMEOUT(!second.busy(), 3000);
    }
    std::sort(samples.begin(), samples.end());
    const qint64 p50 = samples.at(samples.size() / 2);
    const int p95Index = (samples.size() * 95 + 99) / 100 - 1;
    const qint64 p95 = samples.at(p95Index);
    qInfo().noquote() << "cached_library_refresh_to_rows_ms p50=" << p50 << "p95=" << p95;
    const QString libraryRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    const QByteArray locationKey = QCryptographicHash::hash(QByteArrayLiteral("new"), QCryptographicHash::Sha256).toHex();
    const QString libraryFile = QDir(libraryRoot).filePath(QStringLiteral("library-")
        + QString::fromLatin1(locationKey) + QStringLiteral(".json"));
    QVERIFY(QFile::exists(libraryFile));
    QFile file(libraryFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument cached = QJsonDocument::fromJson(file.readAll());
    QVERIFY(cached.isObject());
    QVERIFY(!cached.object().value(QStringLiteral("rows")).toArray().first().toObject().contains(QStringLiteral("notes")));
    QVERIFY(!cached.object().value(QStringLiteral("rows")).toArray().first().toObject().contains(QStringLiteral("tags")));
}

void BackendTests::cachedArticleOpenLatencyDistribution() {
    const QString expected = QStringLiteral("# Article\n\nFirst paragraph for selection.\n\nSecond paragraph for keyboard navigation.");
    {
        Backend seed;
        seed.openDocument("latency-article", "Latency sample", "Writer", "https://example.com/latency");
        QTRY_VERIFY_WITH_TIMEOUT(seed.selectedContent().startsWith(expected), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!seed.busy(), 3000);
    }

    QList<qint64> samples;
    for (int i = 0; i < 21; ++i) {
        Backend backend;
        QElapsedTimer timer;
        timer.start();
        backend.openDocument("latency-article", "", "", "");
        QVERIFY(backend.selectedContent().startsWith(expected));
        samples.append(timer.nsecsElapsed() / 1000000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    }
    std::sort(samples.begin(), samples.end());
    const qint64 p50 = samples.at(samples.size() / 2);
    const int p95Index = (samples.size() * 95 + 99) / 100 - 1;
    const qint64 p95 = samples.at(p95Index);
    qInfo().noquote() << "cached_article_open_to_content_property_ms p50=" << p50 << "p95=" << p95;
}

void BackendTests::cachedLibraryFirstFrameLatencyDistribution() {
    {
        Backend seed;
        seed.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!seed.busy(), 3000);
        seed.openDocument("local", "Local article", "A. Writer", "https://example.com/local");
        QTRY_VERIFY_WITH_TIMEOUT(seed.selectedContent().contains("First paragraph"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!seed.busy(), 3000);
    }
    Q_INIT_RESOURCE(resources);
    QList<qint64> samples;
    QList<qint64> articleSamples;
    for (int i = 0; i < 21; ++i) {
        Backend backend;
        Theme theme;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        QElapsedTimer timer;
        timer.start();
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QSignalSpy frameSpy(window, &QQuickWindow::frameSwapped);
        window->show();
        window->requestUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 3000);
        QObject *list = window->findChild<QObject *>(QStringLiteral("documentList"));
        QVERIFY(list);
        QVERIFY(list->property("count").toInt() > 0);
        samples.append(timer.nsecsElapsed() / 1000000);

        const int framesBeforeOpen = frameSpy.count();
        timer.restart();
        QVERIFY(QMetaObject::invokeMethod(engine.rootObjects().first(), "openSelected"));
        QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > framesBeforeOpen, 3000);
        QObject *article = window->findChild<QObject *>(QStringLiteral("articleText"));
        QVERIFY(article);
        QVERIFY(article->property("text").toString().contains(QStringLiteral("First paragraph")));
        articleSamples.append(timer.nsecsElapsed() / 1000000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    }
    std::sort(samples.begin(), samples.end());
    const qint64 p50 = samples.at(samples.size() / 2);
    const int p95Index = (samples.size() * 95 + 99) / 100 - 1;
    const qint64 p95 = samples.at(p95Index);
    qInfo().noquote() << "cached_library_to_first_rendered_frame_ms p50=" << p50 << "p95=" << p95;
    std::sort(articleSamples.begin(), articleSamples.end());
    const qint64 articleP50 = articleSamples.at(articleSamples.size() / 2);
    const int articleP95Index = (articleSamples.size() * 95 + 99) / 100 - 1;
    const qint64 articleP95 = articleSamples.at(articleP95Index);
    qInfo().noquote() << "cached_article_open_to_rendered_frame_ms p50=" << articleP50 << "p95=" << articleP95;
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
    QObject *apiConnectButton = window->findChild<QObject *>(QStringLiteral("connectApiTokenButton"));
    QVERIFY(search);
    QVERIFY(list);
    QVERIFY(openButton);
    QVERIFY(signInButton);
    QVERIFY(apiConnectButton);
    QVERIFY(!signInButton->property("visible").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.apiTokenReady(), 3000);
    QVERIFY(QMetaObject::invokeMethod(apiConnectButton, "clicked"));
    QObject *apiDialog = window->findChild<QObject *>(QStringLiteral("apiTokenDialog"));
    QObject *apiTokenField = window->findChild<QObject *>(QStringLiteral("apiTokenField"));
    QVERIFY(apiDialog);
    QVERIFY(apiTokenField);
    QVERIFY(apiDialog->property("visible").toBool());
    QCOMPARE(apiTokenField->property("echoMode").toInt(), 2);
    apiTokenField->setProperty("text", QStringLiteral("discard-this-test-value"));
    QVERIFY(QMetaObject::invokeMethod(apiDialog, "close"));
    QTRY_VERIFY(!apiDialog->property("visible").toBool());
    QVERIFY(apiTokenField->property("text").toString().isEmpty());

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
    qputenv("OMAWISE_FORMATTING_FIXTURE", "1");
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(backend.selectedHighlights().size(), 1, 3000);
    QCOMPARE(backend.selectedContent().contains(QStringLiteral("First paragraph")), true);
    QCOMPARE(engine.rootObjects().first()->property("reading").toBool(), true);
    QObject *article = window->findChild<QObject *>(QStringLiteral("articleText"));
    QVERIFY(article);
    QVERIFY(window->grabWindow().save(QStringLiteral("/tmp/omawise-article-toolbar.png")));
    QObject *inboxButton = window->findChild<QObject *>(QStringLiteral("inboxLocationButton"));
    QObject *laterButton = window->findChild<QObject *>(QStringLiteral("laterLocationButton"));
    QObject *archiveButton = window->findChild<QObject *>(QStringLiteral("archiveLocationButton"));
    QObject *appearanceButton = window->findChild<QObject *>(QStringLiteral("readerAppearanceButton"));
    QVERIFY(inboxButton);
    QVERIFY(laterButton);
    QVERIFY(archiveButton);
    QVERIFY(appearanceButton);
    QVERIFY(QMetaObject::invokeMethod(appearanceButton, "clicked"));
    QObject *appearanceDialog = window->findChild<QObject *>(QStringLiteral("readerAppearanceDialog"));
    QVERIFY(appearanceDialog);
    QVERIFY(appearanceDialog->property("visible").toBool());
    QVERIFY(window->findChild<QObject *>(QStringLiteral("readerFontFamilyPicker")));
    QVERIFY(window->findChild<QObject *>(QStringLiteral("readerFontSizePicker")));
    QVERIFY(window->findChild<QObject *>(QStringLiteral("readerLineSpacingPicker")));
    QVERIFY(window->findChild<QObject *>(QStringLiteral("readerParagraphSpacingPicker")));
    QVERIFY(window->findChild<QObject *>(QStringLiteral("readerContentWidthPicker")));
    QObject *appearanceClose = window->findChild<QObject *>(QStringLiteral("readerAppearanceCloseButton"));
    QVERIFY(appearanceClose);
    QVERIFY(clickObject(appearanceClose));
    QTRY_VERIFY_WITH_TIMEOUT(!appearanceDialog->property("visible").toBool(), 1000);
    QVERIFY(!inboxButton->property("enabled").toBool());
    QVERIFY(laterButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(laterButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().first().toMap().value("location").toString(), QStringLiteral("later"));
    QVERIFY(!laterButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(archiveButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().first().toMap().value("location").toString(), QStringLiteral("archive"));
    QVERIFY(QMetaObject::invokeMethod(inboxButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.documents().first().toMap().value("location").toString(), QStringLiteral("new"));
    QVERIFY(QMetaObject::invokeMethod(article, "forceActiveFocus"));
    qunsetenv("OMAWISE_FORMATTING_FIXTURE");
    QTRY_VERIFY_WITH_TIMEOUT(article->property("activeFocus").toBool(), 1000);
    QTest::keyClick(window, Qt::Key_Down);
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
    // the toolbar menu taking focus before the menu action runs.
    QVERIFY(QMetaObject::invokeMethod(article, "select", Q_ARG(int, 0), Q_ARG(int, 12)));
    QObject *moreButtonObject = window->findChild<QObject *>(QStringLiteral("articleMoreButton"));
    auto *moreButton = qobject_cast<QQuickItem *>(moreButtonObject);
    QVERIFY(moreButton);
    QVERIFY(QMetaObject::invokeMethod(moreButton, "clicked"));
    QObject *moreMenu = window->findChild<QObject *>(QStringLiteral("articleMoreMenu"));
    QVERIFY(moreMenu);
    QVERIFY(moreMenu->property("visible").toBool());
    QVERIFY(moreMenu->property("y").toReal() >= moreButton->height());
    QObject *copyMarkdownButton = window->findChild<QObject *>(QStringLiteral("copyArticleMarkdownButton"));
    QVERIFY(copyMarkdownButton);
    QVERIFY(copyMarkdownButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(copyMarkdownButton, "triggered"));
    const QString copiedArticle = QGuiApplication::clipboard()->text();
    QVERIFY(copiedArticle.startsWith("---\n"));
    QVERIFY(copiedArticle.contains("title: \"Remote result\""));
    QVERIFY(copiedArticle.contains("# Article"));
    QObject *highlightButtonObject = window->findChild<QObject *>(QStringLiteral("highlightSelectionButton"));
    auto *highlightButton = qobject_cast<QQuickItem *>(highlightButtonObject);
    QVERIFY(highlightButton);
    QVERIFY(highlightButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(highlightButton, "triggered"));
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
    metadataTabs->setProperty("currentIndex", 0);
    QVERIFY(tagEditor);
    QObject *tagSuggestions = window->findChild<QObject *>(QStringLiteral("documentTagSuggestionsPopup"));
    QVERIFY(tagSuggestions);
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "forceActiveFocus"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.availableDocumentTags().contains(QStringLiteral("shared")), 1500);
    QObject *tagSuggestionList = window->findChild<QObject *>(QStringLiteral("documentTagSuggestionList"));
    QVERIFY(tagSuggestionList);
    tagEditor->setProperty("text", QStringLiteral("sha"));
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "textEdited"));
    QTRY_VERIFY_WITH_TIMEOUT(tagSuggestions->property("visible").toBool(), 1000);
    QVERIFY(tagSuggestionList->property("count").toInt() >= 1);
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "accepted"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"shared"}));
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "forceActiveFocus"));
    tagEditor->setProperty("text", QStringLiteral("alpha"));
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "textEdited"));
    QTRY_VERIFY_WITH_TIMEOUT(tagSuggestions->property("visible").toBool(), 1000);
    QVERIFY(tagSuggestions->property("height").toReal() > 0);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"shared", "alpha"}));
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "forceActiveFocus"));
    tagEditor->setProperty("text", QStringLiteral("beta"));
    QVERIFY(QMetaObject::invokeMethod(tagEditor, "textEdited"));
    QTRY_VERIFY_WITH_TIMEOUT(tagSuggestions->property("visible").toBool(), 1000);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
    QCOMPARE(backend.selectedTags(), QStringList({"shared", "alpha", "beta"}));

    QObject *highlightRepeater = window->findChild<QObject *>(QStringLiteral("highlightRepeater"));
    QVERIFY(highlightRepeater);
    QTRY_COMPARE_WITH_TIMEOUT(highlightRepeater->property("count").toInt(), 1, 2000);

    engine.rootObjects().first()->setProperty("showHelp", true);
    QTRY_VERIFY(window->findChild<QObject *>(QStringLiteral("helpCloseButton")) != nullptr);
    QObject *helpDialog = window->findChild<QObject *>(QStringLiteral("helpDialog"));
    QVERIFY(helpDialog);
    QTRY_VERIFY_WITH_TIMEOUT(helpDialog->property("visible").toBool(), 1000);
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
    QVERIFY(calls.count("reader-get-document-highlights --document-id remote") >= 3);
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Omawise"));
    app.setApplicationName(QStringLiteral("omawise-tests"));
    BackendTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "omawise_tests.moc"
