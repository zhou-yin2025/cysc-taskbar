#include "StoragePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {

// Creates `dir` if needed, then proves it is writable by creating and removing a
// probe file. A directory can exist and still be unwritable (this sandbox denies
// writes to %APPDATA% and %TEMP%), and only an actual write settles it.
bool directoryAcceptsWrites(const QString& dir) {
    if (dir.isEmpty()) return false;
    if (!QDir().mkpath(dir)) {
        // mkpath also fails when the directory already exists but is not
        // accessible; keep going so the probe below can report the truth.
        if (!QFileInfo(dir).isDir()) return false;
    }

    QFile probe(QDir(dir).absoluteFilePath(".cysc_write_probe"));
    if (!probe.open(QIODevice::WriteOnly)) {
        return false;
    }
    probe.write("ok");
    probe.close();
    probe.remove();
    return true;
}

} // namespace

namespace StoragePaths {

QStringList databaseCandidates() {
    QStringList candidates;

    // 1) an explicit override always wins: tests, portable runs, and anyone who
    //    wants the database somewhere specific must not be second-guessed.
    const QString override = qEnvironmentVariable("CYSC_DB_PATH");
    if (!override.isEmpty()) {
        candidates << override;
    }

    // 2) beside the executable (portable layout, and where V0.0.1 stored it)
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << appDir + "/data/cysc.db";
    }

    // 3) the standard per-user location
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        candidates << appData + "/cysc.db";
    }

    // 4) last resort
    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!temp.isEmpty()) {
        candidates << temp + "/CYSC/cysc.db";
    }

    candidates.removeDuplicates();
    return candidates;
}

bool isDatabaseWritable(const QString& dbPath) {
    if (dbPath.isEmpty() || !QFileInfo(dbPath).isFile()) {
        return false;
    }
    return directoryAcceptsWrites(QFileInfo(dbPath).absolutePath());
}

QString writableDatabasePath() {
    const QStringList candidates = databaseCandidates();
    for (const QString& candidate : candidates) {
        const QString dir = QFileInfo(candidate).absolutePath();
        if (!directoryAcceptsWrites(dir)) {
            continue;
        }
        // Prefer an existing database, otherwise this is a usable new location.
        if (QFileInfo(candidate).isFile() || !QFileInfo(candidate).exists()) {
            return candidate;
        }
    }
    return QString();
}

} // namespace StoragePaths
