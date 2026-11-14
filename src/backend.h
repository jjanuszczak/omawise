#pragma once

#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList documents READ documents NOTIFY documentsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QString selectedAuthor READ selectedAuthor NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QString selectedContent READ selectedContent NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QString selectedUrl READ selectedUrl NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QString selectedNote READ selectedNote NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QStringList selectedTags READ selectedTags NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QVariantList selectedHighlights READ selectedHighlights NOTIFY selectedHighlightsChanged)
    Q_PROPERTY(QVariantList selectedHeadings READ selectedHeadings NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QVariantList selectedParagraphs READ selectedParagraphs NOTIFY selectedDocumentChanged)
    Q_PROPERTY(QString location READ location NOTIFY locationChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    QVariantList documents() const { return m_documents; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString status() const { return m_status; }
    QString selectedTitle() const { return m_selectedTitle; }
    QString selectedAuthor() const { return m_selectedAuthor; }
    QString selectedContent() const { return m_selectedContent; }
    QString selectedUrl() const { return m_selectedUrl; }
    QString selectedNote() const { return m_selectedNote; }
    QStringList selectedTags() const { return m_selectedTags; }
    QVariantList selectedHighlights() const { return m_selectedHighlights; }
    QVariantList selectedHeadings() const;
    QVariantList selectedParagraphs() const;
    QString location() const { return m_location; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setLocation(const QString &location);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void openDocument(const QString &id, const QString &title, const QString &author, const QString &url,
                                  const QStringList &tags = {}, const QString &note = {});
    Q_INVOKABLE void archiveDocument(const QString &id);
    Q_INVOKABLE void moveDocument(const QString &id, const QString &location);
    Q_INVOKABLE void saveDocumentNote(const QString &id, const QString &note);
    Q_INVOKABLE void saveDocumentTags(const QString &id, const QStringList &tags);
    Q_INVOKABLE void createHighlight(const QString &documentId, const QString &text);
    Q_INVOKABLE void saveHighlightNote(const QString &documentId, const QString &highlightId, const QString &note);
    Q_INVOKABLE void addHighlightTags(const QString &documentId, const QString &highlightId, const QStringList &tags);
    Q_INVOKABLE void removeHighlightTags(const QString &documentId, const QString &highlightId, const QStringList &tags);
    Q_INVOKABLE void loadHighlights();
    Q_INVOKABLE void login();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE QString formattedMarkdown(const QString &markdown, const QString &accent) const;

signals:
    void documentsChanged();
    void busyChanged();
    void errorChanged();
    void statusChanged();
    void selectedDocumentChanged();
    void selectedHighlightsChanged();
    void locationChanged();

private:
    enum class Operation { None, List, Search, TitleSearch, Detail, Highlights, Archive, Move, SaveNote, SaveTags, CreateHighlight, SaveHighlightNote, AddHighlightTags, RemoveHighlightTags, Login };
    void run(const QStringList &arguments, Operation operation);
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(const QString &message);
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void updateDocumentRows(const QString &key, const QVariant &value);
    static QVariantList resultRows(const QJsonDocument &document);
    static QVariantList highlightRows(const QJsonDocument &document);
    static QJsonObject unwrapObject(const QJsonDocument &document);

    QProcess m_process;
    QTimer m_timeout;
    Operation m_operation = Operation::None;
    QVariantList m_documents;
    QVariantList m_libraryDocuments;
    QVariantList m_localSearchMatches;
    bool m_busy = false;
    bool m_timedOut = false;
    QString m_error;
    QString m_status;
    QString m_selectedTitle;
    QString m_selectedAuthor;
    QString m_selectedContent;
    QString m_selectedUrl;
    QString m_selectedNote;
    QString m_selectedId;
    QStringList m_selectedTags;
    QVariantList m_selectedHighlights;
    QString m_location = "new";
    QString m_activeQuery;
    QString m_pendingId;
    QString m_pendingLocation;
    QString m_pendingNote;
    QStringList m_pendingTags;
    QString m_pendingHighlightId;
    QString m_pendingHighlightNote;
    QStringList m_pendingHighlightTags;
};
