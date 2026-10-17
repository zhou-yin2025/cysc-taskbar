#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

// ============================================================================
//  DatabaseManager.h
//  职责：SQLite 连接与建表（每实例独立连接名、旧库自动补列）
//  依赖：models/Task.h
// ============================================================================

#include <QSqlDatabase>
#include <QString>
#include "src/models/Task.h"
#include "../ds/Vector.h"

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    // Non-copyable: the SQLite connection name is the identity of an instance.
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;

    // Task CRUD
    bool addTask(const Task& task);
    bool updateTask(const Task& task);
    bool deleteTask(int id);
    Vector<Task> loadAllTasks();

    // Get next id
    int getNextId();

private:
    // Unique per instance, so a second DatabaseManager (tests, reloads) does not
    // silently reuse - and then destroy - the first one's connection.
    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_opened = false;

    bool createTables();
};

#endif
