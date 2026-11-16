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

bool isHtmlContent(const QString &content) {
    static const QRegularExpression htmlStart(
        QStringLiteral("^<(?:!doctype|html|article|h[1-6]|p|div|section|main|ul|ol|li|blockquote|pre|table|figure|img|hr|br|a|span|b|i|strong|em|code)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return htmlStart.match(content.trimmed()).hasMatch();
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
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const bool failedMetadata = m_operation == Operation::Highlights;
            setBusy(false);
            m_operation = Operation::None;
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
    if (m_busy || m_apiLoginBusy) {
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
    if (m_busy || m_apiLoginBusy)
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

void Backend::setMetadataBusy(bool busy) {
    if (m_metadataBusy == busy)
        return;
    m_metadataBusy = busy;
    emit metadataBusyChanged();
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
    if (id.isEmpty() || !allowedLocations.contains(location))
        return;
    m_pendingId = id;
    m_pendingLocation = location;
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
    QJsonObject document{{"document_id", id}, {"reading_progress", progress}};
    QJsonArray documents{document};
    m_pendingId = id;
    m_pendingProgress = progress;
    setStatus(tr("Saving reading progress…"));
    run({"--json", "reader-bulk-edit-document-metadata", "--documents",
         QString::fromUtf8(QJsonDocument(documents).toJson(QJsonDocument::Compact))}, Operation::SaveProgress);
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
    m_pendingId = id;
    m_pendingNote = note;
    setStatus(tr("Saving note…"));
    run({"--json", "reader-bulk-edit-document-metadata", "--documents", payload}, Operation::SaveNote);
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
    if (refreshHighlightsBeforeEdit(documentId, highlightId, PendingHighlightEdit::SaveNote, note))
        return;
    m_pendingId = documentId;
    m_pendingHighlightId = highlightId;
    m_pendingHighlightNote = note;
    setStatus(tr("Saving highlight note…"));
    run({"--json", "reader-set-highlight-notes", "--document-id", documentId,
         "--highlight-document-id", highlightId, "--notes", note}, Operation::SaveHighlightNote);
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
    if (refreshHighlightsBeforeEdit(documentId, highlightId, PendingHighlightEdit::AddTags, {}, cleaned))
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
    if (refreshHighlightsBeforeEdit(documentId, highlightId, PendingHighlightEdit::RemoveTags, {}, cleaned))
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
                                          PendingHighlightEdit edit, const QString &note,
                                          const QStringList &tags) {
    if (m_dispatchingFreshHighlightEdit) {
        m_dispatchingFreshHighlightEdit = false;
        return false;
    }
    m_pendingHighlightEdit = edit;
    m_pendingHighlightId = highlightId;
    m_pendingHighlightNote = note;
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
        const QString note = m_pendingHighlightNote;
        const QStringList tags = m_pendingHighlightTags;
        m_pendingHighlightEdit = PendingHighlightEdit::None;
        if (edit == PendingHighlightEdit::SaveNote)
            saveHighlightNote(documentId, highlightId, note);
        else if (edit == PendingHighlightEdit::AddTags)
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

QString Backend::formattedMarkdown(const QString &markdown, const QString &accent,
                                  const QVariantMap &readerStyle) const {
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
    document.setDefaultStyleSheet(QStringLiteral(R"CSS(
        body { color: #d9dbd5; font-family: "%2"; font-size: %3px; margin: 0; padding: 0; }
        p { margin-top: 0; margin-bottom: %4pt; }
        h1 { color: #f2f3ee; font-size: 23pt; font-weight: 700; margin-top: 8pt; margin-bottom: 18pt; }
        h2 { color: #eef0e9; font-size: 18pt; font-weight: 650; margin-top: 25pt; margin-bottom: 11pt; }
        h3 { color: #e7e9e2; font-size: 14pt; font-weight: 650; margin-top: 20pt; margin-bottom: 9pt; }
        h4, h5, h6 { color: #e2e4dd; font-size: 12pt; font-weight: 650; margin-top: 16pt; margin-bottom: 7pt; }
        a { color: %1; text-decoration: none; }
        blockquote { color: #b6bbae; background-color: #22262a; border-left: 3px solid %1; margin: 12pt 0; padding: 8pt 13pt; }
        pre { color: #e1e5d9; background-color: #22262a; border: 1px solid #343940; margin: 10pt 0; padding: 11pt; }
        code { color: #e3e9d2; background-color: #292e2c; }
        li { margin-bottom: 5pt; }
        ul, ol { margin-top: 4pt; margin-bottom: 12pt; }
        table { border-collapse: collapse; margin: 12pt 0; }
        th { color: #f0f1eb; background-color: #2a2e33; font-weight: 650; }
        td, th { border: 1px solid #41464c; padding: 6pt 8pt; }
        hr { color: #41464c; }
        img { max-width: 100%; }
    )CSS").arg(linkColor, fontFamily).arg(fontSizePx).arg(paragraphSpacing));

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

bool Backend::copyArticleMarkdown(const QString &content, const QVariantMap &metadata) {
    if (content.isEmpty() || !QGuiApplication::instance())
        return false;

    QString markdown = content;
    if (isHtmlContent(content)) {
        QTextDocument document;
        document.setHtml(content);
        markdown = document.toMarkdown();
    }

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
    QGuiApplication::clipboard()->setText(frontmatter + markdown);
    setStatus(tr("Article Markdown copied to clipboard"));
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
        return;
    }

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
            if (operation == Operation::Highlights) {
                setMetadataBusy(false);
                m_pendingHighlightEdit = PendingHighlightEdit::None;
                m_dispatchingFreshHighlightEdit = false;
            }
            setStatus(tr("Readwise request failed"));
            fail(error.isEmpty() ? tr("The Readwise CLI returned an error.") : error);
            return;
        }
        if ((operation == Operation::SaveNote || operation == Operation::SaveTags
             || operation == Operation::CreateHighlight || operation == Operation::SaveHighlightNote
             || operation == Operation::AddHighlightTags || operation == Operation::RemoveHighlightTags
             || operation == Operation::SetSeen || operation == Operation::SaveProgress
             || operation == Operation::SaveUrl)
            && root.value("success").isBool()
            && !root.value("success").toBool()) {
            setStatus(tr("Readwise update failed"));
            fail(root.value("message").toString().isEmpty()
                     ? tr("Readwise could not apply the requested update.")
                     : root.value("message").toString());
            return;
        }
        if ((operation == Operation::SaveNote || operation == Operation::SaveTags
             || operation == Operation::CreateHighlight || operation == Operation::SaveHighlightNote
             || operation == Operation::AddHighlightTags || operation == Operation::RemoveHighlightTags
             || operation == Operation::SetSeen || operation == Operation::SaveProgress
             || operation == Operation::SaveUrl)
            && root.value("results").isArray()) {
            for (const QJsonValue &value : root.value("results").toArray()) {
                const QJsonObject result = value.toObject();
                if (!result.value("success").isBool() || result.value("success").toBool())
                    continue;
                m_pendingNote.clear();
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
        QFile::remove(persistentLibraryCachePath(m_location));
        updateDocumentRows("location", m_pendingLocation);
        setStatus(tr("Document moved to %1").arg(m_pendingLocation));
        m_pendingId.clear();
        m_pendingLocation.clear();
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
    case Operation::SaveProgress:
        updateDocumentRows("reading_progress", m_pendingProgress, m_pendingId);
        setStatus(tr("Reading progress saved"));
        m_pendingId.clear();
        break;
    case Operation::SaveNote:
        m_selectedNote = m_pendingNote;
        m_pendingNote.clear();
        updateDocumentRows("notes", m_selectedNote);
        emit selectedDocumentChanged();
        setStatus(tr("Note saved"));
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
    case Operation::SaveHighlightNote: {
        QVariantList updated;
        for (const QVariant &value : m_selectedHighlights) {
            QVariantMap highlight = value.toMap();
            if (highlight.value("id").toString() == m_pendingHighlightId)
                highlight.insert("notes", m_pendingHighlightNote);
            updated.append(highlight);
        }
        m_selectedHighlights = updated;
        m_highlightsCache.insert(m_selectedId, m_selectedHighlights);
        m_highlightsLoaded.insert(m_selectedId);
        m_pendingHighlightId.clear();
        m_pendingHighlightNote.clear();
        emit selectedHighlightsChanged();
        setStatus(tr("Highlight note saved"));
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
