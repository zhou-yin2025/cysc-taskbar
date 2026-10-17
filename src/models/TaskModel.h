#ifndef TASKMODEL_H
#define TASKMODEL_H

// ============================================================================
//  TaskModel.h
//  职责：V0.0.3 旧模型（保留供旧库读取与历史参考）
//  依赖：ds/*, core/DatabaseManager.h
// ============================================================================

#include "Task.h"
#include "TaskQuery.h"
#include "../ds/Vector.h"
#include "../ds/Stack.h"
#include "../ds/Trie.h"
#include "src/core/DatabaseManager.h"
#include <QVector>
#include <string>

class TaskModel {
public:
    TaskModel();

    bool initDatabase(const std::string& dbPath);
    bool isDatabaseReady() const { return m_dbReady; }
    std::string lastError() const { return m_lastError; }

    // Task management. Every mutation mirrors into SQLite when a database is
    // open; if the database write fails the in-memory change is rolled back so
    // the two never silently disagree.
    int addTask(const Task& task);          // returns id, or -1 on failure
    bool updateTask(int id, const Task& task);
    bool updateDetails(int id, const std::string& details);
    bool deleteTask(int id);
    bool completeTask(int id);
    bool changeStatus(int id, int status);

    // Query
    Task* findTask(int id);
    Vector<Task>& allTasks();
    Vector<Task> getTasksByDate(const std::string& date);
    Vector<Task> getTasksByCategory(int categoryId);

    // Filtering + sorting in one pass (drives the list view).
    Vector<Task> query(const TaskQuery& q) const;

    // Search
    Vector<Task> searchByPrefix(const std::string& prefix) const;
    Vector<Task> searchByKeyword(const std::string& keyword) const;

    // Sort in place
    void sortByDeadline();
    void sortByPriority();

    // Undo the most recent mutation
    bool undo();

    // Reminder bookkeeping used by the tray notifier.
    int countByStatus(int status) const;

private:
    Vector<Task> tasks;
    Trie titleTrie;
    Stack<Task> undoStack;
    // Parallel to undoStack: true when that entry came from addTask(), meaning
    // undo() must remove the task rather than re-apply its values. Kept in
    // lock-step with the stack so ids cannot go stale after an undo + re-add.
    QVector<bool> undoEntryIsInsert;
    int nextId = 0;
    DatabaseManager m_db;
    bool m_dbReady = false;
    std::string m_lastError;

    void rebuildTrie();
    int indexOf(int id) const;
    void pushUndo(const Task& t, bool fromInsert = false);
};

#endif
