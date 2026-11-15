#pragma once

#include <QFileSystemWatcher>
#include <QColor>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString uiFont READ uiFont CONSTANT)
    Q_PROPERTY(QVariantMap readerStyle READ readerStyle NOTIFY changed)
    Q_PROPERTY(QString readerStyleError READ readerStyleError NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);
    ~Theme() override;
    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QString uiFont() const { return QStringLiteral("monospace"); }
    QVariantMap readerStyle() const { return m_readerStyle; }
    QString readerStyleError() const { return m_readerStyleError; }
    Q_INVOKABLE bool saveReaderStyle(const QVariantMap &style);

signals:
    void changed();

private:
    void reload();
    void reloadReaderStyle();
    void refreshRounding();
    QString m_directory;
    QString m_configDirectory;
    QString m_configFile;
    QVariantMap m_readerStyle;
    QString m_readerStyleError;
    QString m_accent = "#FFD60A";
    int m_radius = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll;
};
