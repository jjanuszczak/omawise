#include "backend.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QSet>
#include <QColor>
#include <QTextDocument>
#include <QFont>
#include <QUrl>

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::finished, this, &Backend::finish);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) {
            m_timedOut = true;
            m_process.kill();
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setBusy(false);
            m_operation = Operation::None;
            fail(tr("Could not start the Readwise CLI. Install @readwise/cli and make sure 'readwise' is on PATH."));
        }
    });
    if (QStandardPaths::findExecutable("readwise").isEmpty())
        setStatus(tr("Readwise CLI not found"));
    else
        setStatus(tr("Readwise CLI ready · account not checked"));
}

void Backend::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::fail(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::clearError() {
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

void Backend::run(const QStringList &arguments, Operation operation) {
    if (m_process.state() != QProcess::NotRunning)
        return;

    clearError();
    const QString executable = QStandardPaths::findExecutable("readwise");
    if (executable.isEmpty()) {
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
    m_activeQuery.clear();
    setStatus(tr("Loading %1…").arg(m_location));
    run({"--json", "reader-list-documents", "--location", m_location, "--limit", "50"}, Operation::List);
}

void Backend::setLocation(const QString &location) {
    static const QStringList allowed{"new", "later", "shortlist", "archive"};
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

void Backend::openDocument(const QString &id, const QString &title, const QString &author, const QString &url) {
    if (id.isEmpty())
        return;
    m_pendingId = id;
    m_selectedTitle = title;
    m_selectedAuthor = author;
    m_selectedUrl = url;
    m_selectedContent.clear();
    emit selectedDocumentChanged();
    setStatus(tr("Opening document…"));
    run({"--json", "reader-get-document-details", "--document-id", id}, Operation::Detail);
}

void Backend::archiveDocument(const QString &id) {
    if (id.isEmpty())
        return;
    m_pendingId = id;
    setStatus(tr("Archiving…"));
    run({"--json", "reader-move-documents", "--document-ids", id, "--location", "archive"}, Operation::Archive);
}

void Backend::login() {
    setStatus(tr("Waiting for Readwise login…"));
    run({"login"}, Operation::Login);
}

QString Backend::formattedMarkdown(const QString &markdown, const QString &accent) const {
    QTextDocument document;
    document.setDefaultFont(QFont("Inter", 12));
    const QColor accentColor(accent);
    const QString linkColor = accentColor.isValid() ? accentColor.name(QColor::HexRgb) : QStringLiteral("#FFD60A");
    document.setDefaultStyleSheet(QStringLiteral(R"CSS(
        body { color: #d9dbd5; font-family: Inter, sans-serif; font-size: 13pt; }
        p { margin-top: 0; margin-bottom: 15pt; }
        h1 { color: #f2f3ee; font-size: 23pt; font-weight: 700; margin-top: 0; margin-bottom: 18pt; }
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
    )CSS").arg(linkColor));

    const QString trimmed = markdown.trimmed();
    if (trimmed.startsWith("<!DOCTYPE html", Qt::CaseInsensitive)
        || trimmed.startsWith("<html", Qt::CaseInsensitive)
        || trimmed.startsWith("<article", Qt::CaseInsensitive)) {
        document.setHtml(markdown);
    } else {
        document.setMarkdown(markdown);
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
        for (const QString key : {"id", "title", "author", "source", "site_name", "category", "location", "summary", "reading_time", "listening_time", "word_count", "url", "source_url", "first_opened_at"}) {
            if (!object.value(key).isUndefined() && !object.value(key).isNull())
                row.insert(key, object.value(key).toVariant());
        }
        for (const QString key : {"id", "title", "author", "source", "site_name", "category", "location", "summary", "reading_time", "listening_time", "first_opened_at"}) {
            if (!row.contains(key))
                row.insert(key, QString());
        }
        if (!row.value("id").toString().isEmpty())
            result.append(row);
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
    m_timeout.stop();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    const QByteArray standardOutput = m_process.readAllStandardOutput();
    const QString standardError = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    setBusy(false);

    if (m_timedOut) {
        m_timedOut = false;
        setStatus(tr("Readwise request timed out"));
        fail(tr("The Readwise CLI did not respond before the timeout."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        setStatus(tr("Readwise request failed"));
        if (operation == Operation::Search && !m_localSearchMatches.isEmpty()) {
            m_documents = m_localSearchMatches;
            emit documentsChanged();
        }
        fail(standardError.isEmpty() ? tr("Readwise CLI exited with code %1.").arg(exitCode) : standardError);
        return;
    }

    if (operation == Operation::Login) {
        setStatus(tr("Login command finished · refreshing library"));
        refresh();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(standardOutput, &parseError);
    if (operation == Operation::Detail && parseError.error != QJsonParseError::NoError) {
        // The CLI documents document details as Markdown; accept plain Markdown
        // output as well as JSON so the reader is resilient to CLI output modes.
        if (m_selectedTitle.isEmpty())
            m_selectedTitle = tr("Reader document");
        m_selectedContent = QString::fromUtf8(standardOutput);
        emit selectedDocumentChanged();
        setStatus(tr("Document opened"));
        return;
    }
    if (parseError.error != QJsonParseError::NoError) {
        setStatus(tr("Unexpected CLI response"));
        fail(tr("The Readwise CLI returned unrecognized data."));
        return;
    }

    if (json.isObject()) {
        const QJsonObject root = json.object();
        const QString error = root.value("error").toString();
        if (root.value("isError").toBool() || !error.isEmpty()) {
            setStatus(tr("Readwise request failed"));
            fail(error.isEmpty() ? tr("The Readwise CLI returned an error.") : error);
            return;
        }
    }

    switch (operation) {
    case Operation::List:
        m_libraryDocuments = resultRows(json);
        m_documents = m_libraryDocuments;
        emit documentsChanged();
        setStatus(tr("%1 documents").arg(m_documents.size()));
        if (m_documents.isEmpty())
            fail(tr("No documents in this view."));
        break;
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
        setStatus(tr("%1 results").arg(m_documents.size()));
        if (m_documents.isEmpty())
            fail(tr("No matching documents."));
        break;
    }
    case Operation::Detail: {
        const QJsonObject object = unwrapObject(json);
        const QString title = object.value("title").toString();
        const QString author = object.value("author").toString();
        if (!title.isEmpty()) m_selectedTitle = title;
        if (!author.isEmpty()) m_selectedAuthor = author;
        for (const QString key : {"content", "markdown", "markdown_content", "document_content", "text"}) {
            if (object.value(key).isString() && !object.value(key).toString().isEmpty()) {
                m_selectedContent = object.value(key).toString();
                break;
            }
        }
        const QString url = object.value("url").toString();
        if (!url.isEmpty()) m_selectedUrl = url;
        if (m_selectedContent.isEmpty())
            m_selectedContent = tr("The CLI returned document metadata without a Markdown body.");
        emit selectedDocumentChanged();
        setStatus(tr("Document opened"));
        break;
    }
    case Operation::Archive:
        setStatus(tr("Document archived"));
        refresh();
        break;
    case Operation::None:
    case Operation::Login:
        break;
    }
}
