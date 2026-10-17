#include "DocumentStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

namespace {

int nextStoreSerial() {
    static int serial = 0;
    return ++serial;
}

// 标签分隔符：Unit Separator。用普通逗号会在标签本身含逗号时出错。
const QChar kTagSeparator = QChar(0x1F);

// 迁移用的新列（列已存在时 ALTER 会失败，这是正常路径）。
const char* kAlterColumns[] = {
    "ALTER TABLE tasks ADD COLUMN list_id INTEGER DEFAULT 1",
    "ALTER TABLE tasks ADD COLUMN starred INTEGER DEFAULT 0",
    "ALTER TABLE tasks ADD COLUMN in_my_day INTEGER DEFAULT 0",
    "ALTER TABLE tasks ADD COLUMN reminder TEXT DEFAULT ''",
    "ALTER TABLE tasks ADD COLUMN recurrence INTEGER DEFAULT 0",
    "ALTER TABLE tasks ADD COLUMN tags TEXT DEFAULT ''",
    "ALTER TABLE tasks ADD COLUMN manual_order INTEGER DEFAULT 0",
};

} // namespace

DocumentStore::DocumentStore() {}

DocumentStore::~DocumentStore() {
    close();
}

bool DocumentStore::open(const QString& dbPath) {
    if (m_opened) close();
    m_lastError.clear();

    QFileInfo fi(dbPath);
    QDir().mkpath(fi.absolutePath());

    m_connectionName = QString("cysc_doc_%1").arg(nextStoreSerial());

    bool openedOk = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        db.setDatabaseName(dbPath);
        openedOk = db.open();
        if (!openedOk) {
            m_lastError = db.lastError().text();
            qWarning() << "DocumentStore: cannot open" << dbPath << m_lastError;
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
    if (!createSchema()) {
        close();
        return false;
    }
    return true;
}

void DocumentStore::close() {
    if (!m_opened && m_connectionName.isEmpty()) return;
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
    m_opened = false;
}

bool DocumentStore::isOpen() const {
    return m_opened && m_db.isOpen();
}

bool DocumentStore::createSchema() {
    QSqlQuery q(m_db);
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "id INTEGER PRIMARY KEY,"
            "title TEXT NOT NULL,"
            "details TEXT DEFAULT '',"
            "priority INTEGER DEFAULT 1,"
            "deadline TEXT,"
            "category_id INTEGER DEFAULT 0,"
            "status INTEGER DEFAULT 0,"
            "created_at TEXT,"
            "completed_at TEXT,"
            "list_id INTEGER DEFAULT 1,"
            "starred INTEGER DEFAULT 0,"
            "in_my_day INTEGER DEFAULT 0,"
            "reminder TEXT DEFAULT '',"
            "recurrence INTEGER DEFAULT 0,"
            "tags TEXT DEFAULT '',"
            "manual_order INTEGER DEFAULT 0"
            ")")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: create tasks failed:" << m_lastError;
        return false;
    }

    for (const char* sql : kAlterColumns) {
        QSqlQuery a(m_db);
        if (!a.exec(QString::fromLatin1(sql))) {
            const QString error = a.lastError().text();
            if (!error.contains("duplicate column", Qt::CaseInsensitive)) {
                qWarning() << "DocumentStore: ALTER TABLE tasks:" << error;
            }
        }
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS lists ("
            "id INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "color TEXT DEFAULT '#3D6BFF',"
            "group_id INTEGER DEFAULT -1,"
            "created_at TEXT,"
            "is_default INTEGER DEFAULT 0"
            ")")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: create lists failed:" << m_lastError;
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS subtasks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "task_id INTEGER NOT NULL,"
            "position INTEGER DEFAULT 0,"
            "title TEXT NOT NULL,"
            "done INTEGER DEFAULT 0"
            ")")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: create subtasks failed:" << m_lastError;
        return false;
    }

    return true;
}

// ------------------------------------------------------------------ 标签

QString DocumentStore::encodeTags(const std::vector<std::string>& tags) {
    QString out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) out.append(kTagSeparator);
        out.append(QString::fromStdString(tags[i]));
    }
    return out;
}

std::vector<std::string> DocumentStore::decodeTags(const QString& text) {
    std::vector<std::string> out;
    if (text.isEmpty()) return out;
    const QStringList parts = text.split(kTagSeparator, Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) out.push_back(trimmed.toStdString());
    }
    return out;
}

// ------------------------------------------------------------------ 写入

bool DocumentStore::saveDocument(const TaskDocument& doc) {
    if (!isOpen()) {
        m_lastError = "database not open";
        return false;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    // 先清空再写入：整篇覆盖，避免残留旧数据。
    QSqlQuery clear(m_db);
    clear.exec("DELETE FROM subtasks");
    clear.exec("DELETE FROM tasks");
    clear.exec("DELETE FROM lists");

    if (!saveLists(doc) || !saveTasks(doc) || !saveSubtasks(doc)) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool DocumentStore::saveLists(const TaskDocument& doc) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO lists (id, name, color, group_id, created_at, is_default) "
              "VALUES (?, ?, ?, ?, ?, ?)");
    for (const TaskList& list : doc.lists()) {
        q.addBindValue(list.id);
        q.addBindValue(QString::fromStdString(list.name));
        q.addBindValue(QString::fromStdString(list.color));
        q.addBindValue(list.groupId);
        q.addBindValue(QString::fromStdString(list.createdAt));
        q.addBindValue(list.isDefault ? 1 : 0);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            qWarning() << "DocumentStore: saveLists failed:" << m_lastError;
            return false;
        }
    }
    return true;
}

bool DocumentStore::saveTasks(const TaskDocument& doc) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO tasks (id, title, details, priority, deadline, category_id, status, "
              "created_at, completed_at, list_id, starred, in_my_day, reminder, recurrence, tags, "
              "manual_order) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const Task& t : doc.tasks()) {
        q.addBindValue(t.id);
        q.addBindValue(QString::fromStdString(t.title));
        q.addBindValue(QString::fromStdString(t.details));
        q.addBindValue(t.priority);
        q.addBindValue(QString::fromStdString(t.deadline));
        q.addBindValue(t.categoryId);
        q.addBindValue(t.status);
        q.addBindValue(QString::fromStdString(t.createdAt));
        q.addBindValue(QString::fromStdString(t.completedAt));
        q.addBindValue(t.listId);
        q.addBindValue(t.starred ? 1 : 0);
        q.addBindValue(t.inMyDay ? 1 : 0);
        q.addBindValue(QString::fromStdString(t.reminder));
        q.addBindValue(static_cast<int>(t.recurrence));
        q.addBindValue(encodeTags(t.tags));
        q.addBindValue(t.manualOrder);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            qWarning() << "DocumentStore: saveTasks failed:" << m_lastError;
            return false;
        }
    }
    return true;
}

bool DocumentStore::saveSubtasks(const TaskDocument& doc) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO subtasks (task_id, position, title, done) VALUES (?, ?, ?, ?)");
    for (const Task& t : doc.tasks()) {
        for (std::size_t i = 0; i < t.subtasks.size(); ++i) {
            const Subtask& s = t.subtasks[i];
            q.addBindValue(t.id);
            q.addBindValue(static_cast<int>(i));
            q.addBindValue(QString::fromStdString(s.title));
            q.addBindValue(s.done ? 1 : 0);
            if (!q.exec()) {
                m_lastError = q.lastError().text();
                qWarning() << "DocumentStore: saveSubtasks failed:" << m_lastError;
                return false;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ 读取

bool DocumentStore::loadDocument(TaskDocument& doc) {
    if (!isOpen()) {
        m_lastError = "database not open";
        return false;
    }

    doc = TaskDocument();          // 重置成「只有一个默认列表」的干净状态
    if (!loadLists(doc)) return false;
    if (!loadTasks(doc)) return false;
    if (!loadSubtasks(doc)) return false;

    // 重建索引（标签哈希、标题 Trie、线段树）。
    doc.rebuildIndexes();
    // 分组关系按读到的 groupId 恢复，再按并查集结果把 groupId 规范化回去。
    doc.rebuildGroupsFromListIds();
    doc.syncGroupIds();
    return true;
}

bool DocumentStore::loadLists(TaskDocument& doc) {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, name, color, group_id, created_at, is_default FROM lists ORDER BY id")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: loadLists failed:" << m_lastError;
        return false;
    }

    std::vector<TaskList> loaded;
    int maxId = 1;
    while (q.next()) {
        TaskList list;
        list.id = q.value(0).toInt();
        list.name = q.value(1).toString().toStdString();
        list.color = q.value(2).toString().toStdString();
        list.groupId = q.value(3).toInt();
        list.createdAt = q.value(4).toString().toStdString();
        list.isDefault = q.value(5).toInt() != 0;
        maxId = std::max(maxId, list.id + 1);
        loaded.push_back(list);
    }

    if (loaded.empty()) {
        // 数据库里还没有列表：保留构造函数生成的默认列表，直接成功返回。
        return true;
    }

    doc.replaceLists(loaded);
    doc.setNextIds(doc.nextTaskId(), maxId);
    return true;
}

bool DocumentStore::loadTasks(TaskDocument& doc) {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, title, details, priority, deadline, category_id, status, created_at, "
                "completed_at, list_id, starred, in_my_day, reminder, recurrence, tags, manual_order "
                "FROM tasks ORDER BY id")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: loadTasks failed:" << m_lastError;
        return false;
    }

    std::vector<Task> loaded;
    int maxId = 0;
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
        t.listId = q.value(9).toInt();
        t.starred = q.value(10).toInt() != 0;
        t.inMyDay = q.value(11).toInt() != 0;
        t.reminder = q.value(12).toString().toStdString();
        const int rec = q.value(13).toInt();
        t.recurrence = (rec >= 0 && rec <= 5) ? static_cast<Recurrence>(rec) : Recurrence::None;
        t.tags = decodeTags(q.value(14).toString());
        t.manualOrder = q.value(15).toInt();
        maxId = std::max(maxId, t.id + 1);
        loaded.push_back(t);
    }

    doc.replaceTasks(loaded);
    // 手工拖拽顺序读回来后压成连续的 0..n-1：
    // 只靠「相对次序」有意义，绝对值不必保留，规范化后便于比较与展示。
    doc.normalizeManualOrder();
    // 任务 id 必须接在最大值之后，否则新任务会覆盖旧记录。
    doc.setNextIds(maxId, doc.nextListId());
    return true;
}

bool DocumentStore::loadSubtasks(TaskDocument& doc) {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT task_id, position, title, done FROM subtasks ORDER BY task_id, position")) {
        m_lastError = q.lastError().text();
        qWarning() << "DocumentStore: loadSubtasks failed:" << m_lastError;
        return false;
    }

    while (q.next()) {
        const int taskId = q.value(0).toInt();
        Task* task = doc.findTask(taskId);
        if (!task) continue;                 // 悬空的子任务：任务已被删除，跳过
        Subtask s;
        s.id = static_cast<int>(task->subtasks.size());
        s.title = q.value(2).toString().toStdString();
        s.done = q.value(3).toInt() != 0;
        task->subtasks.push_back(s);
    }
    // 子任务的 id 按位置重新编号，保证与内存模型一致。
    for (Task& t : doc.tasks()) {
        for (std::size_t i = 0; i < t.subtasks.size(); ++i) {
            t.subtasks[i].id = static_cast<int>(i);
        }
    }
    return true;
}
