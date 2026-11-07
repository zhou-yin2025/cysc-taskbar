#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

namespace {

// 登记项的名字。Windows 上是 Run 键下的值名，macOS 上是 LaunchAgent 的 Label。
const char *kEntryName = "CYSC TaskBar";
const char *kAgentLabel = "com.cysc.taskbar";

QString g_storageOverride;

// 可执行文件的完整路径，带引号 —— 路径里有空格（本机就是
// "deepseek workplace"）时不加引号会被当成分隔符，开机时启动失败。
QString quotedExecutable() {
    QString path = QCoreApplication::applicationFilePath();
    path.replace(QLatin1Char('/'), QDir::separator());
    return QStringLiteral("\"%1\"").arg(path);
}

#ifdef Q_OS_WIN
// Windows 的自启登记在 HKCU\...\Run 下，不需要管理员权限。
const char *kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

QSettings *makeSettings() {
    if (!g_storageOverride.isEmpty()) {
        // 测试模式：用 INI 文件代替注册表，避免污染用户真实的 Run 键
        return new QSettings(g_storageOverride, QSettings::IniFormat);
    }
    return new QSettings(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
}
#endif

#ifdef Q_OS_MAC
QString launchAgentPath() {
    if (!g_storageOverride.isEmpty()) return g_storageOverride;
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + kAgentLabel
           + QStringLiteral(".plist");
}

QString plistContent() {
    // 最小可用的 LaunchAgent：登录时启动一次，不保活（程序自己会常驻托盘）。
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n"
               "<dict>\n"
               "  <key>Label</key><string>%1</string>\n"
               "  <key>ProgramArguments</key>\n"
               "  <array><string>%2</string></array>\n"
               "  <key>RunAtLoad</key><true/>\n"
               "</dict>\n"
               "</plist>\n")
        .arg(QString::fromLatin1(kAgentLabel), QCoreApplication::applicationFilePath());
}
#endif

} // namespace

namespace Autostart {

bool isSupported() {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    return true;
#else
    return false;
#endif
}

QString commandLine() {
    return quotedExecutable();
}

void setStorageOverrideForTests(const QString &path) {
    g_storageOverride = path;
}

QString storageOverrideForTests() {
    return g_storageOverride;
}

bool isEnabled() {
#ifdef Q_OS_WIN
    QScopedPointer<QSettings> settings(makeSettings());
    // 只认「登记项存在」；值内容可能是旧路径（程序被移动过），仍然算已登记 ——
    // 用户看到的是"开机启动"这个开关，不是路径对不对。
    return settings->contains(QString::fromLatin1(kEntryName));
#elif defined(Q_OS_MAC)
    return QFileInfo::exists(launchAgentPath());
#else
    return false;
#endif
}

bool setEnabled(bool on) {
    if (!isSupported()) return false;

#ifdef Q_OS_WIN
    QScopedPointer<QSettings> settings(makeSettings());
    if (on) {
        settings->setValue(QString::fromLatin1(kEntryName), quotedExecutable());
    } else {
        settings->remove(QString::fromLatin1(kEntryName));
    }
    settings->sync();
    // 写完立刻回读：权限不足时 setValue 不报错，只有 status() 会说明真相。
    const bool ok = settings->status() == QSettings::NoError;
    return ok && (isEnabled() == on);
#elif defined(Q_OS_MAC)
    const QString path = launchAgentPath();
    if (on) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return false;
        }
        file.write(plistContent().toUtf8());
        file.close();
        return QFileInfo::exists(path);
    }
    if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
    return !QFileInfo::exists(path);
#else
    Q_UNUSED(on);
    return false;
#endif
}

} // namespace Autostart
