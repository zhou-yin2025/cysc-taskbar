#ifndef STORAGEPATHS_H
#define STORAGEPATHS_H

// ============================================================================
//  StoragePaths.h
//  职责：数据库位置探测（显式覆盖优先、可写性探测）
//  依赖：无
// ============================================================================

#include <QString>

// Works out which file the task database should live in, and makes sure the
// whole chain (directory + file) is actually writable. Ported from the V0.0.1
// "put it next to the executable" behaviour, which breaks as soon as the app is
// installed somewhere read-only.
namespace StoragePaths {

// Candidate locations, best first:
//   1. <app dir>/data/cysc.db      - portable / development layout
//   2. <AppData>/cysc.db           - the normal per-user location
//   3. <system temp>/CYSC/cysc.db  - last resort
QStringList databaseCandidates();

// Returns the first candidate whose directory can really be created and written
// to (verified with a probe file), or an empty string when none works.
QString writableDatabasePath();

// True when `dbPath` is an existing regular file whose directory accepts writes,
// i.e. SQLite will be able to create and update it in place.
bool isDatabaseWritable(const QString& dbPath);

} // namespace StoragePaths

#endif
