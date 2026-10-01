/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "thememanager.h"
#include "appearanceadaptor.h"

#include <QDomDocument>
#include <QTextStream>
#include <QDBusInterface>
#include <QProcess>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QStandardPaths>
static const QByteArray s_systemFontName = QByteArrayLiteral("Font");
static const QByteArray s_systemFixedFontName = QByteArrayLiteral("FixedFont");
static const QByteArray s_systemPointFontSize = QByteArrayLiteral("FontSize");

static QString gtkRc2Path()
{
    return QDir::homePath() + QLatin1String("/.gtkrc-2.0");
}

static QString gtk3SettingsIniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/gtk-3.0/settings.ini");
}

static QString gtk4SettingsIniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/gtk-4.0/settings.ini");
}

ThemeManager *ThemeManager::self()
{
    static ThemeManager t;
    return &t;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(QStringLiteral("cutefishos"), QStringLiteral("theme")))
{
    if (!QFile::exists(m_settings->fileName())) {
        QFile file(m_settings->fileName());
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream in(&file);
            in << "";
            file.close();
        }
    }

    // init dbus
    new AppearanceAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/com/cutefish/Services/Appearance"), this);

    // init value
    m_isDarkMode = m_settings->value("DarkMode", false).toBool();
    m_darkModeDimsWallpaer = m_settings->value("DarkModeDimsWallpaer", false).toBool();
    m_blurEnabled = m_settings->value("BlurEnabled", true).toBool();
    m_backgroundVisible = true;
    m_wallpaperPath = m_settings->value("Wallpaper", "/usr/share/backgrounds/cutefishos/default.jpg").toString();
    m_accentColor = m_settings->value("AccentColor", 0).toInt();
    m_backgroundType = m_settings->value("BackgroundType", 0).toInt();
    m_backgroundColor = m_settings->value("BackgroundColor", "#2B8ADA").toString();
    m_cursorTheme = m_settings->value("CursorTheme", "default").toString();
    m_cursorSize = m_settings->value("CursorSize", 24).toInt();
    m_iconTheme = m_settings->value("IconTheme", "Cutefish").toString();
    if (m_iconTheme == "WhiteSur-dark" || m_iconTheme.isEmpty()) {
        m_iconTheme = "Cutefish";
        m_settings->setValue("IconTheme", m_iconTheme);
    }
    QIcon::setThemeName(m_iconTheme);

    // Synchronize toolkit and compositor cursor settings at session startup.
    applyCursorSettings();


    // Init fonts.
    if (!m_settings->contains(s_systemFixedFontName)) {
        m_settings->setValue(s_systemFixedFontName, "Monospace");
    }

    if (!m_settings->contains(s_systemFontName)) {
        QSettings lanSettings(QStringLiteral("cutefishos"), QStringLiteral("language"));
        QString languageCode = lanSettings.value("language").toString();
        QString fontName;

        if (languageCode == "zh_CN") {
            fontName = "Noto Sans CJK SC";
        } else if (languageCode.contains("en_")) {
            fontName = "Noto Sans";
        } else if (languageCode == "zh_TW") {
            fontName = "Noto Sans CJK TC";
        } else if (languageCode == "zh_HK") {
            fontName = "Noto Serif CJK HK";
        } else if (languageCode == "ja_JP") {
            fontName = "Noto Serif CJK JP";
        } else {
            fontName = "Noto Sans";
        }

        m_settings->setValue(s_systemFontName, fontName);
    }

    // 登陆后更新 fontconfig
    updateFontConfig();
    updateGtk3Config();
    updateDesktopColorScheme();

}

bool ThemeManager::isDarkMode()
{
    return m_isDarkMode;
}

void ThemeManager::setDarkMode(bool darkMode)
{
    if (darkMode == m_isDarkMode)
        return;

    m_isDarkMode = darkMode;
    m_settings->setValue("DarkMode", darkMode);

    updateGtk3Config();
    updateDesktopColorScheme();

    emit darkModeChanged(m_isDarkMode);
}

bool ThemeManager::darkModeDimsWallpaer() const
{
    return m_darkModeDimsWallpaer;
}

void ThemeManager::setDarkModeDimsWallpaer(bool value)
{
    if (m_darkModeDimsWallpaer == value)
        return;

    m_darkModeDimsWallpaer = value;
    m_settings->setValue("DarkModeDimsWallpaer", m_darkModeDimsWallpaer);

    emit darkModeDimsWallpaerChanged();
}

bool ThemeManager::blurEnabled() const
{
    return m_blurEnabled;
}

void ThemeManager::setBlurEnabled(bool value)
{
    if (m_blurEnabled == value)
        return;

    m_blurEnabled = value;
    m_settings->setValue("BlurEnabled", m_blurEnabled);

    emit blurEnabledChanged();
}

bool ThemeManager::backgroundVisible() const
{
    return m_backgroundVisible;
}

void ThemeManager::setBackgroundVisible(bool value)
{
    if (m_backgroundVisible == value)
        return;

    m_backgroundVisible = value;
    emit backgroundVisibleChanged();
}

int ThemeManager::accentColor()
{
    return m_settings->value("AccentColor", 0).toInt();
}

void ThemeManager::setAccentColor(int accentColor)
{
    if (m_accentColor == accentColor)
        return;
    
    m_accentColor = accentColor;
    m_settings->setValue("AccentColor", m_accentColor);

    emit accentColorChanged(m_accentColor);
}

QString ThemeManager::cursorTheme() const
{
    return m_cursorTheme;
}

void ThemeManager::setCursorTheme(const QString &theme)
{
    if (m_cursorTheme != theme) {
        m_cursorTheme = theme;
        m_settings->setValue("CursorTheme", m_cursorTheme);
        applyCursorSettings();
        emit cursorThemeChanged();
    }
}

int ThemeManager::cursorSize() const
{
    return m_cursorSize;
}

void ThemeManager::setCursorSize(int size)
{
    if (m_cursorSize != size) {
        m_cursorSize = size;
        m_settings->setValue("CursorSize", m_cursorSize);
        applyCursorSettings();
        emit cursorSizeChanged();
    }
}

void ThemeManager::updateGtk2Config()
{

}

QString ThemeManager::systemFont()
{
    return m_settings->value(s_systemFontName, "SF Pro Text").toString();
}

void ThemeManager::setSystemFont(const QString &fontFamily)
{
    m_settings->setValue(s_systemFontName, fontFamily);
    updateGtk3Config();
    updateFontConfig();

    emit systemFontChanged();
}

QString ThemeManager::systemFixedFont()
{
    return m_settings->value(s_systemFixedFontName, "Monospace").toString();
}

void ThemeManager::setSystemFixedFont(const QString &fontFamily)
{
    m_settings->setValue(s_systemFixedFontName, fontFamily);

    updateFontConfig();
}

qreal ThemeManager::systemFontPointSize()
{
    return m_settings->value(s_systemPointFontSize, 9).toReal();
}

void ThemeManager::setSystemFontPointSize(qreal fontSize)
{
    m_settings->setValue(s_systemPointFontSize, fontSize);
    updateGtk3Config();
    emit systemFontPointSizeChanged();
}

QString ThemeManager::wallpaper()
{
    return m_wallpaperPath;
}

void ThemeManager::setWallpaper(const QString &path)
{
    if (m_wallpaperPath != path) {
        m_wallpaperPath = path;
        m_settings->setValue("Wallpaper", path);
        emit wallpaperChanged(path);
    }
}

int ThemeManager::backgroundType()
{
    return m_backgroundType;
}

void ThemeManager::setBackgroundType(int type)
{
    if (m_backgroundType != type) {
        m_backgroundType = type;
        m_settings->setValue("BackgroundType", m_backgroundType);
        emit backgroundTypeChanged();
    }
}

QString ThemeManager::backgroundColor()
{
    return m_backgroundColor;
}

void ThemeManager::setBackgroundColor(QString color)
{
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        m_settings->setValue("BackgroundColor", m_backgroundColor);
        emit backgroundColorChanged();
    }
}

void ThemeManager::updateGtk3Config()
{
    const QString gtk3Path = gtk3SettingsIniPath();
    QDir().mkpath(QFileInfo(gtk3Path).absolutePath());
    QSettings settings(gtk3Path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Settings"));

    // font
    settings.setValue("gtk-font-name", QString("%1 %2").arg(systemFont()).arg(systemFontPointSize()));
    // dark mode
    settings.setValue("gtk-application-prefer-dark-theme", isDarkMode());
    // icon theme
    settings.setValue("gtk-icon-theme-name", m_iconTheme);
    settings.setValue("gtk-cursor-theme-name", m_cursorTheme);
    settings.setValue("gtk-cursor-theme-size", m_cursorSize);
    // other
    settings.setValue("gtk-enable-animations", true);
    settings.setValue("gtk-decoration-layout", "close,minimize,maximize:");
    // theme
    settings.setValue("gtk-theme-name", isDarkMode() ? "Cutefish-dark" : "Cutefish-light");
    settings.endGroup();
    settings.sync();

    const QString gtk4Path = gtk4SettingsIniPath();
    QDir().mkpath(QFileInfo(gtk4Path).absolutePath());
    QSettings gtk4Settings(gtk4Path, QSettings::IniFormat);
    gtk4Settings.beginGroup(QStringLiteral("Settings"));
    gtk4Settings.setValue("gtk-font-name", QString("%1 %2").arg(systemFont()).arg(systemFontPointSize()));
    gtk4Settings.setValue("gtk-application-prefer-dark-theme", isDarkMode());
    gtk4Settings.setValue("gtk-icon-theme-name", m_iconTheme);
    gtk4Settings.setValue("gtk-cursor-theme-name", m_cursorTheme);
    gtk4Settings.setValue("gtk-cursor-theme-size", m_cursorSize);
    gtk4Settings.setValue("gtk-enable-animations", true);
    gtk4Settings.setValue("gtk-decoration-layout", "close,minimize,maximize:");
    gtk4Settings.setValue("gtk-theme-name", isDarkMode() ? "Cutefish-dark" : "Cutefish-light");
    gtk4Settings.endGroup();
    gtk4Settings.sync();

    // GTK 3 & GTK 4 Libadwaita CSS Overrides & Traffic Light Styling
    const QString gtkCssContent = QStringLiteral(
        "@define-color accent_color #007AFF;\n"
        "@define-color accent_bg_color #007AFF;\n"
        "@define-color window_bg_color %1;\n"
        "@define-color window_fg_color %2;\n"
        "@define-color headerbar_bg_color %1;\n"
        "@define-color headerbar_fg_color %2;\n"
        "\n"
        "/* macOS Traffic Light Buttons for GTK3, GTK4 & Libadwaita */\n"
        "headerbar windowcontrols {\n"
        "    margin-left: 6px;\n"
        "}\n"
        "headerbar button.titlebutton.close,\n"
        "headerbar windowcontrols button.close,\n"
        "windowcontrols button.close,\n"
        "windowcontrols button.close:backdrop {\n"
        "    background-color: #FF5F56 !important;\n"
        "    border: 1px solid #E0443E !important;\n"
        "    border-radius: 50% !important;\n"
        "    min-width: 13px !important;\n"
        "    min-height: 13px !important;\n"
        "    max-width: 13px !important;\n"
        "    max-height: 13px !important;\n"
        "    padding: 0 !important;\n"
        "    margin: 0 3px !important;\n"
        "    color: transparent !important;\n"
        "}\n"
        "headerbar button.titlebutton.minimize,\n"
        "headerbar windowcontrols button.minimize,\n"
        "windowcontrols button.minimize,\n"
        "windowcontrols button.minimize:backdrop {\n"
        "    background-color: #FFBD2E !important;\n"
        "    border: 1px solid #DEA123 !important;\n"
        "    border-radius: 50% !important;\n"
        "    min-width: 13px !important;\n"
        "    min-height: 13px !important;\n"
        "    max-width: 13px !important;\n"
        "    max-height: 13px !important;\n"
        "    padding: 0 !important;\n"
        "    margin: 0 3px !important;\n"
        "    color: transparent !important;\n"
        "}\n"
        "headerbar button.titlebutton.maximize,\n"
        "headerbar windowcontrols button.maximize,\n"
        "windowcontrols button.maximize,\n"
        "windowcontrols button.maximize:backdrop {\n"
        "    background-color: #27C93F !important;\n"
        "    border: 1px solid #1AAB29 !important;\n"
        "    border-radius: 50% !important;\n"
        "    min-width: 13px !important;\n"
        "    min-height: 13px !important;\n"
        "    max-width: 13px !important;\n"
        "    max-height: 13px !important;\n"
        "    padding: 0 !important;\n"
        "    margin: 0 3px !important;\n"
        "    color: transparent !important;\n"
        "}\n"
        "headerbar button.titlebutton image,\n"
        "headerbar button.titlebutton label,\n"
        "windowcontrols button image,\n"
        "windowcontrols button label {\n"
        "    opacity: 0 !important;\n"
        "}\n"
        "headerbar button.titlebutton:hover image,\n"
        "headerbar button.titlebutton:hover label,\n"
        "windowcontrols button:hover image,\n"
        "windowcontrols button:hover label {\n"
        "    opacity: 0.8 !important;\n"
        "}\n"
    ).arg(isDarkMode() ? QStringLiteral("#1C1C1E") : QStringLiteral("#F2F2F7"),
          isDarkMode() ? QStringLiteral("#FFFFFF") : QStringLiteral("#1C1C1E"));

    for (const QString &configDir : {QStringLiteral("/gtk-3.0"), QStringLiteral("/gtk-4.0")}) {
        const QString cssPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + configDir + QStringLiteral("/gtk.css");
        QDir().mkpath(QFileInfo(cssPath).absolutePath());
        QFile cssFile(cssPath);
        if (cssFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&cssFile);
            out << gtkCssContent;
            cssFile.close();
        }
    }

    // Default Cursor Theme Fallback (~/.icons/default/index.theme)
    const QString defaultIconDir = QDir::homePath() + QStringLiteral("/.icons/default");
    QDir().mkpath(defaultIconDir);
    QFile indexTheme(defaultIconDir + QStringLiteral("/index.theme"));
    if (indexTheme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&indexTheme);
        out << "[Icon Theme]\n";
        out << "Name=Default\n";
        out << "Comment=Default Cursor Theme\n";
        out << "Inherits=" << m_cursorTheme << "\n";
        indexTheme.close();
    }

    // GTK 2 (~/.gtkrc-2.0)
    const QString gtk2Path = gtkRc2Path();
    QFile gtk2File(gtk2Path);
    if (gtk2File.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&gtk2File);
        out << "include \"/usr/share/themes/" << (isDarkMode() ? "Cutefish-dark" : "Cutefish-light") << "/gtk-2.0/gtkrc\"\n";
        out << "gtk-theme-name=\"" << (isDarkMode() ? "Cutefish-dark" : "Cutefish-light") << "\"\n";
        out << "gtk-icon-theme-name=\"" << m_iconTheme << "\"\n";
        out << "gtk-font-name=\"" << systemFont() << " " << systemFontPointSize() << "\"\n";
        gtk2File.close();
    }
}

void ThemeManager::updateDesktopColorScheme()
{
    // 1. KDE / KWin Configuration (~/.config/kdeglobals & ~/.config/kwinrc)
    QSettings kwinSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                               + QStringLiteral("/kwinrc"),
                           QSettings::IniFormat);
    kwinSettings.beginGroup(QStringLiteral("org.kde.kdecoration2"));
    kwinSettings.setValue(QStringLiteral("ButtonsOnLeft"), QStringLiteral("XIA"));
    kwinSettings.setValue(QStringLiteral("ButtonsOnRight"), QString());
    kwinSettings.setValue(QStringLiteral("library"), QStringLiteral("org.cutefish.decoration"));
    kwinSettings.endGroup();

    kwinSettings.beginGroup(QStringLiteral("Touchpad"));
    kwinSettings.setValue(QStringLiteral("TapToClick"), true);
    kwinSettings.setValue(QStringLiteral("NaturalScroll"), true);
    kwinSettings.setValue(QStringLiteral("PointerAcceleration"), 0.2);
    kwinSettings.endGroup();

    kwinSettings.beginGroup(QStringLiteral("Gestures"));
    kwinSettings.setValue(QStringLiteral("Enabled"), true);
    kwinSettings.endGroup();
    kwinSettings.sync();

    // Configure Touchpad Gestures and Tiling Shortcuts in kglobalshortcutsrc
    QSettings shortcutSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/kglobalshortcutsrc"),
                                QSettings::IniFormat);
    shortcutSettings.beginGroup(QStringLiteral("kwin"));
    shortcutSettings.setValue(QStringLiteral("Window Quick Tile Left"), QStringLiteral("Meta+Left,Meta+Left,Quick Tile Window to the Left"));
    shortcutSettings.setValue(QStringLiteral("Window Quick Tile Right"), QStringLiteral("Meta+Right,Meta+Right,Quick Tile Window to the Right"));
    shortcutSettings.setValue(QStringLiteral("Window Absolute Maximize"), QStringLiteral("Meta+Up,Meta+Up,Maximize"));
    shortcutSettings.setValue(QStringLiteral("Window Minimize"), QStringLiteral("Meta+Down,Meta+Down,Minimize Window"));
    shortcutSettings.endGroup();

    shortcutSettings.beginGroup(QStringLiteral("org.kde.KWin.Gestures"));
    shortcutSettings.setValue(QStringLiteral("SwipeUp3"), QStringLiteral("qdbus com.cutefish.Launcher /Launcher com.cutefish.Launcher.toggle"));
    shortcutSettings.setValue(QStringLiteral("SwipeDown3"), QStringLiteral("qdbus com.cutefish.Overview /Overview com.cutefish.Overview.toggle"));
    shortcutSettings.setValue(QStringLiteral("SwipeLeft3"), QStringLiteral("Switch to Next Desktop"));
    shortcutSettings.setValue(QStringLiteral("SwipeRight3"), QStringLiteral("Switch to Previous Desktop"));
    shortcutSettings.setValue(QStringLiteral("SwipeUp4"), QStringLiteral("qdbus com.cutefish.Overview /Overview com.cutefish.Overview.toggle"));
    shortcutSettings.setValue(QStringLiteral("SwipeDown4"), QStringLiteral("qdbus com.cutefish.Launcher /Launcher com.cutefish.Launcher.toggle"));
    shortcutSettings.endGroup();
    shortcutSettings.sync();

    // Configure libinput-gestures (~/.config/libinput-gestures.conf)
    const QString libinputGesturesPath = QDir::homePath() + QStringLiteral("/.config/libinput-gestures.conf");
    QFile gestureFile(libinputGesturesPath);
    if (gestureFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&gestureFile);
        out << "gesture swipe up 3 qdbus com.cutefish.Launcher /Launcher com.cutefish.Launcher.toggle\n";
        out << "gesture swipe down 3 qdbus com.cutefish.Overview /Overview com.cutefish.Overview.toggle\n";
        out << "gesture swipe left 3 qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.next\n";
        out << "gesture swipe right 3 qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.previous\n";
        out << "gesture swipe up 4 qdbus com.cutefish.Overview /Overview com.cutefish.Overview.toggle\n";
        out << "gesture swipe down 4 qdbus com.cutefish.Launcher /Launcher com.cutefish.Launcher.toggle\n";
        gestureFile.close();
    }

    QProcess::startDetached(QStringLiteral("libinput-gestures-setup"), {QStringLiteral("restart")});

    QSettings kdeSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                              + QStringLiteral("/kdeglobals"),
                          QSettings::IniFormat);
    kdeSettings.beginGroup(QStringLiteral("General"));
    kdeSettings.setValue(QStringLiteral("ColorScheme"), isDarkMode()
                             ? QStringLiteral("BreezeDark")
                             : QStringLiteral("Breeze"));
    kdeSettings.endGroup();

    kdeSettings.beginGroup(QStringLiteral("Icons"));
    kdeSettings.setValue(QStringLiteral("Theme"), m_iconTheme);
    kdeSettings.endGroup();

    kdeSettings.beginGroup(QStringLiteral("KDE"));
    kdeSettings.setValue(QStringLiteral("widgetStyle"), QStringLiteral("Breeze"));
    kdeSettings.endGroup();
    kdeSettings.sync();

    // Broadcast KDE Palette & Style Change Signals
    QDBusMessage paletteChanged = QDBusMessage::createSignal(
        QStringLiteral("/KGlobalSettings"),
        QStringLiteral("org.kde.KGlobalSettings"),
        QStringLiteral("notifyChange"));
    paletteChanged << 0 << 0; // PaletteChanged
    QDBusConnection::sessionBus().send(paletteChanged);

    QDBusMessage styleChanged = QDBusMessage::createSignal(
        QStringLiteral("/KGlobalSettings"),
        QStringLiteral("org.kde.KGlobalSettings"),
        QStringLiteral("notifyChange"));
    styleChanged << 1 << 0; // StyleChanged
    QDBusConnection::sessionBus().send(styleChanged);

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"), QDBusConnection::sessionBus());
    if (kwin.isValid())
        kwin.asyncCall(QStringLiteral("reconfigure"));

    // 2. GNOME / GTK Desktop GSettings
    const QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    if (!gsettings.isEmpty()) {
        const QString schema = QStringLiteral("org.gnome.desktop.interface");
        QProcess::execute(gsettings, {QStringLiteral("set"), schema, QStringLiteral("color-scheme"),
                                      isDarkMode() ? QStringLiteral("prefer-dark") : QStringLiteral("prefer-light")});
        QProcess::execute(gsettings, {QStringLiteral("set"), schema, QStringLiteral("gtk-theme"),
                                      isDarkMode() ? QStringLiteral("Cutefish-dark") : QStringLiteral("Cutefish-light")});
        QProcess::execute(gsettings, {QStringLiteral("set"), schema, QStringLiteral("icon-theme"), m_iconTheme});
        QProcess::execute(gsettings, {QStringLiteral("set"), schema, QStringLiteral("gtk-decoration-layout"),
                                      QStringLiteral("close,minimize,maximize:")});
    }
}

void ThemeManager::applyFontSettings()
{
    m_settings->sync();
    updateFontConfig();
    updateGtk3Config();
}

void ThemeManager::applyCursorSettings()
{
    m_settings->sync();

    QSettings inputSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                + QStringLiteral("/kcminputrc"),
                            QSettings::IniFormat);
    inputSettings.beginGroup(QStringLiteral("Mouse"));
    inputSettings.setValue(QStringLiteral("cursorTheme"), cursorTheme());
    inputSettings.setValue(QStringLiteral("cursorSize"), cursorSize());
    inputSettings.endGroup();
    inputSettings.sync();

    updateGtk3Config();

    QDBusMessage message = QDBusMessage::createSignal("/KGlobalSettings",
                                                      "org.kde.KGlobalSettings",
                                                      "notifyChange");
    message << 5;
    message << 0;
    QDBusConnection::sessionBus().send(message);

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"), QDBusConnection::sessionBus());
    if (kwin.isValid())
        kwin.asyncCall(QStringLiteral("reconfigure"));
}

void ThemeManager::updateFontConfig()
{
    QFontDatabase database;
    const QStringList families = database.families();
    const QString familyFont = families.contains(systemFont())
            ? systemFont()
            : families.contains(QStringLiteral("Noto Sans"))
                ? QStringLiteral("Noto Sans")
                : QStringLiteral("DejaVu Sans");
    const QString fixedFont = families.contains(systemFixedFont())
            ? systemFixedFont()
            : families.contains(QStringLiteral("Noto Sans Mono"))
                ? QStringLiteral("Noto Sans Mono")
                : QStringLiteral("DejaVu Sans Mono");
    const QString familyFallback = familyFont;

    QSettings settings(QSettings::UserScope, "cutefishos", "theme");
    bool hinting = settings.value("FontAntialias", true).toBool();
    QString hintStyle = settings.value("FontHintStyle", "hintslight").toString();

    QString content = QString("<?xml version=\"1.0\"?>"
                        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">"
                        "<fontconfig>"
                        "<match target=\"pattern\">"
                        "<test qual=\"any\" name=\"family\">"
                        "<string>serif</string>"
                        "</test>"
                        "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                        "<string>%1</string>"
                        "<string>%2</string>"
                        "</edit>"
                        "</match>"
                        "<match target=\"pattern\">"
                        "<test qual=\"any\" name=\"family\">"
                        "<string>sans-serif</string>"
                        "</test>"
                        "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                        "<string>%3</string>"
                        "<string>%4</string>"
                        "</edit>"
                        "</match>"
                        "<match target=\"pattern\">"
                        "<test qual=\"any\" name=\"family\">"
                        "<string>monospace</string>"
                        "</test>"
                        "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                        "<string>%5</string>"
                        "<string>%6</string>"
                        "<string>%7</string>"
                        "</edit>"
                        "</match>"
                        "<match target=\"font\">"
                        "<edit name=\"rgba\" mode=\"assign\"><const>rgb</const></edit>"
                        "</match>"
                        "<match target=\"font\">"
                        "<edit name=\"hinting\" mode=\"assign\">"
                        "<bool>%8</bool>"
                        "</edit>"
                        "</match>"
                        "<match target=\"font\">"
                        "<edit name=\"hintstyle\" mode=\"assign\">"
                        "<const>%9</const>"
                        "</edit>"
                        "</match>"
                        "</fontconfig>"
    ).arg(familyFont).arg(familyFallback)
     .arg(familyFont).arg(familyFallback)
     .arg(fixedFont).arg(fixedFont)
     .arg(familyFont).arg(hinting ? "true" : "false")
     .arg(hintStyle);

    QString targetPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/') + QLatin1String("fontconfig"));

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    targetPath += "/conf.d";

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    QString xmlOut;
    QXmlStreamReader reader(content);
    QXmlStreamWriter writer(&xmlOut);
    writer.setAutoFormatting(true);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isWhitespace()) {
            writer.writeCurrentToken(reader);
        }
    }

    QFile file(targetPath + "/99-cutefish.conf");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream s(&file);
        s << xmlOut.toLatin1();
        file.close();
    }
}

QString ThemeManager::iconTheme() const
{
    return m_iconTheme;
}

void ThemeManager::setIconTheme(const QString &iconTheme)
{
    if (iconTheme.isEmpty())
        return;

    m_iconTheme = iconTheme;
    m_settings->setValue("IconTheme", m_iconTheme);
    QIcon::setThemeName(m_iconTheme);

    // 1. Update GTK 2, 3, 4 configs
    updateGtk3Config();

    // 2. Update KDE / Qt config (~/.config/kdeglobals [Icons] Theme)
    QSettings kdeSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                              + QStringLiteral("/kdeglobals"),
                          QSettings::IniFormat);
    kdeSettings.beginGroup(QStringLiteral("Icons"));
    kdeSettings.setValue(QStringLiteral("Theme"), m_iconTheme);
    kdeSettings.endGroup();
    kdeSettings.sync();

    // 3. Update GNOME / GTK via gsettings
    const QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    if (!gsettings.isEmpty()) {
        QProcess::execute(gsettings, {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.interface"),
                                      QStringLiteral("icon-theme"), m_iconTheme});
    }

    // 4. Send DBus notification to all KDE/Qt apps
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KGlobalSettings"),
                                                      QStringLiteral("org.kde.KGlobalSettings"),
                                                      QStringLiteral("notifyChange"));
    message << 0;
    message << 0;
    QDBusConnection::sessionBus().send(message);

    // 5. Notify Cutefish Launcher & Dock to clear icon cache & refresh icons
    QDBusInterface launcher(QStringLiteral("com.cutefish.Launcher"), QStringLiteral("/Launcher"),
                            QStringLiteral("com.cutefish.Launcher"), QDBusConnection::sessionBus());
    if (launcher.isValid()) {
        launcher.asyncCall(QStringLiteral("clearPixmapCache"));
    }

    QDBusInterface dock(QStringLiteral("com.cutefish.Dock"), QStringLiteral("/Dock"),
                        QStringLiteral("com.cutefish.Dock"), QDBusConnection::sessionBus());
    if (dock.isValid()) {
        dock.asyncCall(QStringLiteral("clearPixmapCache"));
    }

    emit iconThemeChanged();
}
