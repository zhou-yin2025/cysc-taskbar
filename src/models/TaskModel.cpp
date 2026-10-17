#include "TaskModel.h"
#include "src/ds/Vector.h"
#include <QDateTime>
#include <algorithm>
#include <string>
#include <vector>

namespace {

// The scope currently used by Vector::sort. Vector::sort takes a plain function
// pointer (no functor overload), so the scope is tunnelled through here; the
// model is single-threaded, and query() restores nothing because every call
// sets it before sorting.
TaskQuery::SortScope g_activeSortScope = TaskQuery::SortCreated;

bool taskLessAscending(const Task& a, const Task& b) {
    const int primary = taskCompareInScope(a.deadline, a.id, a.status,
                                           b.deadline, b.id, b.status, g_activeSortScope);
    if (primary != 0) return primary < 0;

    // Tie-breaker so the order stays deterministic across runs.
    const TaskQuery::SortScope secondary =
        (g_activeSortScope == TaskQuery::SortCreated) ? TaskQuery::SortDeadline
                                                      : TaskQuery::SortCreated;
    return taskCompareInScope(a.deadline, a.id, a.status,
                              b.deadline, b.id, b.status, secondary) < 0;
}

// 降序时不能整体反转：那样已完成项会跑到最前面。这里只反转「同一完成状态内」
// 的比较，完成项依旧被 taskCompareInScope 压到最后。
bool taskLessDescending(const Task& a, const Task& b) {
    const bool aDone = taskIsDone(a.status);
    const bool bDone = taskIsDone(b.status);
    if (aDone != bDone) {
        return !aDone;                 // 未完成在前，已完成在后
    }
    return taskLessAscending(b, a);     // 同组内反向
}

char asciiLower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

std::string toLowerAscii(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = asciiLower(c);
    }
    return out;
}

// Case-insensitive ASCII prefix test, used by the Trie-backed searches. Titles
// are indexed in both original and lower-case form, so a lower-cased query
// prefix reaches them.
bool startsWithIgnoreCase(const std::string& text, const std::string& lowerPrefix) {
    if (text.size() < lowerPrefix.size()) return false;
    for (size_t i = 0; i < lowerPrefix.size(); ++i) {
        if (asciiLower(text[i]) != lowerPrefix[i]) return false;
    }
    return true;
}

bool taskLessInScope(const Task& a, const Task& b, TaskQuery::SortScope scope) {
    g_activeSortScope = scope;
    return taskLessAscending(a, b);
}

// 完成时间的字符串戳，格式与 deadline 一致，便于比较与展示。
std::string currentStamp() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
}

// Plain function pointers, because Vector::sort takes a function pointer.
bool taskLessByDeadline(const Task& a, const Task& b) {
    return taskLessInScope(a, b, TaskQuery::SortDeadline);
}

} // namespace

TaskModel::TaskModel() {
    nextId = 0;
}

bool TaskModel::initDatabase(const std::string& dbPath) {
    m_lastError.clear();
    if (!m_db.open(QString::fromStdString(dbPath))) {
        m_lastError = "cannot open database: " + dbPath;
        return false;
    }
    m_dbReady = true;

    // Load existing tasks
    tasks = m_db.loadAllTasks();
    nextId = m_db.getNextId();

    // Build Trie
    rebuildTrie();
    return true;
}

int TaskModel::indexOf(int id) const {
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks[i].id == id) return i;
    }
    return -1;
}

int TaskModel::addTask(const Task& task) {
    Task newTask = task;
    if (newTask.id < 0) newTask.id = nextId++;

    if (m_dbReady && !m_db.addTask(newTask)) {
        // The insert never happened, so keep memory in step with disk.
        m_lastError = "addTask: database insert failed";
        return -1;
    }

    pushUndo(newTask, /*fromInsert=*/true);
    tasks.pushBack(newTask);
    rebuildTrie();
    return newTask.id;
}

bool TaskModel::updateTask(int id, const Task& task) {
    const int idx = indexOf(id);
    if (idx < 0) return false;

    Task updated = task;
    updated.id = id;

    if (m_dbReady && !m_db.updateTask(updated)) {
        m_lastError = "updateTask: database update failed";
        return false;
    }

    pushUndo(tasks[idx]);
    tasks[idx] = updated;
    rebuildTrie();
    return true;
}

bool TaskModel::updateDetails(int id, const std::string& details) {
    const Task* current = findTask(id);
    if (!current) return false;

    Task updated = *current;
    updated.details = details;
    return updateTask(id, updated);
}

bool TaskModel::deleteTask(int id) {
    const int idx = indexOf(id);
    if (idx < 0) return false;

    if (m_dbReady && !m_db.deleteTask(id)) {
        m_lastError = "deleteTask: database delete failed";
        return false;
    }

    pushUndo(tasks[idx]);
    tasks.remove(idx);
    rebuildTrie();
    return true;
}

bool TaskModel::completeTask(int id) {
    const int idx = indexOf(id);
    if (idx < 0) return false;

    Task updated = tasks[idx];
    updated.status = 2;
    updated.completedAt = currentStamp();

    if (m_dbReady && !m_db.updateTask(updated)) {
        m_lastError = "completeTask: database update failed";
        return false;
    }

    pushUndo(tasks[idx]);
    tasks[idx] = updated;
    return true;
}

bool TaskModel::changeStatus(int id, int status) {
    const int idx = indexOf(id);
    if (idx < 0) return false;
    if (!isKnownStatus(status)) {
        m_lastError = "changeStatus: unknown status";
        return false;
    }

    Task updated = tasks[idx];
    updated.status = status;
    // 记录/清除完成时间，撤销和排序都依赖它保持一致。
    updated.completedAt = (status == 2) ? currentStamp() : std::string();

    if (m_dbReady && !m_db.updateTask(updated)) {
        m_lastError = "changeStatus: database update failed";
        return false;
    }

    pushUndo(tasks[idx]);
    tasks[idx] = updated;
    return true;
}

Task* TaskModel::findTask(int id) {
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : &tasks[idx];
}

Vector<Task>& TaskModel::allTasks() {
    return tasks;
}

Vector<Task> TaskModel::getTasksByDate(const std::string& date) {
    Vector<Task> result;
    if (date.empty()) return result;
    for (int i = 0; i < tasks.size(); ++i) {
        const std::string& deadline = tasks[i].deadline;
        if (deadline.size() >= 10 && deadline.substr(0, 10) == date) {
            result.pushBack(tasks[i]);
        }
    }
    return result;
}

Vector<Task> TaskModel::getTasksByCategory(int categoryId) {
    Vector<Task> result;
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks[i].categoryId == categoryId) {
            result.pushBack(tasks[i]);
        }
    }
    return result;
}

Vector<Task> TaskModel::query(const TaskQuery& q) const {
    const std::string needle = toLowerAscii(q.search);
    const bool useSearch = !needle.empty();

    Vector<Task> result;
    for (int i = 0; i < tasks.size(); ++i) {
        const Task& t = tasks[i];
        if (q.categoryId >= 0 && t.categoryId != q.categoryId) continue;
        if (q.hideDone && t.status == 2) continue;

        // Substring scan across title and details: a prefix-only match would
        // silently miss anything the user does not remember from the start.
        if (useSearch
            && toLowerAscii(t.title).find(needle) == std::string::npos
            && toLowerAscii(t.details).find(needle) == std::string::npos) {
            continue;
        }

        result.pushBack(t);
    }

    g_activeSortScope = q.scope;
    result.sort(q.order == TaskQuery::OrderDesc ? taskLessDescending : taskLessAscending);
    return result;
}

Vector<Task> TaskModel::searchByPrefix(const std::string& prefix) const {
    Vector<Task> result;
    if (prefix.empty()) return result;

    const std::string lowerPrefix = toLowerAscii(prefix);
    const std::vector<std::string> matched = titleTrie.getWordsWithPrefix(lowerPrefix);
    for (size_t m = 0; m < matched.size(); ++m) {
        for (int i = 0; i < tasks.size(); ++i) {
            if (!startsWithIgnoreCase(tasks[i].title, lowerPrefix)) continue;
            // The Trie holds an original-case and a lower-case copy of each
            // title, so the same task can be reached twice: de-duplicate by id.
            bool seen = false;
            for (int r = 0; r < result.size(); ++r) {
                if (result[r].id == tasks[i].id) { seen = true; break; }
            }
            if (!seen) result.pushBack(tasks[i]);
        }
    }
    return result;
}

Vector<Task> TaskModel::searchByKeyword(const std::string& keyword) const {
    Vector<Task> result;
    if (keyword.empty()) return result;

    const std::string needle = toLowerAscii(keyword);
    for (int i = 0; i < tasks.size(); ++i) {
        if (toLowerAscii(tasks[i].title).find(needle) != std::string::npos
            || toLowerAscii(tasks[i].details).find(needle) != std::string::npos) {
            result.pushBack(tasks[i]);
        }
    }
    return result;
}

void TaskModel::sortByDeadline() {
    tasks.sort(taskLessByDeadline);
}

void TaskModel::sortByPriority() {
    // Most urgent first. Uses std::sort because Vector::sort only takes a plain
    // function pointer and this comparison needs no shared scope state.
    std::sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id < b.id;
    });
}

void TaskModel::pushUndo(const Task& t, bool fromInsert) {
    undoStack.push(t);
    undoEntryIsInsert.append(fromInsert);
}

bool TaskModel::undo() {
    if (undoStack.isEmpty()) return false;

    const Task old = undoStack.pop();
    const bool wasInsert = !undoEntryIsInsert.isEmpty() && undoEntryIsInsert.takeLast();

    if (wasInsert) {
        // This entry came from addTask(), so undo means "remove it again".
        const int idx = indexOf(old.id);
        if (idx < 0) return true;      // already gone; nothing left to do

        if (m_dbReady && !m_db.deleteTask(old.id)) {
            m_lastError = "undo: database delete failed";
            pushUndo(old, /*fromInsert=*/true);
            return false;
        }
        tasks.remove(idx);
        rebuildTrie();
        return true;
    }

    const int idx = indexOf(old.id);
    if (idx >= 0) {
        // The task still exists: restore the previous field values.
        if (m_dbReady && !m_db.updateTask(old)) {
            m_lastError = "undo: database update failed";
            pushUndo(old);              // keep the entry so undo can be retried
            return false;
        }
        tasks[idx] = old;
        rebuildTrie();
        return true;
    }

    // The task was deleted: put it back.
    if (m_dbReady && !m_db.addTask(old)) {
        m_lastError = "undo: database re-insert failed";
        pushUndo(old);
        return false;
    }
    tasks.pushBack(old);
    rebuildTrie();
    if (old.id >= nextId) nextId = old.id + 1;
    return true;
}

int TaskModel::countByStatus(int status) const {
    int count = 0;
    for (int i = 0; i < tasks.size(); ++i) {
        if (status < 0 || tasks[i].status == status) ++count;
    }
    return count;
}

void TaskModel::rebuildTrie() {
    Trie newTrie;
    for (int i = 0; i < tasks.size(); ++i) {
        newTrie.insert(tasks[i].title);
        // Index the lower-cased title as well so prefix searches can be
        // case-insensitive without scanning every task.
        const std::string lower = toLowerAscii(tasks[i].title);
        if (lower != tasks[i].title) {
            newTrie.insert(lower);
        }
    }
    titleTrie = newTrie;
}
