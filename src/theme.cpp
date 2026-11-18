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
#include <QSet>
#include <QSaveFile>
#include <QLocale>
#include <cmath>

Theme::Theme(QObject *parent)
    : QObject(parent), m_directory(QDir::homePath() + "/.local/state/omarchy/current"),
      m_configDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omawise"),
      m_configFile(m_configDirectory + "/reader.toml") {
    m_readerStyle = {{"fontFamily", "monospace"}, {"fontSizePx", 12}, {"lineSpacing", 1.2},
                     {"paragraphSpacingPt", 18}, {"contentWidth", 760}, {"progressMode", "last_position"},
                     {"externalEditors", QStringList{"neovim", "vscode", "obsidian"}}};
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
                      {"paragraphSpacingPt", 18}, {"contentWidth", 760}, {"progressMode", "last_position"},
                      {"externalEditors", QStringList{"neovim", "vscode", "obsidian"}}};
    QFile file(m_configFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString contents = QString::fromUtf8(file.readAll());
        const QRegularExpression keyValue(
            R"re(^\s*(font_family|font_size_px|line_spacing|paragraph_spacing_pt|content_width|progress_mode)\s*=\s*(?:"([^"]*)"|([^#\s]+)).*$)re",
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
            } else if (key == "progress_mode") {
                if (value == "last_position" || value == "furthest_position")
                    style["progressMode"] = value;
            } else {
                const double number = value.toDouble(&ok);
                if (!ok || !std::isfinite(number)) continue;
                if (key == "font_size_px") style["fontSizePx"] = qBound(12, qRound(number), 48);
                else if (key == "line_spacing") style["lineSpacing"] = qBound(1.0, number, 2.2);
                else if (key == "paragraph_spacing_pt") style["paragraphSpacingPt"] = qBound(0, qRound(number), 36);
                else if (key == "content_width") style["contentWidth"] = qBound(480, qRound(number), 1200);
            }
        }
        const QRegularExpression editorListExpression(
            R"re(^\s*external_editors\s*=\s*\[([^\]]*)\].*$)re",
            QRegularExpression::MultilineOption);
        const auto editorMatch = editorListExpression.match(contents);
        if (editorMatch.hasMatch()) {
            static const QSet<QString> supportedEditors{
                "neovim", "vscode", "obsidian", "helix", "emacs", "zed", "sublime", "kate", "mousepad"};
            QStringList editors;
            const QRegularExpression idExpression(R"re("([A-Za-z0-9_-]+)")re");
            auto ids = idExpression.globalMatch(editorMatch.captured(1));
            while (ids.hasNext()) {
                const QString id = ids.next().captured(1);
                if (supportedEditors.contains(id) && !editors.contains(id))
                    editors.append(id);
            }
            style["externalEditors"] = editors;
        }
    }
    if (style != m_readerStyle) {
        m_readerStyle = style;
        emit changed();
    }
}

bool Theme::saveReaderStyle(const QVariantMap &style) {
    QVariantMap updated = m_readerStyle;
    const QString family = style.value("fontFamily", updated.value("fontFamily")).toString().trimmed();
    if (!family.isEmpty() && family.size() <= 80
        && QRegularExpression("^[A-Za-z0-9 _,-]+$").match(family).hasMatch())
        updated["fontFamily"] = family;

    bool ok = false;
    const double fontSize = style.value("fontSizePx", updated.value("fontSizePx")).toDouble(&ok);
    if (ok && std::isfinite(fontSize)) updated["fontSizePx"] = qBound(12, qRound(fontSize), 48);
    const double lineSpacing = style.value("lineSpacing", updated.value("lineSpacing")).toDouble(&ok);
    if (ok && std::isfinite(lineSpacing))
        updated["lineSpacing"] = qBound(1.0, qRound(lineSpacing * 10.0) / 10.0, 2.2);
    const double paragraphSpacing = style.value("paragraphSpacingPt", updated.value("paragraphSpacingPt")).toDouble(&ok);
    if (ok && std::isfinite(paragraphSpacing))
        updated["paragraphSpacingPt"] = qBound(0, qRound(paragraphSpacing), 36);
    const double contentWidth = style.value("contentWidth", updated.value("contentWidth")).toDouble(&ok);
    if (ok && std::isfinite(contentWidth))
        updated["contentWidth"] = qBound(480, qRound(contentWidth), 1200);
    const QString progressMode = style.value("progressMode", updated.value("progressMode")).toString();
    if (progressMode == "last_position" || progressMode == "furthest_position")
        updated["progressMode"] = progressMode;
    static const QSet<QString> supportedEditors{
        "neovim", "vscode", "obsidian", "helix", "emacs", "zed", "sublime", "kate", "mousepad"};
    QStringList editors;
    for (const QString &id : style.value("externalEditors", updated.value("externalEditors")).toStringList()) {
        if (supportedEditors.contains(id) && !editors.contains(id))
            editors.append(id);
    }
    updated["externalEditors"] = editors;

    if (!QDir().mkpath(m_configDirectory)) {
        m_readerStyleError = tr("Could not create the Omawise settings directory.");
        emit changed();
        return false;
    }
    QSaveFile file(m_configFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_readerStyleError = tr("Could not write reader.toml: %1").arg(file.errorString());
        emit changed();
        return false;
    }
    const QString contents = QStringLiteral(
        "# Omawise reader and editor settings. Changes apply automatically while Omawise is running.\n"
        "[reader]\n"
        "font_family = \"%1\"\n"
        "font_size_px = %2\n"
        "line_spacing = %3\n"
        "paragraph_spacing_pt = %4\n"
        "content_width = %5\n"
        "progress_mode = \"%6\"\n"
        "external_editors = [%7]\n")
        .arg(updated.value("fontFamily").toString())
        .arg(updated.value("fontSizePx").toInt())
        .arg(QLocale::c().toString(updated.value("lineSpacing").toDouble(), 'f', 1))
        .arg(updated.value("paragraphSpacingPt").toInt())
        .arg(updated.value("contentWidth").toInt())
        .arg(updated.value("progressMode").toString())
        .arg([&editors] {
            QStringList quoted;
            for (const QString &id : editors)
                quoted.append(QStringLiteral("\"%1\"").arg(id));
            return quoted.join(QStringLiteral(", "));
        }());
    const QByteArray bytes = contents.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_readerStyleError = tr("Could not save reader.toml: %1").arg(file.errorString());
        emit changed();
        return false;
    }
    m_readerStyleError.clear();
    reloadReaderStyle();
    emit changed();
    return true;
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

QString Theme::accentForeground() const {
    return m_colors.value("accentForeground", QStringLiteral("white")).toString();
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
    const QString stateDirectory = QFileInfo(m_directory).absolutePath();
    for (const QString &path : {ancestor, QFileInfo(ancestor).absolutePath(), stateDirectory,
                                stateDirectory + "/theme.name", m_directory,
                                m_directory + "/theme", m_directory + "/theme/colors.toml"}) {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }

    QVariantMap source{
        {"mode", "dark"}, {"accent", "#FFD60A"}, {"selection", "#30352a"},
        {"muted", "#858a83"}, {"background", "#25282e"}, {"dark_background", "#1d2025"},
        {"darker_background", "#17191d"}, {"lighter_background", "#272a2f"},
        {"foreground", "#e4e5e0"}, {"dark_foreground", "#858a83"},
        {"light_foreground", "#b7bbb3"}, {"bright_foreground", "#f0f1ed"},
        {"red", "#ffb4ab"}, {"yellow", "#e4dfce"}, {"green", "#8bcbb1"}
    };
    QFile file(m_directory + "/theme/colors.toml");
    if (file.open(QIODevice::ReadOnly)) {
        const QString contents = QString::fromUtf8(file.readAll());
        const QRegularExpression expression(
            R"re(^\s*([A-Za-z_][A-Za-z0-9_-]*)\s*=\s*(?:["']([^"']*)["']|(#[0-9a-fA-F]{6}))\s*(?:#.*)?$)re",
            QRegularExpression::MultilineOption);
        auto matches = expression.globalMatch(contents);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const QString key = match.captured(1);
            if (key == "mode") {
                const QString mode = match.captured(2).toLower();
                if (mode == "dark" || mode == "light") source[key] = mode;
            } else if (source.contains(key)) {
                const QString value = match.captured(3).isEmpty() ? match.captured(2) : match.captured(3);
                if (QColor(value).isValid()) source[key] = QColor(value).name(QColor::HexRgb);
            }
        }
    }

    QVariantMap colors{
        {"window", source.value("darker_background")}, {"panel", source.value("dark_background")},
        {"surface", source.value("background")}, {"surfaceAlt", source.value("lighter_background")},
        {"border", source.value("muted")}, {"borderStrong", source.value("dark_foreground")},
        {"text", source.value("foreground")}, {"textBright", source.value("bright_foreground")},
        {"textSecondary", source.value("light_foreground")}, {"textMuted", source.value("dark_foreground")},
        {"accent", source.value("accent")}, {"selection", source.value("selection")},
        {"error", source.value("red")}, {"warning", source.value("yellow")},
        {"success", source.value("green")}
    };
    const QColor accentColor(colors.value("accent").toString());
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    const double luminance = 0.2126 * linear(accentColor.redF())
        + 0.7152 * linear(accentColor.greenF())
        + 0.0722 * linear(accentColor.blueF());
    colors["accentForeground"] = luminance > 0.45 ? QStringLiteral("#101315") : QStringLiteral("#ffffff");
    colors["tagBackground"] = source.value("selection");
    QColor scrim(colors.value("window").toString());
    scrim.setAlpha(217);
    colors["scrim"] = scrim.name(QColor::HexArgb);

    const QString accent = colors.value("accent").toString();
    const QString colorScheme = source.value("mode").toString();
    const bool changedColors = colors != m_colors || colorScheme != m_colorScheme || accent != m_accent;
    m_colors = colors;
    m_colorScheme = colorScheme;
    m_accent = accent;
    if (changedColors) {
        if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QPalette palette = QGuiApplication::palette();
            palette.setColor(QPalette::Link, QColor(m_accent));
            palette.setColor(QPalette::Highlight, QColor(colors.value("selection").toString()));
            palette.setColor(QPalette::HighlightedText, QColor(colors.value("textBright").toString()));
            QGuiApplication::setPalette(palette);
        }
        emit changed();
    }
}
