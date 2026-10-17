#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QVariant>

namespace {
// Hands out a distinct connection name per DatabaseManager instance
// (Qt keys connections by name, so reusing "QSQLITE" clashes).
int nextConnectionSerial() {
    static int serial = 0;
    return ++serial;
}
} // namespace

DatabaseManager::DatabaseManager() {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open(const QString& dbPath) {
    if (m_opened) {
        close();
    }

    // Ensure the containing directory exists.
    QFileInfo fi(dbPath);
    QDir().mkpath(fi.absolutePath());

    m_connectionName = QString("cysc_%1").arg(nextConnectionSerial());

    // Scope the QSqlDatabase handle so it is released before removeDatabase().
    bool openedOk = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        db.setDatabaseName(dbPath);
        openedOk = db.open();
        if (!openedOk) {
            qWarning() << "Failed to open database:" << db.lastError().text()
                       << "path:" << dbPath;
        } else {
            m_db = db;
        }
    }

    if (!openedOk) {
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    m_opened = true;

    if (!createTables()) {
        close();
        return false;
    }
    return true;
}

void DatabaseManager::close() {
    if (!m_opened && m_connectionName.isEmpty()) {
        return;
    }
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();          // drop our handle first...
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);   // ...then deregister it
        m_connectionName.clear();
    }
    m_opened = false;
}

bool DatabaseManager::isOpen() const {
    return m_opened && m_db.isOpen();
}

bool DatabaseManager::createTables() {
    QSqlQuery q(m_db);

    bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY,"
        "title TEXT NOT NULL,"
        "details TEXT DEFAULT '',"
        "priority INTEGER DEFAULT 1,"
        "deadline TEXT,"
        "category_id INTEGER DEFAULT 0,"
        "status INTEGER DEFAULT 0,"
        "created_at TEXT,"
        "completed_at TEXT"
        ")"
    );

    if (!ok) {
        qWarning() << "Failed to create tasks table:" << q.lastError().text();
        return false;
    }

    // Databases created before V0.0.3 have no details column. ALTER TABLE fails
    // harmlessly when the column is already there, so no version bookkeeping is
    // needed.
    QSqlQuery alter(m_db);
    if (!alter.exec("ALTER TABLE tasks ADD COLUMN details TEXT DEFAULT ''")) {
        const QString error = alter.lastError().text();
        if (!error.contains("duplicate column", Qt::CaseInsensitive)) {
            qWarning() << "ALTER TABLE tasks: " << error;
        }
    }

    // ---- V0.1.0 新增列与表 ----
    // 每一条 ALTER 都以「列已存在」告警作为正常路径，因此不需要版本号表。
    const char* kAlterColumns[] = {
        "ALTER TABLE tasks ADD COLUMN list_id INTEGER DEFAULT 1",
        "ALTER TABLE tasks ADD COLUMN starred INTEGER DEFAULT 0",
        "ALTER TABLE tasks ADD COLUMN in_my_day INTEGER DEFAULT 0",
        "ALTER TABLE tasks ADD COLUMN reminder TEXT DEFAULT ''",
        "ALTER TABLE tasks ADD COLUMN recurrence INTEGER DEFAULT 0",
        "ALTER TABLE tasks ADD COLUMN tags TEXT DEFAULT ''",
        "ALTER TABLE tasks ADD COLUMN manual_order INTEGER DEFAULT 0",
    };
    for (const char* sql : kAlterColumns) {
        QSqlQuery a(m_db);
        if (!a.exec(QString::fromLatin1(sql))) {
            const QString error = a.lastError().text();
            if (!error.contains("duplicate column", Qt::CaseInsensitive)) {
                qWarning() << "ALTER TABLE tasks:" << error;
            }
        }
    }

    QSqlQuery lists(m_db);
    if (!lists.exec(
            "CREATE TABLE IF NOT EXISTS lists ("
            "id INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "color TEXT DEFAULT '#3D6BFF',"
            "group_id INTEGER DEFAULT -1,"
            "created_at TEXT,"
            "is_default INTEGER DEFAULT 0"
            ")")) {
        qWarning() << "Failed to create lists table:" << lists.lastError().text();
        return false;
    }

    QSqlQuery steps(m_db);
    if (!steps.exec(
            "CREATE TABLE IF NOT EXISTS subtasks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "task_id INTEGER NOT NULL,"
            "position INTEGER DEFAULT 0,"
            "title TEXT NOT NULL,"
            "done INTEGER DEFAULT 0"
            ")")) {
        qWarning() << "Failed to create subtasks table:" << steps.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::addTask(const Task& task) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO tasks (id, title, details, priority, deadline, category_id, status, created_at, completed_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(task.id);
    q.addBindValue(QString::fromStdString(task.title));
    q.addBindValue(QString::fromStdString(task.details));
    q.addBindValue(task.priority);
    q.addBindValue(QString::fromStdString(task.deadline));
    q.addBindValue(task.categoryId);
    q.addBindValue(task.status);
    q.addBindValue(QString::fromStdString(task.createdAt));
    q.addBindValue(QString::fromStdString(task.completedAt));

    if (!q.exec()) {
        qWarning() << "addTask failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateTask(const Task& task) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE tasks SET title=?, details=?, priority=?, deadline=?, category_id=?, "
              "status=?, created_at=?, completed_at=? WHERE id=?");
    q.addBindValue(QString::fromStdString(task.title));
    q.addBindValue(QString::fromStdString(task.details));
    q.addBindValue(task.priority);
    q.addBindValue(QString::fromStdString(task.deadline));
    q.addBindValue(task.categoryId);
    q.addBindValue(task.status);
    q.addBindValue(QString::fromStdString(task.createdAt));
    q.addBindValue(QString::fromStdString(task.completedAt));
    q.addBindValue(task.id);

    if (!q.exec()) {
        qWarning() << "updateTask failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteTask(int id) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM tasks WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) {
        qWarning() << "deleteTask failed:" << q.lastError().text();
        return false;
    }
    return true;
}

Vector<Task> DatabaseManager::loadAllTasks() {
    Vector<Task> result;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, title, details, priority, deadline, category_id, status, created_at, completed_at "
                "FROM tasks ORDER BY id")) {
        qWarning() << "loadAllTasks failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        Task t;
        t.id = q.value(0).toInt();
        t.title = q.value(1).toString().toStdString();
        t.details = q.value(2).toString().toStdString();
        t.priority = q.value(3).toInt();
        t.deadline = q.value(4).toString().toStdString();
        t.categoryId = q.value(5).toInt();
        t.status = q.value(6).toInt();
        t.createdAt = q.value(7).toString().toStdString();
        t.completedAt = q.value(8).toString().toStdString();
        result.pushBack(t);
    }
    return result;
}

int DatabaseManager::getNextId() {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT MAX(id) FROM tasks")) {
        qWarning() << "getNextId failed:" << q.lastError().text();
        return 0;
    }
    if (q.next() && !q.value(0).isNull()) {
        return q.value(0).toInt() + 1;
    }
    return 0;
}
