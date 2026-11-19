#include "backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QSaveFile>
#include <QSet>
#include <QColor>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QRegularExpression>
#include <QFont>
#include <QGuiApplication>
#include <QClipboard>
#include <QUrl>
#include <QUrlQuery>
#include <QPointer>
#include <QThreadPool>
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#ifdef signals
#undef signals
#define OMAWISE_RESTORE_QT_SIGNALS
#endif
#include <libsecret/secret.h>
#ifdef OMAWISE_RESTORE_QT_SIGNALS
#define signals Q_SIGNALS
#undef OMAWISE_RESTORE_QT_SIGNALS
#endif

namespace {
SecretSchema *apiTokenSchema() {
    return secret_schema_new("org.omawise.ReadwiseApi", SECRET_SCHEMA_NONE,
                             "service", SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
}

QStringList tagNames(const QJsonValue &value) {
    QStringList tags;
    if (value.isArray()) {
        for (const QJsonValue &tag : value.toArray()) {
            if (tag.isString())
                tags.append(tag.toString());
            else if (tag.isObject()) {
                const QString name = tag.toObject().value("name").toString();
                if (!name.isEmpty())
                    tags.append(name);
            }
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString name = it.value().isString() ? it.value().toString() : it.key();
            if (!name.isEmpty())
                tags.append(name);
        }
    }
    return tags;
}

bool containsFailedMove(const QJsonValue &value) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QJsonValue success = object.value(QStringLiteral("success"));
        if (success.isBool() && !success.toBool())
            return true;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (containsFailedMove(it.value()))
                return true;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            if (containsFailedMove(item))
                return true;
        }
    }
    return false;
}

QString moveFailureMessage(const QJsonValue &value) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString &key : {QStringLiteral("error"), QStringLiteral("message"), QStringLiteral("reason")}) {
            const QString message = object.value(key).toString().trimmed();
            if (!message.isEmpty())
                return message;
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString message = moveFailureMessage(it.value());
            if (!message.isEmpty())
                return message;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QString message = moveFailureMessage(item);
            if (!message.isEmpty())
                return message;
        }
    }
    return {};
}

int readingProgressPercent(const QJsonValue &value) {
    if (!value.isDouble())
        return 0;
    const double progress = value.toDouble();
    if (!std::isfinite(progress))
        return 0;
    const double percentage = progress >= 0.0 && progress <= 1.0 ? progress * 100.0 : progress;
    return qBound(0, qRound(percentage), 100);
}

bool isHtmlContent(const QString &content) {
    static const QRegularExpression htmlStart(
        QStringLiteral("^<(?:!doctype|html|article|h[1-6]|p|div|section|main|ul|ol|li|blockquote|pre|table|figure|img|hr|br|a|span|b|i|strong|em|code)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return htmlStart.match(content.trimmed()).hasMatch();
}

QString markdownInlineCode(const QString &source) {
    QString code = source;
    code.replace(QRegularExpression(QStringLiteral("\\r?\\n")), QStringLiteral(" "));
    int longestBacktickRun = 0;
    auto runs = QRegularExpression(QStringLiteral("`+")).globalMatch(code);
    while (runs.hasNext())
        longestBacktickRun = std::max(longestBacktickRun,
                                      static_cast<int>(runs.next().capturedLength()));
    const QString delimiter(longestBacktickRun + 1, QLatin1Char('`'));
    const bool pad = code.startsWith(QLatin1Char('`')) || code.endsWith(QLatin1Char('`'))
        || (code.startsWith(QLatin1Char(' ')) && code.endsWith(QLatin1Char(' '))
            && !code.trimmed().isEmpty());
    return delimiter + (pad ? QStringLiteral(" ") : QString()) + code
        + (pad ? QStringLiteral(" ") : QString()) + delimiter;
}

QString htmlToMarkdown(const QString &html) {
    QString preparedHtml;
    QHash<QString, QString> inlineCodeSpans;
    const QRegularExpression codeOrPre(
        QStringLiteral("(<pre\\b[^>]*>.*?</pre\\s*>)|(<code\\b[^>]*>(.*?)</code\\s*>)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    qsizetype offset = 0;
    int placeholderIndex = 0;
    auto matches = codeOrPre.globalMatch(html);
    while (matches.hasNext()) {
        const auto match = matches.next();
        preparedHtml += html.mid(offset, match.capturedStart() - offset);
        if (match.capturedStart(1) >= 0) {
            preparedHtml += match.captured(1);
        } else {
            QTextDocument codeDocument;
            codeDocument.setDefaultFont(QFont(QStringLiteral("sans-serif")));
            codeDocument.setHtml(QStringLiteral("<html><body>") + match.captured(3)
                                 + QStringLiteral("</body></html>"));
            const QString placeholder = QStringLiteral("OMAWISEINLINECODEPLACEHOLDER%1END")
                .arg(placeholderIndex++);
            preparedHtml += placeholder;
            inlineCodeSpans.insert(placeholder, markdownInlineCode(codeDocument.toPlainText()));
        }
        offset = match.capturedEnd();
    }
    preparedHtml += html.mid(offset);

    QTextDocument document;
    document.setDefaultFont(QFont(QStringLiteral("sans-serif")));
    document.setHtml(preparedHtml);
    QString markdown = document.toMarkdown();
    for (auto it = inlineCodeSpans.cbegin(); it != inlineCodeSpans.cend(); ++it)
        markdown.replace(it.key(), it.value());
    return markdown;
}

QString yamlQuotedString(const QString &value) {
    QJsonArray array;
    array.append(value);
    const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
}

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::finished, this, &Backend::finish);
    connect(&m_noteProcess, &QProcess::finished, this, &Backend::finishNoteWrite);
    connect(&m_noteProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_noteTimeout.stop();
        const QString message = tr("Could not start the Readwise CLI to sync notes.");
        setNoteSyncStatus(message);
        setStatus(message);
        fail(message);
        m_activeNoteWrite = {};
        startNextNoteWrite();
    });
    connect(&m_documentTagsProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_documentTagsInProgress = false;
        m_documentTagsLoaded = true;
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            QJsonParseError parseError;
            const QJsonValue value = QJsonValue::fromJson(m_documentTagsProcess.readAllStandardOutput(), &parseError);
            QJsonArray rows;
            if (value.isArray()) {
                rows = value.toArray();
            } else if (value.isObject()) {
                QJsonObject root = value.toObject();
                for (const QString key : {"results", "tags", "items", "data"}) {
                    if (root.value(key).isArray()) {
                        rows = root.value(key).toArray();
                        break;
                    }
                }
            }
            if (parseError.error == QJsonParseError::NoError) {
                for (const QJsonValue &row : rows) {
                    QString name;
                    if (row.isString())
                        name = row.toString().trimmed();
                    else if (row.isObject()) {
                        const QJsonObject tag = row.toObject();
                        name = tag.value("name").toString(tag.value("key").toString()).trimmed();
                    }
                    if (!name.isEmpty() && !m_availableDocumentTags.contains(name, Qt::CaseInsensitive))
                        m_availableDocumentTags.append(name);
                }
            }
        }
        emit availableDocumentTagsChanged();
    });
    connect(&m_documentTagsProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_documentTagsInProgress = false;
        m_documentTagsLoaded = true;
        emit availableDocumentTagsChanged();
    });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_apiReply) {
            m_timedOut = true;
            m_apiReply->abort();
        } else if (m_process.state() != QProcess::NotRunning) {
            m_timedOut = true;
            m_process.kill();
        }
    });
    m_progressTimeout.setSingleShot(true);
    m_progressTimeout.setInterval(30000);
    connect(&m_progressTimeout, &QTimer::timeout, this, [this] {
        if (m_progressReply) {
            m_progressTimedOut = true;
            m_progressReply->abort();
        } else if (m_progressProcess.state() != QProcess::NotRunning) {
            m_progressTimedOut = true;
            m_progressProcess.kill();
        }
    });
    m_noteTimeout.setSingleShot(true);
    m_noteTimeout.setInterval(30000);
    connect(&m_noteTimeout, &QTimer::timeout, this, [this] {
        if (m_noteProcess.state() != QProcess::NotRunning) {
            m_noteTimedOut = true;
            m_noteProcess.kill();
        }
    });
    connect(&m_progressProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_progressTimeout.stop();
        const QString id = m_activeProgressId;
        const int progress = m_activeProgress;
        m_activeProgressId.clear();
        const QByteArray output = m_progressProcess.readAllStandardOutput();
        const QString standardError = QString::fromUtf8(m_progressProcess.readAllStandardError()).trimmed();
        QString failure;
        if (m_progressTimedOut) {
            m_progressTimedOut = false;
            failure = tr("The Readwise CLI did not respond before the progress update timed out.");
        } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            failure = standardError.isEmpty()
                ? tr("Readwise CLI exited with code %1 while saving progress.").arg(exitCode)
                : standardError;
        }
        completeProgressWrite(id, progress, output, failure);
        if (m_queuedProgressWrites.isEmpty())
            setProgressBusy(false);
        else
            startNextProgressWrite();
    });
    connect(&m_progressProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_progressTimeout.stop();
        const QString id = m_activeProgressId;
        const int progress = m_activeProgress;
        m_activeProgressId.clear();
        m_progressTimedOut = false;
        completeProgressWrite(id, progress, {},
                              tr("Could not start the Readwise CLI to save reading progress."));
        if (m_queuedProgressWrites.isEmpty())
            setProgressBusy(false);
        else
            startNextProgressWrite();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const bool failedMetadata = m_operation == Operation::Highlights;
            const bool failedMove = m_operation == Operation::Move;
            setBusy(false);
            m_operation = Operation::None;
            if (failedMove) {
                m_pendingId.clear();
                clearPendingMove();
            }
            if (failedMetadata) {
                setMetadataBusy(false);
                m_pendingHighlightEdit = PendingHighlightEdit::None;
                m_dispatchingFreshHighlightEdit = false;
            }
            fail(tr("Could not start the Readwise CLI. Install @readwise/cli and make sure 'readwise' is on PATH."));
        }
    });
    connect(&m_network, &QNetworkAccessManager::finished, this, &Backend::handleApiReply);
    setStatus(tr("Checking saved Readwise connection…"));
#ifdef OMAWISE_TESTING
    m_apiTokenReady = true;
#else
    loadApiTokenFromKeyring();
#endif
}

QString Backend::persistentArticleCachePath(const QString &id) const {
    if (id.isEmpty())
        return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    const QByteArray key = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return root + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".json");
}

QString Backend::readPersistentArticle(const QString &id) {
    const QString path = persistentArticleCachePath(id);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return {};
    const QJsonObject entry = document.object();
    if (entry.value(QStringLiteral("id")).toString() != id)
        return {};
    const QDateTime modified = QFileInfo(path).lastModified();
    if (modified.isValid() && modified < QDateTime::currentDateTime().addDays(-30)) {
        QFile::remove(path);
        return {};
    }
    if (m_selectedId == id) {
        if (m_selectedTitle.isEmpty()) m_selectedTitle = entry.value(QStringLiteral("title")).toString();
        if (m_selectedAuthor.isEmpty()) m_selectedAuthor = entry.value(QStringLiteral("author")).toString();
        if (m_selectedUrl.isEmpty()) m_selectedUrl = entry.value(QStringLiteral("url")).toString();
    }
    const QString content = entry.value(QStringLiteral("content")).toString();
    if (content.isEmpty())
        return {};
    file.close();
    QFile touched(path);
    if (touched.open(QIODevice::ReadWrite))
        touched.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return content;
}

void Backend::writePersistentArticle(const QString &id, const QString &content) {
    const QByteArray contentBytes = content.toUtf8();
    if (id.isEmpty() || contentBytes.isEmpty() || contentBytes.size() > MaxPersistentArticleBytes)
        return;
    const QString path = persistentArticleCachePath(id);
    const QString root = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(root)) {
        return;
    }
    QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QJsonObject entry{{QStringLiteral("id"), id},
                            {QStringLiteral("content"), content},
                            {QStringLiteral("title"), m_selectedId == id ? m_selectedTitle : QString()},
                            {QStringLiteral("author"), m_selectedId == id ? m_selectedAuthor : QString()},
                            {QStringLiteral("url"), m_selectedId == id ? m_selectedUrl : QString()},
                            {QStringLiteral("cachedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if (file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        return;
    trimPersistentArticleCache();
}

QString Backend::persistentLibraryCachePath(const QString &location) const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    const QByteArray key = QCryptographicHash::hash(location.toUtf8(), QCryptographicHash::Sha256).toHex();
    return root + QStringLiteral("/library-") + QString::fromLatin1(key) + QStringLiteral(".json");
}

void Backend::readPersistentLibrary(const QString &location) {
    const QString path = persistentLibraryCachePath(location);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QDateTime modified = QFileInfo(path).lastModified();
    if (modified.isValid() && modified < QDateTime::currentDateTime().addSecs(-24 * 60 * 60)) {
        file.close();
        QFile::remove(path);
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() || document.object().value(QStringLiteral("location")).toString() != location)
        return;
    const QJsonObject object = document.object();
    const QVariantList cachedRows = resultRows(QJsonDocument(object.value(QStringLiteral("rows")).toArray()));
    if (cachedRows.isEmpty())
        return;
    m_libraryDocuments = cachedRows;
    m_documents = cachedRows;
    m_nextCursor = object.value(QStringLiteral("nextCursor")).toString();
    emit documentsChanged();
    emit hasMoreDocumentsChanged();
    file.close();
    QFile touched(path);
    if (touched.open(QIODevice::ReadWrite))
        touched.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
}

void Backend::writePersistentLibrary(const QString &location) {
    if (m_libraryDocuments.isEmpty())
        return;
    const QString path = persistentLibraryCachePath(location);
    const QString root = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(root))
        return;
    QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QVariantList cacheRows;
    cacheRows.reserve(m_libraryDocuments.size());
    for (const QVariant &value : m_libraryDocuments) {
        QVariantMap row = value.toMap();
        row.remove(QStringLiteral("notes"));
        row.remove(QStringLiteral("tags"));
        cacheRows.append(row);
    }
    const QJsonArray rows = QJsonArray::fromVariantList(cacheRows);
    const QJsonObject entry{{QStringLiteral("location"), location},
                            {QStringLiteral("rows"), rows},
                            {QStringLiteral("nextCursor"), m_nextCursor}};
    const QByteArray bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    if (bytes.size() > 2 * 1024 * 1024)
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(bytes) < 0 || !file.commit())
        return;
    trimPersistentArticleCache();
}

void Backend::trimPersistentArticleCache() const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    QDir dir(root);
    QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() < right.lastModified();
    });
    qint64 totalBytes = 0;
    QFileInfoList retainedEntries;
    const QDateTime expiry = QDateTime::currentDateTime().addDays(-30);
    for (const QFileInfo &entry : entries) {
        if (entry.lastModified() < expiry) {
            QFile::remove(entry.absoluteFilePath());
            continue;
        }
        retainedEntries.append(entry);
        totalBytes += entry.size();
    }
    for (const QFileInfo &entry : retainedEntries) {
        if (totalBytes <= MaxPersistentArticleCacheBytes)
            break;
        totalBytes -= entry.size();
        QFile::remove(entry.absoluteFilePath());
    }
}

void Backend::clearArticleCache() {
    m_contentCache.clear();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    const QDir dir(root);
    for (const QString &entry : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        if (!entry.startsWith(QStringLiteral("library-")))
            QFile::remove(dir.filePath(entry));
    }
    setStatus(tr("Article cache cleared"));
}

void Backend::clearPersistentReaderCache() {
    m_contentCache.clear();
    m_highlightsCache.clear();
    m_highlightsLoaded.clear();
    m_selectedContent.clear();
    m_selectedHighlights.clear();
    m_selectedId.clear();
    m_selectedTitle.clear();
    m_selectedAuthor.clear();
    m_selectedUrl.clear();
    m_selectedNote.clear();
    m_selectedTags.clear();
    m_availableDocumentTags.clear();
    m_documentTagsLoaded = false;
    m_documentTagsInProgress = false;
    m_documents.clear();
    m_libraryDocuments.clear();
    m_nextCursor.clear();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/article-cache");
    QDir(root).removeRecursively();
    emit documentsChanged();
    emit selectedDocumentChanged();
    emit selectedHighlightsChanged();
    emit hasMoreDocumentsChanged();
    emit availableDocumentTagsChanged();
}

void Backend::setApiLoginBusy(bool busy) {
    if (m_apiLoginBusy == busy)
        return;
    m_apiLoginBusy = busy;
    emit apiLoginBusyChanged();
}

void Backend::setApiLoginError(const QString &error) {
    if (m_apiLoginError == error)
        return;
    m_apiLoginError = error;
    emit apiLoginErrorChanged();
}

void Backend::setApiTokenRejected(bool rejected) {
    if (m_apiTokenRejected == rejected)
        return;
    m_apiTokenRejected = rejected;
    emit apiTokenStateChanged();
}

void Backend::setCliSignedIn(bool signedIn) {
    if (m_cliConnectionKnown && m_cliSignedIn == signedIn)
        return;
    m_cliConnectionKnown = true;
    m_cliSignedIn = signedIn;
    emit cliConnectionStateChanged();
}

void Backend::loadApiTokenFromKeyring() {
    QPointer<Backend> guard(this);
    QThreadPool::globalInstance()->start([guard] {
        SecretSchema *schema = apiTokenSchema();
        GError *error = nullptr;
        gchar *stored = secret_password_lookup_sync(schema, nullptr, &error,
                                                    "service", "readwise-api", nullptr);
        const QString token = stored ? QString::fromUtf8(stored) : QString();
        if (stored)
            secret_password_free(stored);
        if (error)
            g_error_free(error);
        secret_schema_unref(schema);
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard, [guard, token] {
            if (!guard)
                return;
            guard->m_apiToken = token;
            guard->m_apiTokenReady = true;
            emit guard->apiTokenStateChanged();
            if (guard->m_refreshPending) {
                guard->m_refreshPending = false;
                guard->setBusy(false);
                guard->refresh();
            }
        }, Qt::QueuedConnection);
    });
}

void Backend::storeApiTokenInKeyring(const QString &token) {
    setApiLoginBusy(true);
    setApiLoginError({});
    QPointer<Backend> guard(this);
    QThreadPool::globalInstance()->start([guard, token] {
        SecretSchema *schema = apiTokenSchema();
        GError *error = nullptr;
        const QByteArray utf8 = token.toUtf8();
        const gboolean stored = secret_password_store_sync(
            schema, SECRET_COLLECTION_DEFAULT, "Omawise Readwise API token", utf8.constData(),
            nullptr, &error, "service", "readwise-api", nullptr);
        if (error)
            g_error_free(error);
        secret_schema_unref(schema);
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard, [guard, token, stored] {
            if (!guard)
                return;
            guard->setApiLoginBusy(false);
            if (!stored) {
                guard->setApiLoginError(guard->tr("Could not store the token in the desktop keyring. Check that a Secret Service keyring is available."));
                return;
            }
            guard->m_apiToken = token;
            guard->m_apiTokenRejected = false;
            guard->m_apiTokenReady = true;
            emit guard->apiTokenStateChanged();
            guard->clearPersistentReaderCache();
            guard->setStatus(guard->tr("Readwise account connected"));
            guard->refresh();
        }, Qt::QueuedConnection);
    });
}

void Backend::clearApiTokenFromKeyring() {
    setApiLoginBusy(true);
    QPointer<Backend> guard(this);
    const QString previousToken = m_apiToken;
    QThreadPool::globalInstance()->start([guard, previousToken] {
        SecretSchema *schema = apiTokenSchema();
        GError *error = nullptr;
        const gboolean cleared = secret_password_clear_sync(schema, nullptr, &error,
                                                             "service", "readwise-api", nullptr);
        if (error)
            g_error_free(error);
        secret_schema_unref(schema);
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard, [guard, previousToken, cleared] {
            if (!guard)
                return;
            if (!cleared) {
                guard->m_apiToken = previousToken;
                guard->setApiLoginBusy(false);
                guard->setApiLoginError(guard->tr("Could not remove the Readwise token from the desktop keyring."));
                emit guard->apiTokenStateChanged();
                return;
            }
            guard->m_apiToken.clear();
            guard->m_apiTokenRejected = false;
            guard->m_apiTokenReady = true;
            guard->setApiLoginBusy(false);
            guard->setApiLoginError({});
            emit guard->apiTokenStateChanged();
            guard->clearPersistentReaderCache();
            guard->refresh();
        }, Qt::QueuedConnection);
    });
}

void Backend::saveApiToken(const QString &value) {
    if (m_busy || m_progressBusy || m_apiLoginBusy) {
        setApiLoginError(tr("Wait for the current Readwise request to finish."));
        return;
    }
    const QString token = value.trimmed();
    if (token.isEmpty()) {
        setApiLoginError(tr("Enter a Readwise access token."));
        return;
    }
    if (m_apiReply)
        m_apiReply->abort();
    m_pendingApiToken = token;
    setApiLoginError({});
    setApiLoginBusy(true);
    setStatus(tr("Validating Readwise access token…"));
    runApiGet(QUrl(QStringLiteral("https://readwise.io/api/v2/auth/")), Operation::ValidateApiToken, token);
}

void Backend::forgetApiToken() {
    if (m_busy || m_progressBusy || m_apiLoginBusy)
        return;
    m_pendingApiToken.clear();
    m_apiTokenRejected = false;
    clearApiTokenFromKeyring();
}

void Backend::runApiGet(const QUrl &url, Operation operation, const QString &token) {
    if (m_busy || m_apiReply || m_process.state() != QProcess::NotRunning)
        return;
    const QString &authToken = token.isEmpty() ? m_apiToken : token;
    if (authToken.isEmpty() && operation != Operation::ValidateApiToken) {
        setStatus(tr("Connect a Readwise account"));
        return;
    }
    clearError();
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Token ") + authToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_operation = operation;
    setBusy(true);
    m_apiReply = m_network.get(request);
    m_timeout.setInterval(30000);
    m_timeout.start();
}

void Backend::queueProgressWrite(const QString &id, int progress) {
    m_queuedProgressWrites.insert(id, progress);
    setProgressBusy(true);
    startNextProgressWrite();
}

void Backend::startNextProgressWrite() {
    if (m_progressReply || m_progressProcess.state() != QProcess::NotRunning
        || m_queuedProgressWrites.isEmpty())
        return;

    clearError();
    const QString id = m_queuedProgressWrites.keys().first();
    const int progress = m_queuedProgressWrites.take(id);
    m_activeProgressId = id;
    m_activeProgress = progress;

    if (!apiTokenAvailable()) {
        const QString executable = QStandardPaths::findExecutable("readwise");
        if (executable.isEmpty()) {
            m_activeProgressId.clear();
            completeProgressWrite(id, progress, {},
                                  tr("Install the official Readwise CLI or connect a Readwise API token to save reading progress."));
            if (m_queuedProgressWrites.isEmpty())
                setProgressBusy(false);
            else
                startNextProgressWrite();
            return;
        }
        const QJsonObject document{{"document_id", id}, {"reading_progress", progress / 100.0}};
        const QJsonArray documents{document};
        m_progressProcess.setProgram(executable);
        m_progressProcess.setArguments({"--json", "reader-bulk-edit-document-metadata", "--documents",
                                        QString::fromUtf8(QJsonDocument(documents).toJson(QJsonDocument::Compact))});
        m_progressProcess.setProcessChannelMode(QProcess::SeparateChannels);
        m_progressProcess.start();
        m_progressTimeout.start();
        return;
    }

    const QJsonObject update{{"id", id}, {"reading_progress", progress / 100.0}};
    const QJsonObject payload{{"updates", QJsonArray{update}}};
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QUrl url(QStringLiteral("https://readwise.io/api/v3/bulk_update/"));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Token ") + m_apiToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_progressReply = m_network.sendCustomRequest(request, QByteArrayLiteral("PATCH"), body);
    m_progressTimeout.start();
    connect(m_progressReply, &QNetworkReply::finished, this, [this, id, progress] {
        QNetworkReply *reply = m_progressReply;
        if (!reply)
            return;
        m_progressTimeout.stop();
        m_progressReply = nullptr;
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();

        QString failure;
        if (m_progressTimedOut) {
            m_progressTimedOut = false;
            failure = tr("Readwise did not respond before the progress update timed out.");
        } else if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
            failure = tr("Readwise API progress update failed (HTTP %1).").arg(statusCode);
            if (statusCode == 401 || statusCode == 403) {
                setApiTokenRejected(true);
                m_queuedProgressWrites.clear();
            }
        }
        completeProgressWrite(id, progress, responseBody, failure);

        if (m_queuedProgressWrites.isEmpty())
            setProgressBusy(false);
        else
            startNextProgressWrite();
    });
}

void Backend::completeProgressWrite(const QString &id, int progress, const QByteArray &response,
                                    const QString &requestFailure) {
    QString failure = requestFailure;
    if (failure.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            failure = tr("Readwise returned an unrecognized progress update response.");
        } else {
            const QJsonObject root = document.object();
            const QString error = root.value("error").toString();
            if (root.value("isError").toBool() || !error.isEmpty())
                failure = error.isEmpty() ? tr("Readwise rejected the progress update.") : error;
            else if (root.value("success").isBool() && !root.value("success").toBool())
                failure = root.value("message").toString(tr("Readwise rejected the progress update."));
            else {
                const QJsonArray results = root.value("results").toArray();
                if (results.isEmpty()) {
                    failure = tr("Readwise returned no result for the progress update.");
                } else {
                    for (const QJsonValue &value : results) {
                        const QJsonObject result = value.toObject();
                        if (result.value("success").isBool() && !result.value("success").toBool()) {
                            failure = result.value("error").toString(tr("Readwise rejected the progress update."));
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!failure.isEmpty()) {
        setStatus(tr("Reading progress could not be saved"));
        fail(failure);
        return;
    }
    updateDocumentRows("reading_progress", progress, id);
    writePersistentLibrary(m_location);
    setApiTokenRejected(false);
    setStatus(tr("Reading progress saved"));
}

void Backend::setProgressBusy(bool busy) {
    if (m_progressBusy == busy)
        return;
    m_progressBusy = busy;
    emit progressBusyChanged();
}

void Backend::handleApiReply(QNetworkReply *reply) {
    if (reply != m_apiReply)
        return;
    m_timeout.stop();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    m_apiReply = nullptr;
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray responseBody = reply->readAll();
    reply->deleteLater();
    setBusy(false);
    QTimer::singleShot(0, this, &Backend::processQueuedWork);

    if (m_timedOut) {
        m_timedOut = false;
        if (operation == Operation::Move) {
            m_pendingId.clear();
            clearPendingMove();
        }
        if (operation == Operation::ValidateApiToken) {
            setApiLoginBusy(false);
            setApiLoginError(tr("Token validation timed out. Check your connection and try again."));
        } else {
            setStatus(tr("Readwise API request timed out"));
            fail(tr("Readwise did not respond before the timeout."));
        }
        return;
    }

    if (operation == Operation::ValidateApiToken) {
        if (statusCode == 204 && networkError == QNetworkReply::NoError) {
            storeApiTokenInKeyring(m_pendingApiToken);
            m_pendingApiToken.clear();
        } else {
            m_pendingApiToken.clear();
            setApiLoginBusy(false);
            setApiLoginError(statusCode == 401
                                 ? tr("That token was rejected. Check it and try again.")
                                 : tr("Could not validate the token (HTTP %1). Check your connection and try again.").arg(statusCode));
        }
        return;
    }

    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        if (statusCode == 401 || statusCode == 403) {
            setApiTokenRejected(true);
            setApiLoginError(tr("The saved Readwise token was rejected. Connect again with a valid token."));
        }
        setStatus(tr("Readwise API request failed"));
        fail(statusCode == 401 || statusCode == 403
                 ? tr("Readwise rejected the saved access token.")
                 : tr("Readwise API request failed (HTTP %1).").arg(statusCode));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setStatus(tr("Unexpected Readwise API response"));
        fail(tr("Readwise returned unrecognized data."));
        return;
    }
    const QJsonObject root = document.object();
    if (operation == Operation::List) {
        m_libraryDocuments = resultRows(document);
        m_documents = m_libraryDocuments;
        const QString previousCursor = m_nextCursor;
        m_nextCursor = root.value("nextPageCursor").toString();
        if (previousCursor != m_nextCursor)
            emit hasMoreDocumentsChanged();
    writePersistentLibrary(m_location);
        emit documentsChanged();
        setApiTokenRejected(false);
        setStatus(tr("%1 documents").arg(m_documents.size()));
        if (m_documents.isEmpty())
            fail(tr("No documents in this view."));
        return;
    }
    if (operation == Operation::ListMore) {
        const QVariantList page = resultRows(document);
        QSet<QString> seen;
        for (const QVariant &row : m_libraryDocuments)
            seen.insert(row.toMap().value("id").toString());
        for (const QVariant &row : page) {
            const QString id = row.toMap().value("id").toString();
            if (!id.isEmpty() && !seen.contains(id)) {
                m_libraryDocuments.append(row);
                m_documents.append(row);
                seen.insert(id);
            }
        }
        const QString previousCursor = m_nextCursor;
        m_nextCursor = root.value("nextPageCursor").toString();
        if (previousCursor != m_nextCursor)
            emit hasMoreDocumentsChanged();
        setApiTokenRejected(false);
        emit documentsChanged();
        setStatus(tr("%1 documents").arg(m_documents.size()));
        writePersistentLibrary(m_location);
        return;
    }
    if (operation == Operation::Detail || operation == Operation::ApiDetail) {
        QJsonArray rows = root.value("results").toArray();
        if (rows.isEmpty() && root.value("id").isString())
            rows.append(root);
        if (rows.isEmpty() || !rows.first().isObject()) {
            setStatus(tr("Document not found"));
            fail(tr("Readwise returned no details for this document."));
            return;
        }
        const QJsonObject object = rows.first().toObject();
        const QString detailId = object.value("id").toString(m_pendingId);
        QString content = object.value("html_content").toString();
        if (content.isEmpty())
            content = object.value("content").toString();
        m_contentCache.insert(detailId, content);
        writePersistentArticle(detailId, content);
        if (m_selectedId != detailId)
            return;
        const QString title = object.value("title").toString();
        const QString author = object.value("author").toString();
        if (!title.isEmpty()) m_selectedTitle = title;
        if (!author.isEmpty()) m_selectedAuthor = author;
        if (object.value("notes").isString()) m_selectedNote = object.value("notes").toString();
        if (!object.value("tags").isUndefined()) m_selectedTags = tagNames(object.value("tags"));
        m_selectedContent = content;
        emit selectedDocumentChanged();
        setApiTokenRejected(false);
        setStatus(tr("Document opened"));
        loadHighlights();
    }
}

void Backend::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::clearPendingMove() {
    if (m_pendingLocation.isEmpty())
        return;
    m_pendingLocation.clear();
    emit pendingMoveLocationChanged();
}

void Backend::setMetadataBusy(bool busy) {
    if (m_metadataBusy == busy)
        return;
    m_metadataBusy = busy;
    emit metadataBusyChanged();
}

void Backend::setNoteSyncBusy(bool busy) {
    if (m_noteSyncBusy == busy)
        return;
    m_noteSyncBusy = busy;
    emit noteSyncBusyChanged();
}

void Backend::setNoteSyncStatus(const QString &status) {
    if (m_noteSyncStatus == status)
        return;
    m_noteSyncStatus = status;
    emit noteSyncStatusChanged();
}

void Backend::enqueueNoteWrite(const QString &key, const QStringList &arguments) {
    for (int i = 0; i < m_noteWriteQueue.size(); ++i) {
        if (m_noteWriteQueue.at(i).key == key) {
            m_noteWriteQueue[i].arguments = arguments;
            setNoteSyncBusy(true);
            setNoteSyncStatus(tr("Saved here · syncing to Readwise…"));
            startNextNoteWrite();
            return;
        }
    }
    m_noteWriteQueue.enqueue({key, arguments});
    setNoteSyncBusy(true);
    setNoteSyncStatus(tr("Saved here · syncing to Readwise…"));
    startNextNoteWrite();
}

void Backend::startNextNoteWrite() {
    if (m_noteProcess.state() != QProcess::NotRunning)
        return;
    if (m_noteWriteQueue.isEmpty()) {
        m_activeNoteWrite = {};
        setNoteSyncBusy(false);
        if (!m_noteSyncStatus.startsWith(tr("Could not"))
            && !m_noteSyncStatus.startsWith(tr("Note sync failed")))
            setNoteSyncStatus(tr("All note changes synced"));
        return;
    }
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("readwise"));
    if (executable.isEmpty()) {
        m_noteWriteQueue.clear();
        const QString message = tr("Readwise CLI not found. Note changes are saved locally but could not be synced.");
        setNoteSyncBusy(false);
        setNoteSyncStatus(message);
        setStatus(message);
        fail(message);
        return;
    }
    m_activeNoteWrite = m_noteWriteQueue.dequeue();
    setNoteSyncStatus(tr("Saved here · syncing to Readwise…"));
    m_noteTimedOut = false;
    m_noteProcess.setProgram(executable);
    m_noteProcess.setArguments(m_activeNoteWrite.arguments);
    m_noteProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_noteProcess.start();
    m_noteTimeout.start();
}

void Backend::finishNoteWrite(int exitCode, QProcess::ExitStatus exitStatus) {
    m_noteTimeout.stop();
    const QByteArray output = m_noteProcess.readAllStandardOutput();
    const QString standardError = QString::fromUtf8(m_noteProcess.readAllStandardError()).trimmed();
    QString failure;
    if (m_noteTimedOut) {
        m_noteTimedOut = false;
        failure = tr("The Readwise CLI timed out while syncing notes.");
    } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        failure = standardError.isEmpty()
            ? tr("Readwise CLI exited with code %1 while syncing notes.").arg(exitCode)
            : standardError;
    } else {
        QJsonParseError parseError;
        const QJsonValue response = QJsonValue::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (!output.trimmed().isEmpty())
                failure = tr("Readwise returned an invalid response while syncing notes.");
        } else if (response.isObject()) {
            const QJsonObject root = response.toObject();
            failure = root.value(QStringLiteral("error")).toString();
            if (failure.isEmpty() && root.value(QStringLiteral("success")).isBool()
                && !root.value(QStringLiteral("success")).toBool())
                failure = root.value(QStringLiteral("message")).toString(tr("Readwise could not save the note."));
            if (failure.isEmpty() && root.value(QStringLiteral("results")).isArray()) {
                for (const QJsonValue &value : root.value(QStringLiteral("results")).toArray()) {
                    const QJsonObject result = value.toObject();
                    if (result.value(QStringLiteral("success")).isBool()
                        && !result.value(QStringLiteral("success")).toBool()) {
                        failure = result.value(QStringLiteral("error")).toString(
                            tr("Readwise could not save the note."));
                        break;
                    }
                }
            }
        }
    }
    m_activeNoteWrite = {};
    if (!failure.isEmpty()) {
        const QString message = tr("Note sync failed: %1").arg(failure);
        setNoteSyncStatus(message);
        setStatus(message);
        fail(failure);
        if (m_authenticationRequired)
            setCliSignedIn(false);
    } else {
        clearError();
        setCliSignedIn(true);
        if (m_noteWriteQueue.isEmpty()) {
            setNoteSyncStatus(tr("All note changes synced"));
            setStatus(tr("Note saved"));
        } else {
            setNoteSyncStatus(tr("Saved here · syncing to Readwise…"));
        }
    }
    startNextNoteWrite();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::updateDocumentRows(const QString &key, const QVariant &value, const QString &id) {
    const QString targetId = id.isEmpty() ? m_selectedId : id;
    const auto update = [&key, &value, &targetId](QVariantList &rows) {
        for (QVariant &rowValue : rows) {
            QVariantMap row = rowValue.toMap();
            if (row.value("id").toString() != targetId)
                continue;
            row.insert(key, value);
            rowValue = row;
        }
    };
    update(m_documents);
    update(m_libraryDocuments);
    emit documentsChanged();
}

void Backend::fail(const QString &message) {
    const QString lower = message.toLower();
    const bool authenticationRequired = lower.contains("unauthorized")
        || lower.contains("unauthorised") || lower.contains("401")
        || lower.contains("authentication required") || lower.contains("not authenticated")
        || lower.contains("invalid token") || lower.contains("token expired")
        || lower.contains("login required") || lower.contains("sign in required");
    if (m_authenticationRequired != authenticationRequired) {
        m_authenticationRequired = authenticationRequired;
        emit authenticationRequiredChanged();
    }
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::clearError() {
    if (m_error.isEmpty() && !m_authenticationRequired)
        return;
    m_error.clear();
    m_authenticationRequired = false;
    emit errorChanged();
    emit authenticationRequiredChanged();
}

void Backend::run(const QStringList &arguments, Operation operation) {
    if (m_busy || m_process.state() != QProcess::NotRunning || m_apiReply)
        return;

    clearError();
    const QString executable = QStandardPaths::findExecutable("readwise");
    if (executable.isEmpty()) {
        if (operation == Operation::Move) {
            m_pendingId.clear();
            clearPendingMove();
        }
        if (operation == Operation::Highlights) {
            setMetadataBusy(false);
            m_pendingHighlightEdit = PendingHighlightEdit::None;
            m_dispatchingFreshHighlightEdit = false;
        }
        setStatus(tr("Readwise CLI not found"));
        fail(tr("Install the official Readwise CLI with npm install -g @readwise/cli, then restart this app."));
        return;
    }

    m_operation = operation;
    setBusy(true);
    m_process.setProgram(executable);
    m_process.setArguments(arguments);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    m_timeout.setInterval(operation == Operation::Login ? 120000 : 30000);
    m_timeout.start();
}

void Backend::refresh() {
    if (!m_apiTokenReady) {
        m_refreshPending = true;
        setBusy(true);
        setStatus(tr("Checking saved Readwise connection…"));
        return;
    }
    m_activeQuery.clear();
    if (!m_nextCursor.isEmpty()) {
        m_nextCursor.clear();
        emit hasMoreDocumentsChanged();
    }
    readPersistentLibrary(m_location);
    QString viewName = m_location;
    if (m_location == "new") viewName = tr("Inbox");
    else if (m_location == "later") viewName = tr("Later");
    else if (m_location == "shortlist") viewName = tr("Shortlist");
    else if (m_location == "archive") viewName = tr("Archive");
    else if (m_location == "feed") viewName = tr("Feed");
    setStatus(tr("Loading %1…").arg(viewName));
    if (!m_apiToken.isEmpty()) {
        QUrl apiUrl(QStringLiteral("https://readwise.io/api/v3/list/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("location"), m_location);
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
        apiUrl.setQuery(query);
        runApiGet(apiUrl, Operation::List);
    } else {
        run({"--json", "reader-list-documents", "--location", m_location, "--limit", "50"}, Operation::List);
    }
}

void Backend::loadMoreDocuments() {
    if (m_nextCursor.isEmpty() || m_busy || !m_activeQuery.isEmpty())
        return;
    setStatus(tr("Loading more documents…"));
    if (!m_apiToken.isEmpty()) {
        QUrl apiUrl(QStringLiteral("https://readwise.io/api/v3/list/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("location"), m_location);
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
        query.addQueryItem(QStringLiteral("pageCursor"), m_nextCursor);
        apiUrl.setQuery(query);
        runApiGet(apiUrl, Operation::ListMore);
    } else {
        run({"--json", "reader-list-documents", "--location", m_location, "--limit", "50",
             "--page-cursor", m_nextCursor}, Operation::ListMore);
    }
}

void Backend::setLocation(const QString &location) {
    static const QStringList allowed{"new", "later", "shortlist", "archive", "feed"};
    if (!allowed.contains(location) || m_location == location)
        return;
    m_location = location;
    emit locationChanged();
    refresh();
}

void Backend::search(const QString &query) {
    const QString trimmed = query.trimmed();
    m_activeQuery = trimmed;
    if (trimmed.isEmpty()) {
        refresh();
        return;
    }
    if (!m_nextCursor.isEmpty()) {
        m_nextCursor.clear();
        emit hasMoreDocumentsChanged();
    }
    m_localSearchMatches.clear();
    for (const QVariant &value : m_libraryDocuments) {
        const QVariantMap row = value.toMap();
        const QString searchable = row.value("title").toString() + " "
            + row.value("author").toString() + " "
            + row.value("site_name").toString() + " "
            + row.value("source").toString() + " "
            + row.value("summary").toString();
        if (searchable.contains(trimmed, Qt::CaseInsensitive))
            m_localSearchMatches.append(row);
    }
    setStatus(tr("Searching…"));
    run({"--json", "reader-search-documents", "--query", trimmed, "--limit", "50"}, Operation::Search);
}

void Backend::openDocument(const QString &id, const QString &title, const QString &author, const QString &url,
                           const QStringList &tags, const QString &note) {
    if (id.isEmpty())
        return;
    selectDocument(id, title, author, url, tags, note);
    if (!m_contentCache.contains(id)) {
        const QString persisted = readPersistentArticle(id);
        if (!persisted.isEmpty())
            m_contentCache.insert(id, persisted);
    }
    const bool hasCachedContent = m_contentCache.contains(id);
    if (hasCachedContent) {
        m_selectedContent = m_contentCache.value(id);
        emit selectedDocumentChanged();
        setStatus(tr("Opened cached article · refreshing"));
    }
    if (m_process.state() != QProcess::NotRunning || m_apiReply || m_busy) {
        m_hasQueuedOpen = true;
        m_queuedOpenId = id;
        m_queuedOpenTitle = title;
        m_queuedOpenAuthor = author;
        m_queuedOpenUrl = url;
        m_queuedOpenTags = tags;
        m_queuedOpenNote = note;
        setStatus(tr("Opening document after current request…"));
        return;
    }
    m_pendingId = id;
    if (!hasCachedContent) {
        m_selectedContent.clear();
        emit selectedDocumentChanged();
        setStatus(tr("Opening document…"));
    }
    m_pendingId = id;
    if (!m_apiToken.isEmpty()) {
        QUrl apiUrl(QStringLiteral("https://readwise.io/api/v3/list/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("id"), id);
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("withHtmlContent"), QStringLiteral("true"));
        apiUrl.setQuery(query);
        runApiGet(apiUrl, Operation::ApiDetail);
    } else {
        run({"--json", "reader-get-document-details", "--document-id", id}, Operation::Detail);
    }
}

void Backend::selectDocument(const QString &id, const QString &title, const QString &author, const QString &url,
                             const QStringList &tags, const QString &note) {
    if (id.isEmpty())
        return;
    if (m_selectedId != id && m_pendingHighlightEdit != PendingHighlightEdit::None) {
        m_pendingHighlightEdit = PendingHighlightEdit::None;
        m_dispatchingFreshHighlightEdit = false;
    }
    m_selectedId = id;
    m_selectedTitle = title;
    m_selectedAuthor = author;
    m_selectedUrl = url;
    m_selectedLocation = m_location;
    for (const QVariant &value : m_documents) {
        const QVariantMap row = value.toMap();
        if (row.value("id").toString() != id)
            continue;
        const QString rowLocation = row.value("location").toString();
        if (!rowLocation.isEmpty())
            m_selectedLocation = rowLocation == "inbox" ? QStringLiteral("new") : rowLocation;
        break;
    }
    m_selectedNote = note;
    m_selectedTags = tags;
    m_selectedContent = m_contentCache.value(id);
    if (m_highlightsLoaded.contains(id))
        m_selectedHighlights = m_highlightsCache.value(id);
    else
        m_selectedHighlights.clear();
    emit selectedDocumentChanged();
    emit selectedHighlightsChanged();
}

void Backend::archiveDocument(const QString &id) {
    if (id.isEmpty())
        return;
    m_pendingId = id;
    setStatus(tr("Archiving…"));
    run({"--json", "reader-move-documents", "--document-ids", id, "--location", "archive"}, Operation::Archive);
}

void Backend::moveDocument(const QString &id, const QString &location) {
    static const QStringList allowedLocations{"new", "later", "archive"};
    if (id.isEmpty() || !allowedLocations.contains(location) || m_busy
        || m_process.state() != QProcess::NotRunning || m_apiReply)
        return;
    m_pendingId = id;
    m_pendingLocation = location;
    emit pendingMoveLocationChanged();
    setStatus(tr("Moving document…"));
    run({"--json", "reader-move-documents", "--document-ids", id, "--location", location}, Operation::Move);
}

void Backend::saveUrl(const QString &value) {
    const QUrl url = QUrl::fromUserInput(value.trimmed());
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty()) {
        fail(tr("Enter a valid HTTP or HTTPS URL."));
        return;
    }
    m_pendingUrl = url.toString(QUrl::FullyEncoded);
    setStatus(tr("Saving URL to Reader…"));
    run({"--json", "reader-create-document", "--url", m_pendingUrl}, Operation::SaveUrl);
}

void Backend::setDocumentSeen(const QString &id, bool seen) {
    if (id.isEmpty())
        return;
    QJsonObject document{{"document_id", id}, {"seen", seen}};
    QJsonArray documents{document};
    m_pendingId = id;
    m_pendingSeen = seen;
    setStatus(seen ? tr("Marking document seen…") : tr("Marking document unseen…"));
    run({"--json", "reader-bulk-edit-document-metadata", "--documents",
         QString::fromUtf8(QJsonDocument(documents).toJson(QJsonDocument::Compact))}, Operation::SetSeen);
}

void Backend::saveReadingProgress(const QString &id, int progress) {
    if (id.isEmpty())
        return;
    progress = qBound(0, progress, 100);
    setStatus(tr("Saving reading progress…"));
    queueProgressWrite(id, progress);
}

void Backend::saveDocumentNote(const QString &id, const QString &note) {
    if (id.isEmpty())
        return;
    QJsonObject document;
    document.insert("document_id", id);
    document.insert("notes", note);
    QJsonArray documents;
    documents.append(document);
    const QString payload = QString::fromUtf8(QJsonDocument(documents).toJson(QJsonDocument::Compact));
    if (id == m_selectedId)
        m_selectedNote = note;
    for (QVariantList *rows : {&m_documents, &m_libraryDocuments}) {
        for (QVariant &rowValue : *rows) {
            QVariantMap row = rowValue.toMap();
            if (row.value(QStringLiteral("id")).toString() == id) {
                row.insert(QStringLiteral("notes"), note);
                rowValue = row;
            }
        }
    }
    clearError();
    setStatus(tr("Note saved here · syncing to Readwise…"));
    enqueueNoteWrite(QStringLiteral("document:") + id,
                     {"--json", "reader-bulk-edit-document-metadata", "--documents", payload});
}

void Backend::saveDocumentTags(const QString &id, const QStringList &tags) {
    if (id.isEmpty())
        return;
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString value = tag.trimmed();
        bool duplicate = false;
        for (const QString &existing : cleaned) {
            if (existing.compare(value, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!value.isEmpty() && !duplicate)
            cleaned.append(value);
    }
    QJsonObject document;
    document.insert("document_id", id);
    document.insert("tags", QJsonArray::fromStringList(cleaned));
    QJsonArray documents;
    documents.append(document);
    const QString payload = QString::fromUtf8(QJsonDocument(documents).toJson(QJsonDocument::Compact));
    m_pendingId = id;
    m_pendingTags = cleaned;
    setStatus(tr("Saving tags…"));
    run({"--json", "reader-bulk-edit-document-metadata", "--documents", payload}, Operation::SaveTags);
}

void Backend::loadDocumentTags() {
    if (m_documentTagsLoaded || m_documentTagsInProgress)
        return;
    m_availableDocumentTags.clear();
    m_documentTagsInProgress = true;
    emit availableDocumentTagsChanged();
    m_documentTagsProcess.start(QStringLiteral("readwise"), {"--json", "reader-list-tags"});
}

void Backend::createHighlight(const QString &documentId, const QString &text) {
    if (documentId.isEmpty() || text.trimmed().isEmpty())
        return;
    m_pendingId = documentId;
    setStatus(tr("Creating highlight…"));
    run({"--json", "reader-create-highlight", "--document-id", documentId,
         "--html-content", text}, Operation::CreateHighlight);
}

void Backend::saveHighlightNote(const QString &documentId, const QString &highlightId, const QString &note) {
    if (documentId.isEmpty() || highlightId.isEmpty() || documentId != m_selectedId)
        return;
    for (QVariant &value : m_selectedHighlights) {
        QVariantMap highlight = value.toMap();
        if (highlight.value(QStringLiteral("id")).toString() == highlightId) {
            highlight.insert(QStringLiteral("notes"), note);
            value = highlight;
            break;
        }
    }
    m_highlightsCache.insert(documentId, m_selectedHighlights);
    m_highlightsLoaded.insert(documentId);
    emit selectedHighlightsChanged();
    clearError();
    setStatus(tr("Highlight note saved here · syncing to Readwise…"));
    enqueueNoteWrite(QStringLiteral("highlight:") + documentId + QLatin1Char(':') + highlightId,
                     {"--json", "reader-set-highlight-notes", "--document-id", documentId,
                      "--highlight-document-id", highlightId, "--notes", note});
}

void Backend::addHighlightTags(const QString &documentId, const QString &highlightId, const QStringList &tags) {
    if (documentId.isEmpty() || highlightId.isEmpty() || documentId != m_selectedId)
        return;
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString value = tag.trimmed();
        bool duplicate = false;
        for (const QString &existing : cleaned) {
            if (existing.compare(value, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!value.isEmpty() && !duplicate)
            cleaned.append(value);
    }
    if (cleaned.isEmpty())
        return;
    if (refreshHighlightsBeforeEdit(documentId, highlightId, PendingHighlightEdit::AddTags, cleaned))
        return;
    QJsonArray payload = QJsonArray::fromStringList(cleaned);
    m_pendingId = documentId;
    m_pendingHighlightId = highlightId;
    m_pendingHighlightTags = cleaned;
    setStatus(tr("Adding highlight tags…"));
    run({"--json", "reader-add-tags-to-highlight", "--document-id", documentId,
         "--highlight-document-id", highlightId, "--tag-names",
         QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}, Operation::AddHighlightTags);
}

void Backend::removeHighlightTags(const QString &documentId, const QString &highlightId, const QStringList &tags) {
    if (documentId.isEmpty() || highlightId.isEmpty() || documentId != m_selectedId || tags.isEmpty())
        return;
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString value = tag.trimmed();
        if (!value.isEmpty() && !cleaned.contains(value, Qt::CaseInsensitive))
            cleaned.append(value);
    }
    if (cleaned.isEmpty())
        return;
    if (refreshHighlightsBeforeEdit(documentId, highlightId, PendingHighlightEdit::RemoveTags, cleaned))
        return;
    const QJsonArray payload = QJsonArray::fromStringList(cleaned);
    m_pendingId = documentId;
    m_pendingHighlightId = highlightId;
    m_pendingHighlightTags = cleaned;
    setStatus(tr("Removing highlight tags…"));
    run({"--json", "reader-remove-tags-from-highlight", "--document-id", documentId,
         "--highlight-document-id", highlightId, "--tag-names",
         QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}, Operation::RemoveHighlightTags);
}

bool Backend::refreshHighlightsBeforeEdit(const QString &documentId, const QString &highlightId,
                                          PendingHighlightEdit edit, const QStringList &tags) {
    if (m_dispatchingFreshHighlightEdit) {
        m_dispatchingFreshHighlightEdit = false;
        return false;
    }
    m_pendingHighlightEdit = edit;
    m_pendingHighlightId = highlightId;
    m_pendingHighlightTags = tags;
    m_highlightsLoaded.remove(documentId);
    m_highlightsCache.remove(documentId);
    loadHighlights();
    return true;
}

void Backend::loadHighlights() {
    if (m_selectedId.isEmpty())
        return;
    const QString requestedId = m_selectedId;
    if (m_highlightsLoaded.contains(requestedId)) {
        m_selectedHighlights = m_highlightsCache.value(requestedId);
        emit selectedHighlightsChanged();
        if (m_operation != Operation::Highlights)
            setMetadataBusy(false);
        if (m_queuedHighlightId == requestedId)
            m_queuedHighlightId.clear();
        return;
    }
    if (m_process.state() != QProcess::NotRunning) {
        if (m_operation == Operation::Highlights && m_pendingId == requestedId) {
            m_queuedHighlightId.clear();
            setMetadataBusy(true);
            return;
        }
        m_queuedHighlightId = requestedId;
        setMetadataBusy(true);
        return;
    }
    m_pendingId = requestedId;
    setMetadataBusy(true);
    setStatus(tr("Loading highlights…"));
    run({"--json", "reader-get-document-highlights", "--document-id", requestedId}, Operation::Highlights);
}

void Backend::processQueuedWork() {
    if (m_process.state() != QProcess::NotRunning || m_apiReply || m_busy)
        return;
    if (m_hasQueuedOpen) {
        const QString id = m_queuedOpenId;
        const QString title = m_queuedOpenTitle;
        const QString author = m_queuedOpenAuthor;
        const QString url = m_queuedOpenUrl;
        const QStringList tags = m_queuedOpenTags;
        const QString note = m_queuedOpenNote;
        m_hasQueuedOpen = false;
        m_queuedHighlightId.clear();
        openDocument(id, title, author, url, tags, note);
        return;
    }
    if (m_pendingHighlightEdit != PendingHighlightEdit::None
        && m_dispatchingFreshHighlightEdit && m_selectedId == m_pendingId) {
        const PendingHighlightEdit edit = m_pendingHighlightEdit;
        const QString documentId = m_selectedId;
        const QString highlightId = m_pendingHighlightId;
        const QStringList tags = m_pendingHighlightTags;
        m_pendingHighlightEdit = PendingHighlightEdit::None;
        if (edit == PendingHighlightEdit::AddTags)
            addHighlightTags(documentId, highlightId, tags);
        else if (edit == PendingHighlightEdit::RemoveTags)
            removeHighlightTags(documentId, highlightId, tags);
        return;
    }
    if (!m_queuedHighlightId.isEmpty()) {
        const QString queuedId = m_queuedHighlightId;
        m_queuedHighlightId.clear();
        if (queuedId == m_selectedId)
            loadHighlights();
        else
            setMetadataBusy(false);
    }
}

void Backend::login() {
    setStatus(tr("Waiting for Readwise login…"));
    run({"login"}, Operation::Login);
}

bool Backend::openExternalUrl(const QString &value) {
    const QUrl url(value, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != "http" && scheme != "https" && scheme != "mailto"))
        return false;
    if ((scheme == "http" || scheme == "https") && url.host().isEmpty())
        return false;
    return QDesktopServices::openUrl(url);
}

QVariantList Backend::externalEditorOptions() const {
    QVariantList result;
    QString terminal;
    for (const QString &candidate : {QStringLiteral("foot"), QStringLiteral("kitty"),
                                     QStringLiteral("alacritty"), QStringLiteral("ghostty")}) {
        terminal = QStandardPaths::findExecutable(candidate);
        if (!terminal.isEmpty())
            break;
    }

    const QList<QPair<QString, QString>> definitions{
        {"neovim", "Neovim"}, {"vscode", "Visual Studio Code"}, {"obsidian", "Obsidian"},
        {"helix", "Helix"}, {"emacs", "Emacs"}, {"zed", "Zed"},
        {"sublime", "Sublime Text"}, {"kate", "Kate"}, {"mousepad", "Mousepad"}};
    for (const auto &definition : definitions) {
        const QString &id = definition.first;
        QString command;
        if (id == "neovim") command = "nvim";
        else if (id == "vscode") command = "code";
        else if (id == "obsidian") command = "obsidian";
        else if (id == "helix") command = "hx";
        else if (id == "emacs") command = "emacs";
        else if (id == "zed") command = "zed";
        else if (id == "sublime") command = "subl";
        else if (id == "kate") command = "kate";
        else if (id == "mousepad") command = "mousepad";
        const bool installed = !QStandardPaths::findExecutable(command).isEmpty()
            && (!((id == "neovim" || id == "helix") && terminal.isEmpty()));
        result.append(QVariantMap{{"id", id}, {"label", definition.second}, {"installed", installed}});
    }
    return result;
}

QVariantList Backend::availableExternalEditors(const QStringList &configured) const {
    QVariantList result;
    for (const QVariant &option : externalEditorOptions()) {
        const QVariantMap editor = option.toMap();
        if (editor.value(QStringLiteral("installed")).toBool()
            && configured.contains(editor.value(QStringLiteral("id")).toString()))
            result.append(editor);
    }
    return result;
}

QString Backend::writeEditorMarkdownFile(const QString &markdown, const QVariantMap &metadata) const {
    const QString documentsDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString directory = documentsDirectory.isEmpty()
        ? QString() : QDir(documentsDirectory).filePath(QStringLiteral("Omawise Reader"));
    if (!directory.isEmpty() && !QDir().mkpath(directory))
        directory.clear();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/editor-handoff");
        if (!QDir().mkpath(directory))
            return {};
    }

    QString title = metadata.value(QStringLiteral("title")).toString().trimmed();
    title.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    title = title.left(80);
    while (title.startsWith(QLatin1Char('-'))) title.remove(0, 1);
    while (title.endsWith(QLatin1Char('-'))) title.chop(1);
    if (title.isEmpty())
        title = QStringLiteral("article");
    QString id = metadata.value(QStringLiteral("id")).toString();
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QString());
    id = id.left(16);
    const QString stem = id.isEmpty() ? title : title + QLatin1Char('-') + id;

    for (int suffix = 1; suffix < 1000; ++suffix) {
        const QString filename = stem + (suffix == 1 ? QString() : QStringLiteral("-%1").arg(suffix))
            + QStringLiteral(".md");
        const QString path = QDir(directory).filePath(filename);
        if (QFileInfo::exists(path))
            continue;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return {};
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QByteArray bytes = markdown.toUtf8();
        if (file.write(bytes) != bytes.size() || !file.commit())
            return {};
        return path;
    }
    return {};
}

bool Backend::openArticleInEditor(const QString &editorId, const QString &content,
                                  const QVariantMap &metadata) {
    const QString markdown = articleMarkdown(content, metadata);
    if (markdown.isEmpty() || !QGuiApplication::instance())
        return false;
    QGuiApplication::clipboard()->setText(markdown);

    if (editorId == QStringLiteral("obsidian")) {
        QUrl uri(QStringLiteral("obsidian://new"));
        QUrlQuery query;
        QString title = metadata.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty())
            title = tr("Reader article");
        query.addQueryItem(QStringLiteral("name"), title);
        query.addQueryItem(QStringLiteral("clipboard"), QStringLiteral("true"));
        uri.setQuery(query);
        if (!QDesktopServices::openUrl(uri)) {
            setStatus(tr("Could not open Obsidian"));
            fail(tr("Obsidian did not accept the new-note request."));
            return false;
        }
        setStatus(tr("Article opened in Obsidian"));
        return true;
    }

    const QVariantList available = availableExternalEditors(
        {"neovim", "vscode", "helix", "emacs", "zed", "sublime", "kate", "mousepad"});
    QString label;
    for (const QVariant &value : available) {
        const QVariantMap editor = value.toMap();
        if (editor.value(QStringLiteral("id")).toString() == editorId) {
            label = editor.value(QStringLiteral("label")).toString();
            break;
        }
    }
    if (label.isEmpty()) {
        setStatus(tr("Editor is not available"));
        fail(tr("That editor is not installed or configured."));
        return false;
    }

    const QString path = writeEditorMarkdownFile(markdown, metadata);
    if (path.isEmpty()) {
        setStatus(tr("Could not prepare article Markdown"));
        fail(tr("Could not write the article export under Documents/Omawise Reader."));
        return false;
    }

    QString executable;
    QStringList arguments;
    if (editorId == QStringLiteral("neovim") || editorId == QStringLiteral("helix")) {
        QString terminal;
        for (const QString &candidate : {QStringLiteral("foot"), QStringLiteral("kitty"),
                                         QStringLiteral("alacritty"), QStringLiteral("ghostty")}) {
            terminal = QStandardPaths::findExecutable(candidate);
            if (!terminal.isEmpty())
                break;
        }
        executable = terminal;
        arguments = {QStringLiteral("-e"), editorId == QStringLiteral("neovim")
                         ? QStringLiteral("nvim") : QStringLiteral("hx"), path};
    } else {
        QString command = editorId;
        if (editorId == QStringLiteral("vscode")) command = QStringLiteral("code");
        else if (editorId == QStringLiteral("emacs")) command = QStringLiteral("emacs");
        else if (editorId == QStringLiteral("sublime")) command = QStringLiteral("subl");
        else if (editorId == QStringLiteral("mousepad")) command = QStringLiteral("mousepad");
        executable = QStandardPaths::findExecutable(command);
        if (editorId == QStringLiteral("vscode"))
            arguments.append(QStringLiteral("--reuse-window"));
        arguments.append(path);
    }
    if (!QProcess::startDetached(executable, arguments)) {
        setStatus(tr("Could not open %1").arg(label));
        fail(tr("The editor process could not be started."));
        return false;
    }
    setStatus(tr("Article opened in %1").arg(label));
    return true;
}

QString Backend::formattedMarkdown(const QString &markdown, const QString &accent,
                                  const QVariantMap &readerStyle,
                                  const QVariantMap &themeColors,
                                  const QVariantList &highlights) const {
    const QString fontFamily = readerStyle.value("fontFamily", "monospace").toString();
    const int fontSizePx = qBound(12, readerStyle.value("fontSizePx", 12).toInt(), 48);
    const double lineSpacing = qBound(1.0, readerStyle.value("lineSpacing", 1.2).toDouble(), 2.2);
    const int paragraphSpacing = qBound(0, readerStyle.value("paragraphSpacingPt", 18).toInt(), 36);
    QTextDocument document;
    QFont documentFont("monospace");
    documentFont.setPixelSize(fontSizePx);
    documentFont.setFamily(fontFamily);
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        documentFont = QGuiApplication::font();
        documentFont.setFamily(fontFamily);
        documentFont.setPixelSize(fontSizePx);
        if (documentFont.pixelSize() <= 0 && documentFont.pointSizeF() <= 0)
            documentFont.setPointSize(12);
    }
    document.setDefaultFont(documentFont);
    const QColor accentColor(accent);
    const QString linkColor = accentColor.isValid() ? accentColor.name(QColor::HexRgb) : QStringLiteral("#FFD60A");
    const auto color = [&themeColors](const QString &key, const QString &fallback) {
        const QColor value(themeColors.value(key).toString());
        return value.isValid() ? value.name(QColor::HexRgb) : fallback;
    };
    const QString textColor = color("text", "#d9dbd5");
    const QString brightTextColor = color("textBright", "#f2f3ee");
    const QString secondaryTextColor = color("textSecondary", "#b6bbae");
    const QString panelColor = color("panel", "#22262a");
    const QString surfaceAltColor = color("surfaceAlt", "#292e2c");
    const QString borderColor = color("border", "#343940");
    document.setDefaultStyleSheet(QStringLiteral(R"CSS(
        body { color: %5; font-family: "%2"; font-size: %3px; margin: 0; padding: 0; }
        p { margin-top: 0; margin-bottom: %4pt; }
        h1, h2, h3, h4, h5, h6 { color: %6; }
        h1 { font-size: 23pt; font-weight: 700; margin-top: 8pt; margin-bottom: 18pt; }
        h2 { font-size: 18pt; font-weight: 650; margin-top: 25pt; margin-bottom: 11pt; }
        h3 { font-size: 14pt; font-weight: 650; margin-top: 20pt; margin-bottom: 9pt; }
        h4, h5, h6 { font-size: 12pt; font-weight: 650; margin-top: 16pt; margin-bottom: 7pt; }
        a { color: %1; text-decoration: none; }
        blockquote { color: %7; background-color: %8; border-left: 3px solid %1; margin: 12pt 0; padding: 8pt 13pt; }
        pre { color: %6; background-color: %8; border: 1px solid %10; margin: 10pt 0; padding: 11pt; }
        code { color: %6; background-color: %9; }
        li { margin-bottom: 5pt; }
        ul, ol { margin-top: 4pt; margin-bottom: 12pt; }
        table { border-collapse: collapse; margin: 12pt 0; }
        th { color: %6; background-color: %8; font-weight: 650; }
        td, th { border: 1px solid %10; padding: 6pt 8pt; }
        hr { color: %10; }
        img { max-width: 100%; }
    )CSS").arg(linkColor, fontFamily).arg(fontSizePx).arg(paragraphSpacing)
                      .arg(textColor, brightTextColor, secondaryTextColor, panelColor, surfaceAltColor, borderColor));

    if (isHtmlContent(markdown)) {
        document.setHtml(markdown);
    } else {
        document.setMarkdown(markdown);
    }
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        format.setLineHeight(lineSpacing * 100.0, QTextBlockFormat::ProportionalHeight);
        if (format.headingLevel() == 0 && format.indent() == 0 && !block.textList()
            && !format.hasProperty(QTextFormat::BlockCodeLanguage)) {
            const qreal paragraphSpacingPx = paragraphSpacing * (96.0 / 72.0);
            format.setBottomMargin(paragraphSpacingPx);
        }
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
    }
    // Reader marks saved passages with a subdued olive fill and a fine underline.
    // Match against QTextDocument's plain text so highlights work for Markdown and HTML.
    const QString plainText = document.toPlainText();
    const auto normalizeWhitespace = [](const QString &text, QVector<qsizetype> *positions) {
        QString normalized;
        bool inWhitespace = false;
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text.at(i).isSpace()) {
                if (!inWhitespace) {
                    normalized.append(QLatin1Char(' '));
                    if (positions) positions->append(i);
                }
                inWhitespace = true;
            } else {
                normalized.append(text.at(i));
                if (positions) positions->append(i);
                inWhitespace = false;
            }
        }
        qsizetype first = 0;
        while (first < normalized.size() && normalized.at(first).isSpace())
            ++first;
        qsizetype last = normalized.size();
        while (last > first && normalized.at(last - 1).isSpace())
            --last;
        if (positions) {
            if (first > 0)
                positions->remove(0, first);
            if (positions->size() > last - first)
                positions->resize(last - first);
        }
        return normalized.mid(first, last - first);
    };
    QVector<qsizetype> articlePositions;
    const QString normalizedArticle = normalizeWhitespace(plainText, &articlePositions);
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(QStringLiteral("#4b481f")));
    highlightFormat.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    highlightFormat.setUnderlineColor(QColor(QStringLiteral("#c6bd32")));
    for (const QVariant &value : highlights) {
        const QString passage = value.toMap().value(QStringLiteral("text")).toString().trimmed();
        if (passage.isEmpty())
            continue;
        const QString normalizedPassage = normalizeWhitespace(passage, nullptr);
        if (normalizedPassage.isEmpty())
            continue;
        qsizetype from = 0;
        while ((from = normalizedArticle.indexOf(normalizedPassage, from, Qt::CaseInsensitive)) >= 0) {
            const qsizetype last = from + normalizedPassage.size() - 1;
            if (last < articlePositions.size()) {
                QTextCursor cursor(&document);
                cursor.setPosition(articlePositions.at(from));
                cursor.setPosition(articlePositions.at(last) + 1, QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(highlightFormat);
            }
            from += normalizedPassage.size();
        }
    }
    const QString html = document.toHtml();
    const QRegularExpression anchorExpression(QStringLiteral("<a\\s+href=\"([^\"]*)\">(.*?)</a>"),
                                              QRegularExpression::CaseInsensitiveOption
                                                  | QRegularExpression::DotMatchesEverythingOption);
    QString formattedHtml;
    qsizetype offset = 0;
    auto matches = anchorExpression.globalMatch(html);
    while (matches.hasNext()) {
        const auto match = matches.next();
        formattedHtml += html.mid(offset, match.capturedStart() - offset);
        formattedHtml += QStringLiteral("<a href=\"%1\"><font color=\"%2\">%3</font></a>")
                             .arg(match.captured(1), linkColor, match.captured(2));
        offset = match.capturedEnd();
    }
    formattedHtml += html.mid(offset);
    return formattedHtml;
}

QString Backend::articleMarkdown(const QString &content, const QVariantMap &metadata) const {
    if (content.isEmpty())
        return {};

    QString markdown = content;
    if (isHtmlContent(content))
        markdown = htmlToMarkdown(content);

    QString frontmatter = QStringLiteral("---\n");
    const QString sourceKey = metadata.value(QStringLiteral("site_name")).toString().trimmed().isEmpty()
        ? QStringLiteral("source") : QStringLiteral("site_name");
    const QList<QPair<QString, QString>> fields{
        {QStringLiteral("id"), QStringLiteral("id")},
        {QStringLiteral("title"), QStringLiteral("title")},
        {QStringLiteral("author"), QStringLiteral("author")},
        {QStringLiteral("url"), QStringLiteral("url")},
        {QStringLiteral("source"), sourceKey},
        {QStringLiteral("category"), QStringLiteral("category")},
        {QStringLiteral("location"), QStringLiteral("location")},
        {QStringLiteral("published_date"), QStringLiteral("published_date")},
        {QStringLiteral("saved_at"), QStringLiteral("saved_at")},
        {QStringLiteral("reading_time"), QStringLiteral("reading_time")},
        {QStringLiteral("summary"), QStringLiteral("summary")}
    };
    for (const auto &field : fields) {
        const QString value = metadata.value(field.second).toString().trimmed();
        if (!value.isEmpty())
            frontmatter += field.first + QStringLiteral(": ") + yamlQuotedString(value) + QLatin1Char('\n');
    }

    QStringList tags;
    const QVariant tagValue = metadata.value(QStringLiteral("tags"));
    if (tagValue.canConvert<QStringList>())
        tags = tagValue.toStringList();
    if (tags.isEmpty()) {
        for (const QVariant &value : tagValue.toList()) {
            const QString tag = value.typeId() == QMetaType::QVariantMap
                ? value.toMap().value(QStringLiteral("name")).toString()
                : value.toString();
            if (!tag.trimmed().isEmpty())
                tags.append(tag.trimmed());
        }
    }
    if (!tags.isEmpty()) {
        frontmatter += QStringLiteral("tags:\n");
        for (const QString &tag : tags)
            frontmatter += QStringLiteral("  - ") + yamlQuotedString(tag) + QLatin1Char('\n');
    }
    frontmatter += QStringLiteral("---\n\n");

    if (!markdown.endsWith(QLatin1Char('\n')))
        markdown += QLatin1Char('\n');
    return frontmatter + markdown;
}

bool Backend::copyArticleMarkdown(const QString &content, const QVariantMap &metadata) {
    if (!QGuiApplication::instance())
        return false;
    const QString markdown = articleMarkdown(content, metadata);
    if (markdown.isEmpty())
        return false;
    QGuiApplication::clipboard()->setText(markdown);
    setStatus(tr("Article Markdown copied to clipboard"));
    return true;
}

bool Backend::copyDocumentUrl(const QString &value) {
    const QUrl url(value, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!QGuiApplication::instance() || !url.isValid()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        || url.host().isEmpty())
        return false;
    QGuiApplication::clipboard()->setText(url.toString(QUrl::FullyEncoded));
    setStatus(tr("Original document URL copied to clipboard"));
    return true;
}

QVariantList Backend::selectedHeadings() const {
    QVariantList headings;
    QTextDocument document;
    if (isHtmlContent(m_selectedContent)) {
        document.setHtml(m_selectedContent);
    } else {
        document.setMarkdown(m_selectedContent);
    }

    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        const QString text = block.text().trimmed();
        if (level < 1 || text.isEmpty())
            continue;
        headings.append(QVariantMap{{"text", text}, {"level", level}, {"position", block.position()}});
    }
    return headings;
}

QVariantList Backend::selectedParagraphs() const {
    QVariantList paragraphs;
    QTextDocument document;
    if (isHtmlContent(m_selectedContent)) {
        document.setHtml(m_selectedContent);
    } else {
        document.setMarkdown(m_selectedContent);
    }

    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        if (text.trimmed().isEmpty())
            continue;
        paragraphs.append(QVariantMap{{"text", text},
                                      {"position", block.position()},
                                      {"length", text.length()}});
    }
    return paragraphs;
}

QVariantList Backend::resultRows(const QJsonDocument &document) {
    QJsonArray rows;
    if (document.isArray()) {
        rows = document.array();
    } else if (document.isObject()) {
        const QJsonObject root = document.object();
        for (const QString key : {"results", "documents", "items", "data"}) {
            if (root.value(key).isArray()) {
                rows = root.value(key).toArray();
                break;
            }
        }
        if (rows.isEmpty() && root.contains("id"))
            rows.append(root);
    }

    QVariantList result;
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        QVariantMap row;
        QString documentId = object.value("id").toString();
        if (documentId.isEmpty())
            documentId = object.value("document_id").toString();
        if (documentId.isEmpty())
            documentId = object.value("documentId").toString();
        if (!documentId.isEmpty())
            row.insert("id", documentId);
        for (const QString key : {"id", "title", "author", "source", "site_name", "category", "location", "summary", "reading_time", "listening_time", "word_count", "url", "source_url", "first_opened_at", "published_date", "saved_at", "reading_progress", "seen", "image_url", "notes", "tags"}) {
            if (key == "tags") {
                if (!object.value(key).isUndefined() && !object.value(key).isNull())
                    row.insert(key, tagNames(object.value(key)));
            } else if (key == "reading_progress") {
                if (!object.value(key).isUndefined() && !object.value(key).isNull())
                    row.insert(key, readingProgressPercent(object.value(key)));
            } else if (!object.value(key).isUndefined() && !object.value(key).isNull()) {
                row.insert(key, object.value(key).toVariant());
            }
        }
        for (const QString key : {"id", "title", "author", "source", "site_name", "category", "location", "summary", "reading_time", "listening_time", "first_opened_at"}) {
            if (!row.contains(key))
                row.insert(key, QString());
        }
        if (!row.contains("reading_progress"))
            row.insert("reading_progress", 0);
        if (!row.contains("seen"))
            row.insert("seen", !row.value("first_opened_at").toString().isEmpty());
        if (!row.value("id").toString().isEmpty())
            result.append(row);
    }
    return result;
}

QVariantList Backend::highlightRows(const QJsonDocument &document) {
    QJsonArray rows;
    if (document.isArray()) {
        rows = document.array();
    } else if (document.isObject()) {
        QJsonObject root = document.object();
        for (const QString key : {"data", "result"}) {
            if (root.value(key).isObject()) {
                root = root.value(key).toObject();
                break;
            }
        }
        for (const QString key : {"highlights", "document_highlights", "results", "documents", "items"}) {
            if (root.value(key).isArray()) {
                rows = root.value(key).toArray();
                break;
            }
        }
        if (rows.isEmpty() && root.value("highlight").isObject())
            rows.append(root.value("highlight"));
        else if (rows.isEmpty() && (root.contains("id") || root.contains("highlight_id")))
            rows.append(root);
    }

    QVariantList result;
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        QString text;
        for (const QString key : {"text", "content", "highlight", "highlight_text", "highlighted_text", "html_content"}) {
            if (object.value(key).isString()) {
                text = object.value(key).toString();
                break;
            }
        }
        QStringList tags;
        for (const QJsonValue &tag : object.value("tags").toArray()) {
            if (tag.isString()) tags.append(tag.toString());
            else if (tag.isObject()) tags.append(tag.toObject().value("name").toString());
        }
        result.append(QVariantMap{
            {"id", object.value("id").isUndefined() ? object.value("highlight_id").toVariant() : object.value("id").toVariant()},
            {"text", text},
            {"notes", object.value("notes").isUndefined() ? object.value("note").toString() : object.value("notes").toString()},
            {"tags", tags}
        });
    }
    return result;
}

QJsonObject Backend::unwrapObject(const QJsonDocument &document) {
    if (!document.isObject())
        return {};
    QJsonObject object = document.object();
    for (const QString key : {"document", "result", "data"}) {
        if (object.value(key).isObject()) {
            object = object.value(key).toObject();
            break;
        }
    }
    return object;
}

void Backend::finish(int exitCode, QProcess::ExitStatus exitStatus) {
    QTimer::singleShot(0, this, &Backend::processQueuedWork);
    m_timeout.stop();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    const QByteArray standardOutput = m_process.readAllStandardOutput();
    const QString standardError = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    setBusy(false);

    if (m_timedOut) {
        m_timedOut = false;
        if (operation == Operation::Highlights) {
            setMetadataBusy(false);
            m_pendingHighlightEdit = PendingHighlightEdit::None;
            m_dispatchingFreshHighlightEdit = false;
        }
        setStatus(tr("Readwise request timed out"));
        fail(tr("The Readwise CLI did not respond before the timeout."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (operation == Operation::Move) {
            m_pendingId.clear();
            clearPendingMove();
        }
        if (operation == Operation::Highlights) {
            setMetadataBusy(false);
            m_pendingHighlightEdit = PendingHighlightEdit::None;
            m_dispatchingFreshHighlightEdit = false;
        }
        setStatus(tr("Readwise request failed"));
        if (operation == Operation::Search && !m_localSearchMatches.isEmpty()) {
            m_documents = m_localSearchMatches;
            emit documentsChanged();
        }
        fail(standardError.isEmpty() ? tr("Readwise CLI exited with code %1.").arg(exitCode) : standardError);
        if (m_authenticationRequired)
            setCliSignedIn(false);
        return;
    }

    setCliSignedIn(true);

    if (operation == Operation::Login) {
        clearPersistentReaderCache();
        setStatus(tr("Login command finished · refreshing library"));
        refresh();
        return;
    }

    QJsonParseError parseError;
    const QJsonValue jsonValue = QJsonValue::fromJson(standardOutput, &parseError);
    const QJsonDocument json = jsonValue.isObject()
        ? QJsonDocument(jsonValue.toObject())
        : jsonValue.isArray() ? QJsonDocument(jsonValue.toArray()) : QJsonDocument();
    if (operation == Operation::Detail && parseError.error != QJsonParseError::NoError) {
        // The CLI documents document details as Markdown; accept plain Markdown
        // output as well as JSON so the reader is resilient to CLI output modes.
        const QString detailId = m_pendingId;
        const QString content = QString::fromUtf8(standardOutput);
        m_contentCache.insert(detailId, content);
        writePersistentArticle(detailId, content);
        if (m_selectedId != detailId)
            return;
        if (m_selectedTitle.isEmpty())
            m_selectedTitle = tr("Reader document");
        m_selectedContent = content;
        emit selectedDocumentChanged();
        setStatus(tr("Document opened"));
        loadHighlights();
        return;
    }
    if (operation == Operation::Detail && jsonValue.isString()) {
        const QString detailId = m_pendingId;
        const QString content = jsonValue.toString();
        m_contentCache.insert(detailId, content);
        writePersistentArticle(detailId, content);
        if (m_selectedId != detailId)
            return;
        if (m_selectedTitle.isEmpty())
            m_selectedTitle = tr("Reader document");
        m_selectedContent = content;
        emit selectedDocumentChanged();
        setStatus(tr("Document opened"));
        loadHighlights();
        return;
    }
    if (parseError.error != QJsonParseError::NoError) {
        if (operation == Operation::Move) {
            m_pendingId.clear();
            clearPendingMove();
        }
        if (operation == Operation::Highlights) {
            setMetadataBusy(false);
            m_pendingHighlightEdit = PendingHighlightEdit::None;
            m_dispatchingFreshHighlightEdit = false;
        }
        setStatus(tr("Unexpected CLI response"));
        fail(tr("The Readwise CLI returned unrecognized data."));
        return;
    }

    if (json.isObject()) {
        const QJsonObject root = json.object();
        const QString error = root.value("error").toString();
        if (root.value("isError").toBool() || !error.isEmpty()) {
            if (operation == Operation::Move) {
                m_pendingId.clear();
                clearPendingMove();
            }
            if (operation == Operation::Highlights) {
                setMetadataBusy(false);
                m_pendingHighlightEdit = PendingHighlightEdit::None;
                m_dispatchingFreshHighlightEdit = false;
            }
            setStatus(tr("Readwise request failed"));
            fail(error.isEmpty() ? tr("The Readwise CLI returned an error.") : error);
            if (m_authenticationRequired)
                setCliSignedIn(false);
            return;
        }
        if ((operation == Operation::SaveTags
             || operation == Operation::CreateHighlight
             || operation == Operation::AddHighlightTags || operation == Operation::RemoveHighlightTags
             || operation == Operation::SetSeen
             || operation == Operation::SaveUrl)
            && root.value("success").isBool()
            && !root.value("success").toBool()) {
            setStatus(tr("Readwise update failed"));
            fail(root.value("message").toString().isEmpty()
                     ? tr("Readwise could not apply the requested update.")
                     : root.value("message").toString());
            return;
        }
        if ((operation == Operation::SaveTags
             || operation == Operation::CreateHighlight
             || operation == Operation::AddHighlightTags || operation == Operation::RemoveHighlightTags
             || operation == Operation::SetSeen
             || operation == Operation::SaveUrl)
            && root.value("results").isArray()) {
            for (const QJsonValue &value : root.value("results").toArray()) {
                const QJsonObject result = value.toObject();
                if (!result.value("success").isBool() || result.value("success").toBool())
                    continue;
                m_pendingTags.clear();
                setStatus(tr("Readwise update failed"));
                fail(result.value("error").toString().isEmpty()
                         ? tr("Readwise could not apply the requested document update.")
                         : result.value("error").toString());
                return;
            }
        }
    }

    switch (operation) {
    case Operation::List: {
        m_libraryDocuments = resultRows(json);
        m_documents = m_libraryDocuments;
        const QString previousCursor = m_nextCursor;
        m_nextCursor.clear();
        if (json.isObject()) {
            const QJsonObject root = json.object();
            for (const QString key : {"next_cursor", "nextCursor", "next_page_cursor", "nextPageCursor", "cursor"}) {
                if (root.value(key).isString()) {
                    m_nextCursor = root.value(key).toString();
                    break;
                }
            }
        }
        if (previousCursor != m_nextCursor)
            emit hasMoreDocumentsChanged();
        writePersistentLibrary(m_location);
        emit documentsChanged();
        setStatus(tr("%1 documents").arg(m_documents.size()));
        if (m_documents.isEmpty())
            fail(tr("No documents in this view."));
        break;
    }
    case Operation::ListMore: {
        const QVariantList page = resultRows(json);
        QSet<QString> seen;
        for (const QVariant &row : m_libraryDocuments)
            seen.insert(row.toMap().value("id").toString());
        for (const QVariant &row : page) {
            const QString id = row.toMap().value("id").toString();
            if (!id.isEmpty() && !seen.contains(id)) {
                m_libraryDocuments.append(row);
                m_documents.append(row);
                seen.insert(id);
            }
        }
        const QString previousCursor = m_nextCursor;
        m_nextCursor.clear();
        if (json.isObject()) {
            const QJsonObject root = json.object();
            for (const QString key : {"next_cursor", "nextCursor", "next_page_cursor", "nextPageCursor", "cursor"}) {
                if (root.value(key).isString()) {
                    m_nextCursor = root.value(key).toString();
                    break;
                }
            }
        }
        if (previousCursor != m_nextCursor)
            emit hasMoreDocumentsChanged();
        emit documentsChanged();
        setStatus(tr("%1 documents").arg(m_documents.size()));
        break;
    }
    case Operation::Search: {
        const QVariantList remoteMatches = resultRows(json);
        QVariantList merged = m_localSearchMatches;
        QSet<QString> seen;
        for (const QVariant &row : merged)
            seen.insert(row.toMap().value("id").toString());
        for (const QVariant &row : remoteMatches) {
            const QString id = row.toMap().value("id").toString();
            if (!id.isEmpty() && !seen.contains(id)) {
                merged.append(row);
                seen.insert(id);
            }
        }
        m_documents = merged;
        emit documentsChanged();
        setStatus(tr("Searching titles…"));
        // The hybrid query can miss title matches. The CLI's dedicated
        // title filter covers the full library independently of the loaded page.
        run({"--json", "reader-search-documents", "--query", m_activeQuery,
             "--title-search", m_activeQuery, "--limit", "50"}, Operation::TitleSearch);
        break;
    }
    case Operation::TitleSearch: {
        const QVariantList titleMatches = resultRows(json);
        QVariantList merged = m_documents;
        QSet<QString> seen;
        for (const QVariant &row : merged)
            seen.insert(row.toMap().value("id").toString());
        for (const QVariant &row : titleMatches) {
            const QString id = row.toMap().value("id").toString();
            if (!id.isEmpty() && !seen.contains(id)) {
                merged.append(row);
                seen.insert(id);
            }
        }
        m_documents = merged;
        emit documentsChanged();
        setStatus(tr("%1 results").arg(m_documents.size()));
        if (m_documents.isEmpty())
            fail(tr("No matching documents."));
        break;
    }
    case Operation::Detail: {
        const QString detailId = m_pendingId;
        if (m_selectedId != detailId)
            break;
        const QJsonObject object = unwrapObject(json);
        const QString title = object.value("title").toString();
        const QString author = object.value("author").toString();
        if (!object.value("notes").isUndefined() && !object.value("notes").isNull())
            m_selectedNote = object.value("notes").toString();
        if (object.value("tags").isArray()) {
            m_selectedTags.clear();
            for (const QJsonValue &tag : object.value("tags").toArray()) {
                if (tag.isString()) m_selectedTags.append(tag.toString());
                else if (tag.isObject()) m_selectedTags.append(tag.toObject().value("name").toString());
            }
        }
        if (!title.isEmpty()) m_selectedTitle = title;
        if (!author.isEmpty()) m_selectedAuthor = author;
        QString content;
        for (const QString key : {"content", "markdown", "markdown_content", "document_content", "text"}) {
            if (object.value(key).isString() && !object.value(key).toString().isEmpty()) {
                content = object.value(key).toString();
                break;
            }
        }
        const QString url = object.value("url").toString();
        if (!url.isEmpty()) m_selectedUrl = url;
        if (content.isEmpty())
            content = tr("The CLI returned document metadata without a Markdown body.");
        m_contentCache.insert(detailId, content);
        if (!content.isEmpty() && !content.startsWith(tr("The CLI returned document metadata without a Markdown body.")))
            writePersistentArticle(detailId, content);
        m_selectedContent = content;
        emit selectedDocumentChanged();
        setStatus(tr("Document opened"));
        loadHighlights();
        break;
    }
    case Operation::Highlights:
        {
        const QString highlightsId = m_pendingId;
        const QVariantList fetched = highlightRows(json);
        m_highlightsCache.insert(highlightsId, fetched);
        m_highlightsLoaded.insert(highlightsId);
        m_dispatchingFreshHighlightEdit = m_pendingHighlightEdit != PendingHighlightEdit::None
            && m_pendingId == highlightsId;
        if (m_selectedId == highlightsId) {
            m_selectedHighlights = fetched;
            emit selectedHighlightsChanged();
        }
        if (m_queuedHighlightId.isEmpty())
            setMetadataBusy(false);
        setStatus(tr("%1 highlights").arg(fetched.size()));
        break;
        }
    case Operation::Archive:
        QFile::remove(persistentLibraryCachePath(m_location));
        setStatus(tr("Document archived"));
        refresh();
        break;
    case Operation::Move:
        if (containsFailedMove(json.isObject() ? QJsonValue(json.object()) : QJsonValue(json.array()))) {
            const QString message = moveFailureMessage(json.isObject() ? QJsonValue(json.object())
                                                                         : QJsonValue(json.array()));
            setStatus(tr("Document move failed"));
            fail(message.isEmpty() ? tr("Readwise could not move the document.") : message);
            m_pendingId.clear();
            clearPendingMove();
            break;
        }
        QFile::remove(persistentLibraryCachePath(m_location));
        updateDocumentRows("location", m_pendingLocation, m_pendingId);
        if (m_selectedId == m_pendingId)
            m_selectedLocation = m_pendingLocation;
        emit selectedDocumentChanged();
        setStatus(tr("Document moved to %1").arg(m_pendingLocation));
        m_pendingId.clear();
        clearPendingMove();
        break;
    case Operation::SaveUrl:
        setStatus(tr("URL saved to Reader"));
        m_pendingUrl.clear();
        refresh();
        break;
    case Operation::SetSeen:
        updateDocumentRows("seen", m_pendingSeen, m_pendingId);
        if (m_pendingId == m_selectedId)
            emit selectedDocumentChanged();
        setStatus(m_pendingSeen ? tr("Document marked seen") : tr("Document marked unseen"));
        m_pendingId.clear();
        break;
    case Operation::SaveTags:
        m_selectedTags = m_pendingTags;
        m_pendingTags.clear();
        updateDocumentRows("tags", m_selectedTags);
        emit selectedDocumentChanged();
        setStatus(tr("Tags saved"));
        break;
    case Operation::CreateHighlight:
        {
        const QVariantList created = highlightRows(json);
        for (const QVariant &item : created) {
            const QVariantMap highlight = item.toMap();
            const QString id = highlight.value("id").toString();
            bool alreadyPresent = false;
            for (const QVariant &existing : m_selectedHighlights) {
                if (!id.isEmpty() && existing.toMap().value("id").toString() == id) {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent)
                m_selectedHighlights.prepend(highlight);
        }
        emit selectedHighlightsChanged();
        m_highlightsCache.insert(m_selectedId, m_selectedHighlights);
        m_highlightsLoaded.insert(m_selectedId);
        setStatus(tr("Highlight saved"));
        m_highlightsLoaded.remove(m_selectedId);
        m_highlightsCache.remove(m_selectedId);
        loadHighlights();
        break;
        }
    case Operation::AddHighlightTags:
    case Operation::RemoveHighlightTags: {
        QVariantList updated;
        for (const QVariant &value : m_selectedHighlights) {
            QVariantMap highlight = value.toMap();
            if (highlight.value("id").toString() == m_pendingHighlightId) {
                QStringList tags = highlight.value("tags").toStringList();
                if (operation == Operation::AddHighlightTags) {
                    for (const QString &tag : m_pendingHighlightTags) {
                        if (!tags.contains(tag, Qt::CaseInsensitive))
                            tags.append(tag);
                    }
                } else {
                    for (const QString &tag : m_pendingHighlightTags) {
                        tags.removeAll(tag);
                        for (int i = tags.size() - 1; i >= 0; --i) {
                            if (tags.at(i).compare(tag, Qt::CaseInsensitive) == 0)
                                tags.removeAt(i);
                        }
                    }
                }
                highlight.insert("tags", tags);
            }
            updated.append(highlight);
        }
        m_selectedHighlights = updated;
        m_highlightsCache.insert(m_selectedId, m_selectedHighlights);
        m_highlightsLoaded.insert(m_selectedId);
        m_pendingHighlightId.clear();
        m_pendingHighlightTags.clear();
        emit selectedHighlightsChanged();
        setStatus(operation == Operation::AddHighlightTags ? tr("Highlight tags added") : tr("Highlight tags removed"));
        break;
    }
    case Operation::None:
    case Operation::ApiDetail:
    case Operation::ValidateApiToken:
    case Operation::Login:
        break;
    }
}
