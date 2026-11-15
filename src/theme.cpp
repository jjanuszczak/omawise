#include "theme.h"

#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QStandardPaths>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(QObject *parent)
    : QObject(parent), m_directory(QDir::homePath() + "/.local/state/omarchy/current"),
      m_configDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omawise"),
      m_configFile(m_configDirectory + "/reader.toml") {
    m_readerStyle = {{"fontFamily", "monospace"}, {"fontSizePx", 12}, {"lineSpacing", 1.2},
                     {"paragraphSpacingPt", 18}, {"contentWidth", 760}};
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    QFileSystemWatcher *configWatcher = new QFileSystemWatcher(this);
    QString configWatchAncestor = m_configDirectory;
    while (!QFileInfo::exists(configWatchAncestor) && configWatchAncestor != "/")
        configWatchAncestor = QFileInfo(configWatchAncestor).absolutePath();
    if (QFileInfo::exists(configWatchAncestor)) configWatcher->addPath(configWatchAncestor);
    const auto watchConfigFiles = [this, configWatcher]() {
        if (QFileInfo::exists(m_configDirectory) && !configWatcher->directories().contains(m_configDirectory))
            configWatcher->addPath(m_configDirectory);
        if (QFileInfo::exists(m_configFile) && !configWatcher->files().contains(m_configFile))
            configWatcher->addPath(m_configFile);
    };
    connect(configWatcher, &QFileSystemWatcher::directoryChanged, this, [this, watchConfigFiles]() {
        reloadReaderStyle();
        watchConfigFiles();
    });
    connect(configWatcher, &QFileSystemWatcher::fileChanged, this, [this, watchConfigFiles]() {
        reloadReaderStyle();
        watchConfigFiles();
    });
    watchConfigFiles();
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        m_hyprctl = QStandardPaths::findExecutable("hyprctl");
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto response = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object();
        const int radius = response.value("int").toInt(-1);
        if (code == 0 && status == QProcess::NormalExit && radius >= 0 && radius != m_radius) {
            m_radius = radius;
            emit changed();
        }
    });
    if (!m_hyprctl.isEmpty()) {
        m_roundingPoll.setInterval(1200);
        m_roundingPoll.start();
        refreshRounding();
    }
    reload();
    reloadReaderStyle();
}

void Theme::reloadReaderStyle() {
    QVariantMap style{{"fontFamily", "monospace"}, {"fontSizePx", 12}, {"lineSpacing", 1.2},
                      {"paragraphSpacingPt", 18}, {"contentWidth", 760}};
    QFile file(m_configFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString contents = QString::fromUtf8(file.readAll());
        const QRegularExpression keyValue(
            R"re(^\s*(font_family|font_size_px|line_spacing|paragraph_spacing_pt|content_width)\s*=\s*(?:"([^"]*)"|([^#\s]+)).*$)re",
            QRegularExpression::MultilineOption);
        auto matches = keyValue.globalMatch(contents);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const QString key = match.captured(1);
            const QString value = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
            bool ok = false;
            if (key == "font_family") {
                if (!value.isEmpty() && value.size() <= 80
                    && QRegularExpression("^[A-Za-z0-9 _,-]+$").match(value).hasMatch())
                    style["fontFamily"] = value;
            } else {
                const double number = value.toDouble(&ok);
                if (!ok || !std::isfinite(number)) continue;
                if (key == "font_size_px") style["fontSizePx"] = qBound(12, qRound(number), 48);
                else if (key == "line_spacing") style["lineSpacing"] = qBound(1.0, number, 2.2);
                else if (key == "paragraph_spacing_pt") style["paragraphSpacingPt"] = qBound(0, qRound(number), 36);
                else if (key == "content_width") style["contentWidth"] = qBound(480, qRound(number), 1200);
            }
        }
    }
    if (style != m_readerStyle) {
        m_readerStyle = style;
        emit changed();
    }
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

QString Theme::accentForeground() const {
    const QColor color(m_accent);
    auto linear = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    return luminance > 0.179 ? "black" : "white";
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
}

void Theme::reload() {
    const auto oldPaths = m_watcher.files() + m_watcher.directories();
    if (!oldPaths.isEmpty())
        m_watcher.removePaths(oldPaths);

    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    for (const QString &path : {ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                                m_directory + "/theme", m_directory + "/theme/colors.toml"}) {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }

    QString accent = "#FFD60A";
    QFile file(m_directory + "/theme/colors.toml");
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            accent = match.captured(1);
    }
    if (accent != m_accent) {
        m_accent = accent;
        if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QPalette palette = QGuiApplication::palette();
            palette.setColor(QPalette::Link, QColor(m_accent));
            QGuiApplication::setPalette(palette);
        }
        emit changed();
    }
}
