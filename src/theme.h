#pragma once

#include <QFileSystemWatcher>
#include <QColor>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);
    ~Theme() override;
    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }

signals:
    void changed();

private:
    void reload();
    void refreshRounding();
    QString m_directory;
    QString m_accent = "#FFD60A";
    int m_radius = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll;
};
