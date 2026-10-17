#include "TaskDocument.h"

#include <QDateTime>

#include <algorithm>
#include <cctype>

namespace {

// 标题归一化：去首尾空白 + 转小写 + 折叠内部连续空白，用于「重复标题」判定。
std::string normalizeTitle(const std::string& title) {
    std::string out;
    out.reserve(title.size());
    bool lastWasSpace = false;
    for (unsigned char c : title) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!out.empty() && !lastWasSpace) {
                out.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }
        lastWasSpace = false;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

constexpr int kDefaultListId = 1;

} // namespace

TaskDocument::TaskDocument() {
    // 默认列表：对应 To Do 里的「任务」
    TaskList def;
    def.id = kDefaultListId;
    def.name = "任务";
    def.color = "#3D6BFF";
    def.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    def.isDefault = true;
    m_lists.push_back(def);
    m_nextListId = kDefaultListId + 1;
    m_groups.reset(0);
    rebuildSegmentTree();
}

// ------------------------------------------------------------------ 任务 CRUD

int TaskDocument::addTask(Task task) {
    if (task.title.empty()) return -1;
    if (task.id < 0) task.id = m_nextTaskId++;
    else if (task.id >= m_nextTaskId) m_nextTaskId = task.id + 1;
    if (task.createdAt.empty()) {
        task.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    }
    if (findList(task.listId) == nullptr) task.listId = defaultListId();

    m_tasks.push_back(task);
    indexTags(task.id, task.tags);
    m_titleTrie.insert(task.title);
    ++m_titleCount;
    rebuildSegmentTree(m_windowDays);
    refreshTagNames();
    return task.id;
}

bool TaskDocument::updateTask(const Task& task) {
    // 先拍快照，成功后再入栈：失败的操作不该留下一条空的撤销记录。
    Snapshot snap = takeSnapshot();
    for (Task& existing : m_tasks) {
        if (existing.id != task.id) continue;
        unindexTags(existing.id);
        existing = task;
        if (existing.listId < 0 || findList(existing.listId) == nullptr) {
            existing.listId = defaultListId();
        }
        indexTags(existing.id, existing.tags);
        rebuildTrieIndex();
        rebuildSegmentTree(m_windowDays);
        refreshTagNames();
        pushSnapshot(std::move(snap));
        return true;
    }
    return false;
}

bool TaskDocument::removeTask(int id) {
    Snapshot snap = takeSnapshot();
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->id != id) continue;
        unindexTags(id);
        m_tasks.erase(it);
        rebuildTrieIndex();
        rebuildSegmentTree(m_windowDays);
        refreshTagNames();
        pushSnapshot(std::move(snap));
        return true;
    }
    return false;
}

Task* TaskDocument::findTask(int id) {
    for (Task& t : m_tasks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const Task* TaskDocument::findTask(int id) const {
    for (const Task& t : m_tasks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

// --------------------------------------------------------------------- 列表

int TaskDocument::addList(const std::string& name, const std::string& color) {
    if (name.empty()) return -1;
    TaskList list;
    list.id = m_nextListId++;
    list.name = name;
    list.color = color;
    list.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    m_lists.push_back(list);

    // 并查集的下标是「列表在 m_lists 中的位置」，列表增加后必须同步容量。
    // 关键：不能只 reset——那会把已有的分组关系（以及 TaskList::groupId）一起清掉，
    // 于是「加一个新列表」就会摧毁所有分组。正确做法是按 groupId 重建并查集，
    // 新加进来的列表自然自成一个集合。
    rebuildGroupsFromListIds();
    return list.id;
}

bool TaskDocument::removeList(int listId) {
    if (listId == kDefaultListId) return false;      // 默认列表不可删
    for (auto it = m_lists.begin(); it != m_lists.end(); ++it) {
        if (it->id != listId) continue;
        // 列表内的任务迁移到默认列表，避免任务悬空。
        for (Task& t : m_tasks) {
            if (t.listId == listId) t.listId = defaultListId();
        }
        m_lists.erase(it);
        // 下标变了，并查集要整体重建（它不支持删除元素）。
        //
        // 这里**不能**用 m_groups.reset()：那只清空并查集，不会清掉各
        // TaskList::groupId。结果是删掉一个列表就把所有分组都抹掉了，
        // 而残留的 groupId 下次读库时又被 rebuildGroupsFromListIds 恢复，
        // 出现「这次运行分组没了、重启后又回来」的诡异现象。
        // 正确做法：按幸存的 groupId 重建分组关系。
        rebuildGroupsFromListIds();
        rebuildSegmentTree(m_windowDays);
        return true;
    }
    return false;
}

bool TaskDocument::renameList(int listId, const std::string& name) {
    if (name.empty()) return false;
    TaskList* list = findList(listId);
    if (!list) return false;
    list->name = name;
    return true;
}

bool TaskDocument::setListColor(int listId, const std::string& color) {
    if (color.empty()) return false;
    TaskList* list = findList(listId);
    if (!list) return false;
    list->color = color;
    return true;
}

bool TaskDocument::removeListFromGroup(int listId) {
    TaskList* list = findList(listId);
    if (!list) return false;
    if (list->groupId < 0) return false;          // 本来就不在任何分组里

    Snapshot snap = takeSnapshot();
    list->groupId = -1;
    // 并查集没法从集合里摘掉一个元素，所以按剩下的 groupId 重建整张关系。
    // 重建后原来的同组伙伴仍然互相成组（它们的 groupId 没变），
    // 而被摘掉的这一条变成独立集合（setSize == 1 -> groupId 归 -1）。
    rebuildGroupsFromListIds();
    syncGroupIds();
    pushSnapshot(std::move(snap));
    return true;
}

TaskList* TaskDocument::findList(int listId) {
    for (TaskList& l : m_lists) {
        if (l.id == listId) return &l;
    }
    return nullptr;
}

int TaskDocument::defaultListId() const {
    for (const TaskList& l : m_lists) {
        if (l.isDefault) return l.id;
    }
    return m_lists.empty() ? kDefaultListId : m_lists.front().id;
}

// ------------------------------------------------- 并查集：列表分组

// 并查集下标 = 列表在 m_lists 中的位置；取不到返回 -1。
int TaskDocument::listIndexOf(int listId) const {
    for (std::size_t i = 0; i < m_lists.size(); ++i) {
        if (m_lists[i].id == listId) return static_cast<int>(i);
    }
    return -1;
}

// 并查集必须与列表数量同步，否则下标越界。
void TaskDocument::ensureGroupSets() {
    if (m_groups.size() != static_cast<int>(m_lists.size())) {
        m_groups.reset(static_cast<int>(m_lists.size()));
        m_groupSets = m_groups.setCount();
    }
}

bool TaskDocument::joinListsIntoGroup(int listA, int listB) {
    ensureGroupSets();
    const int indexA = listIndexOf(listA);
    const int indexB = listIndexOf(listB);
    if (indexA < 0 || indexB < 0) return false;
    Snapshot snap = takeSnapshot();
    const bool merged = m_groups.join(indexA, indexB);
    if (merged) {
        syncGroupIds();
        pushSnapshot(std::move(snap));
    }
    return merged;
}

bool TaskDocument::listsShareGroup(int listA, int listB) {
    ensureGroupSets();
    const int indexA = listIndexOf(listA);
    const int indexB = listIndexOf(listB);
    if (indexA < 0 || indexB < 0) return false;
    return m_groups.sameSet(indexA, indexB);
}

std::vector<int> TaskDocument::listsInSameGroup(int listId) {
    ensureGroupSets();
    std::vector<int> out;
    const int index = listIndexOf(listId);
    if (index < 0) return out;
    for (int member : m_groups.members(index)) {
        if (member >= 0 && member < static_cast<int>(m_lists.size())) {
            out.push_back(m_lists[static_cast<std::size_t>(member)].id);
        }
    }
    return out;
}

int TaskDocument::groupNumberCount() const {
    // 收集出现过的 groupId 并去重。groupId 是「组内最小的列表 id」，所以
    // 同一个组的列表会共享同一个值；未分组是 -1，不算。
    std::vector<int> ids;
    ids.reserve(m_lists.size());
    for (const TaskList& l : m_lists) {
        if (l.groupId >= 0) ids.push_back(l.groupId);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return static_cast<int>(ids.size());
}

void TaskDocument::syncGroupIds() {
    ensureGroupSets();
    // 分组号用「组内最小的列表 id」，而不是并查集的根下标：根下标依赖列表在
    // 数组里的位置，存取一轮或删掉一个列表就会整体错位。用列表 id 就稳定了。
    for (std::size_t i = 0; i < m_lists.size(); ++i) {
        // 集合大小为 1 时视为「无分组」
        if (m_groups.setSize(static_cast<int>(i)) == 1) {
            m_lists[i].groupId = -1;
            continue;
        }
        int smallest = m_lists[i].id;
        for (int member : m_groups.members(static_cast<int>(i))) {
            if (member >= 0 && member < static_cast<int>(m_lists.size())) {
                smallest = std::min(smallest, m_lists[static_cast<std::size_t>(member)].id);
            }
        }
        m_lists[i].groupId = smallest;
    }
    m_groupSets = m_groups.setCount();
}

// ------------------------------------------------- 哈希表：标签倒排索引

void TaskDocument::indexTags(int taskId, const std::vector<std::string>& tags) {
    for (const std::string& raw : tags) {
        const std::string tag = normalizeTitle(raw);
        if (tag.empty()) continue;
        std::vector<int> ids = m_tagIndex.value(tag);
        if (std::find(ids.begin(), ids.end(), taskId) == ids.end()) {
            ids.push_back(taskId);
        }
        m_tagIndex.insert(tag, ids);
    }
}

void TaskDocument::unindexTags(int taskId) {
    // 必须先取一份键的副本：keys() 返回的是临时对象，若在 range-for 里直接遍历
    // 它、循环体内又去修改哈希表，临时对象会在循环中途被销毁 -> 悬垂引用。
    const std::vector<std::string> tags = m_tagIndex.keys();
    for (const std::string& tag : tags) {
        std::vector<int> ids = m_tagIndex.value(tag);
        const auto it = std::find(ids.begin(), ids.end(), taskId);
        if (it == ids.end()) continue;
        ids.erase(it);
        if (ids.empty()) {
            m_tagIndex.remove(tag);
        } else {
            m_tagIndex.insert(tag, ids);
        }
    }
}

void TaskDocument::rebuildTagIndex() {
    m_tagIndex.clear();
    for (const Task& t : m_tasks) {
        indexTags(t.id, t.tags);
    }
    refreshTagNames();
}

void TaskDocument::refreshTagNames() {
    m_tagNames = m_tagIndex.keys();
    std::sort(m_tagNames.begin(), m_tagNames.end());
}

std::vector<TagCount> TaskDocument::tagCounts() const {
    std::vector<TagCount> out;
    for (const std::string& tag : m_tagNames) {
        out.push_back(TagCount{tag, static_cast<int>(m_tagIndex.value(tag).size())});
    }
    std::sort(out.begin(), out.end(), [](const TagCount& a, const TagCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.tag < b.tag;
    });
    return out;
}

std::vector<int> TaskDocument::taskIdsWithTag(const std::string& tag) const {
    return m_tagIndex.value(normalizeTitle(tag));
}

int TaskDocument::tagUsage(const std::string& tag) const {
    return static_cast<int>(m_tagIndex.value(normalizeTitle(tag)).size());
}

std::size_t TaskDocument::tagProbeLength(const std::string& tag) const {
    return m_tagIndex.probeLength(normalizeTitle(tag));
}

// ------------------------------------------------- 哈希表：重复标题检测

std::vector<DuplicateGroup> TaskDocument::duplicateTitleGroups() const {
    HashTable<std::string, std::vector<int>> index;
    for (const Task& t : m_tasks) {
        const std::string key = normalizeTitle(t.title);
        if (key.empty()) continue;
        std::vector<int> ids = index.value(key);
        ids.push_back(t.id);
        index.insert(key, ids);
    }

    std::vector<DuplicateGroup> out;
    for (const auto& entry : index.entries()) {
        if (entry.second.size() < 2) continue;
        DuplicateGroup group;
        group.normalizedTitle = entry.first;
        group.taskIds = entry.second;
        std::sort(group.taskIds.begin(), group.taskIds.end());
        out.push_back(group);
    }
    std::sort(out.begin(), out.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        if (a.taskIds.size() != b.taskIds.size()) return a.taskIds.size() > b.taskIds.size();
        return a.normalizedTitle < b.normalizedTitle;
    });
    return out;
}

// ----------------------------------------------------- Trie：搜索与补全

void TaskDocument::rebuildTrieIndex() {
    Trie fresh;
    for (const Task& t : m_tasks) {
        fresh.insert(t.title);
    }
    m_titleTrie = fresh;
    m_titleCount = m_tasks.size();
}

std::vector<std::string> TaskDocument::titleCompletions(const std::string& prefix, int limit) const {
    std::vector<std::string> out;
    if (prefix.empty()) return out;
    for (const std::string& word : m_titleTrie.getWordsWithPrefix(prefix)) {
        out.push_back(word);
        if (limit > 0 && static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

std::vector<int> TaskDocument::taskIdsWithTitlePrefix(const std::string& prefix) const {
    std::vector<int> out;
    if (prefix.empty()) return out;
    for (const std::string& title : m_titleTrie.getWordsWithPrefix(prefix)) {
        for (const Task& t : m_tasks) {
            if (t.title == title) out.push_back(t.id);
        }
    }
    return out;
}

// ------------------------------------------- 线段树：截止日期区间统计

void TaskDocument::rebuildSegmentTree(int windowDays) {
    m_windowDays = windowDays < 1 ? 1 : windowDays;
    m_deadlineTree.resize(m_windowDays);

    const QDate today = QDate::currentDate();
    for (const Task& t : m_tasks) {
        if (t.status == 2) continue;             // 已完成不占「计划」
        if (!t.hasDeadline()) continue;
        const QDate due = QDate::fromString(QString::fromStdString(t.deadline.substr(0, 10)),
                                           "yyyy-MM-dd");
        if (!due.isValid()) continue;
        const qint64 offset = today.daysTo(due);
        if (offset < 0 || offset >= m_windowDays) continue;   // 窗口外的单独统计
        m_deadlineTree.add(static_cast<long long>(offset), 1);
    }
}

DeadlineStats TaskDocument::deadlineStats(int windowDays) const {
    DeadlineStats stats;
    const QDate today = QDate::currentDate();

    for (const Task& t : m_tasks) {
        if (t.status == 2 || !t.hasDeadline()) continue;
        const QDate due = QDate::fromString(QString::fromStdString(t.deadline.substr(0, 10)),
                                           "yyyy-MM-dd");
        if (!due.isValid()) continue;
        const qint64 offset = today.daysTo(due);
        if (offset < 0) {
            ++stats.overdue;
            continue;
        }
        if (offset == 0) ++stats.dueToday;
        if (offset < windowDays) ++stats.total;
    }

    if (m_deadlineTree.days() > 0 && windowDays > 0) {
        const long long span = std::min<long long>(windowDays, m_deadlineTree.days());
        stats.busiestDayCount = static_cast<int>(m_deadlineTree.queryMax(0, span - 1));
        // 找出最忙的那一天（最近的一个）
        for (long long d = 0; d < span; ++d) {
            if (m_deadlineTree.pointValue(d) == stats.busiestDayCount) {
                stats.busiestDayOffset = static_cast<int>(d);
                break;
            }
        }
    }
    return stats;
}

std::vector<int> TaskDocument::dailyDeadlineCounts(int windowDays) const {
    std::vector<int> out;
    const long long span = std::min<long long>(windowDays, m_deadlineTree.days());
    out.reserve(static_cast<std::size_t>(span));
    for (long long d = 0; d < span; ++d) {
        out.push_back(static_cast<int>(m_deadlineTree.pointValue(d)));
    }
    return out;
}

// ------------------------------------------------------------- 查询与排序

bool TaskDocument::matches(const Task& task, const TaskFilter& filter) {
    if (filter.useSmartView) {
        switch (filter.smartView) {
            case SmartView::MyDay:
                if (!task.inMyDay) return false;
                break;
            case SmartView::Important:
                if (!task.starred) return false;
                break;
            case SmartView::Planned:
                if (!task.hasDeadline() || task.status == 2) return false;
                break;
            case SmartView::Completed:
                if (task.status != 2) return false;
                break;
            case SmartView::All:
            default:
                break;
        }
    } else if (filter.listId >= 0) {
        if (task.listId != filter.listId) return false;
    }

    if (filter.hideDone && task.status == 2) return false;

    if (filter.categoryId >= 0 && task.categoryId != filter.categoryId) return false;

    if (!filter.tag.empty()) {
        bool found = false;
        for (const std::string& t : task.tags) {
            if (normalizeTitle(t) == normalizeTitle(filter.tag)) { found = true; break; }
        }
        if (!found) return false;
    }

    if (!filter.search.empty()) {
        const std::string needle = normalizeTitle(filter.search);
        if (normalizeTitle(task.title).find(needle) == std::string::npos
            && normalizeTitle(task.details).find(needle) == std::string::npos) {
            return false;
        }
    }
    return true;
}

void TaskDocument::sortTasks(std::vector<Task>& out, TaskSort sort) {
    switch (sort) {
        case TaskSort::DeadlineAsc:
            std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                // 无截止时间的排在有期限之后（已完成的置底由下面的统一处理负责）
                const bool aHas = a.hasDeadline();
                const bool bHas = b.hasDeadline();
                if (aHas != bHas) return aHas;
                if (a.deadline != b.deadline) return a.deadline < b.deadline;
                return a.id < b.id;
            });
            break;
        case TaskSort::PriorityDesc:
            std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.id < b.id;
            });
            break;
        case TaskSort::TitleAsc:
            std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                if (a.title != b.title) return a.title < b.title;
                return a.id < b.id;
            });
            break;
        case TaskSort::Manual:
            std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                // 手工顺序：先比 manualOrder，相同再按 id，保证稳定且可复现。
                if (a.manualOrder != b.manualOrder) return a.manualOrder < b.manualOrder;
                return a.id < b.id;
            });
            break;
        case TaskSort::CreatedAsc:
        default:
            std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
                return a.id < b.id;
            });
            break;
    }

    // 统一收尾：**已完成一律置底**。
    //
    // 原来只有 DeadlineAsc / PriorityDesc / CreatedAsc 做了这件事，
    // Manual 与 TitleAsc 漏掉了 —— 于是「手动拖拽」模式下改完状态，
    // 已完成的任务还停在原地，看起来就像"自动置底失效了"。
    // 用 stable_sort 保证同组内保持上面排好的相对顺序。
    std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
        const bool aDone = a.status == 2;
        const bool bDone = b.status == 2;
        if (aDone == bDone) return false;      // 同组：保持原有次序
        return !aDone;                          // 未完成的排在前面
    });

    // 再追加一条：**进行中置顶**。
    // 需求是"进行中任务自动置顶"——正在做的事应该一眼看到，不该埋在一堆
    // 待办中间。再串一次 stable_sort 即可，不必改动上面各排序模式的规则。
    std::stable_sort(out.begin(), out.end(), [](const Task& a, const Task& b) {
        const bool aDoing = a.status == 1;
        const bool bDoing = b.status == 1;
        if (aDoing == bDoing) return false;
        return aDoing;
    });
}

std::vector<Task> TaskDocument::query(const TaskFilter& filter) const {
    std::vector<Task> out;
    // 标签筛选走哈希索引，避免全表扫描
    if (!filter.tag.empty()) {
        for (int id : taskIdsWithTag(filter.tag)) {
            const Task* t = findTask(id);
            if (t && matches(*t, filter)) out.push_back(*t);
        }
    } else {
        for (const Task& t : m_tasks) {
            if (matches(t, filter)) out.push_back(t);
        }
    }
    sortTasks(out, filter.sort);
    return out;
}

// ------------------------------------------------------------------ 统计

int TaskDocument::countByStatus(int status) const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.status == status) ++n;
    }
    return n;
}

int TaskDocument::countStarred() const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.starred) ++n;
    }
    return n;
}

int TaskDocument::countInMyDay() const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.inMyDay) ++n;
    }
    return n;
}

int TaskDocument::countPlanned() const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.hasDeadline() && t.status != 2) ++n;
    }
    return n;
}

int TaskDocument::countCompleted() const {
    return countByStatus(2);
}

int TaskDocument::countInList(int listId) const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.listId == listId && t.status != 2) ++n;
    }
    return n;
}

// -------------------------------------------------------------- 重复任务

bool TaskDocument::nextOccurrence(const Task& task, std::string& nextDeadline) {
    if (task.recurrence == Recurrence::None) return false;
    if (!task.hasDeadline()) return false;

    const QDate due = QDate::fromString(QString::fromStdString(task.deadline.substr(0, 10)),
                                       "yyyy-MM-dd");
    if (!due.isValid()) return false;

    QDate next;
    switch (task.recurrence) {
        case Recurrence::Daily:
            next = due.addDays(1);
            break;
        case Recurrence::Weekdays: {
            next = due.addDays(1);
            while (next.dayOfWeek() > 5) next = next.addDays(1);   // 跳过周六周日
            break;
        }
        case Recurrence::Weekly:
            next = due.addDays(7);
            break;
        case Recurrence::Monthly:
            next = due.addMonths(1);
            break;
        case Recurrence::Yearly:
            next = due.addYears(1);
            break;
        case Recurrence::None:
        default:
            return false;
    }

    const QString timePart = QString::fromStdString(task.deadline).mid(10);   // " HH:mm"
    nextDeadline = (next.toString("yyyy-MM-dd") + timePart).toStdString();
    return true;
}

int TaskDocument::spawnNextOccurrence(int completedTaskId) {
    Task* done = findTask(completedTaskId);
    if (!done) return -1;

    std::string nextDeadline;
    if (!nextOccurrence(*done, nextDeadline)) return -1;

    Task fresh = *done;
    fresh.id = -1;
    fresh.status = 0;
    fresh.deadline = nextDeadline;
    fresh.completedAt.clear();
    fresh.inMyDay = false;                 // 新实例不自动进「我的一天」
    fresh.reminder.clear();                // 提醒需要按新日期重设
    for (Subtask& s : fresh.subtasks) {    // 子任务重置为未完成，并重新编号
        s.done = false;
    }
    return addTask(fresh);
}

// ------------------------------------------------------------- 我的一天

void TaskDocument::clearMyDay() {
    for (Task& t : m_tasks) {
        t.inMyDay = false;
    }
}

std::vector<int> TaskDocument::suggestions(int limit) const {
    const QDate today = QDate::currentDate();

    struct Candidate {
        int id;
        int bucket;      // 0 逾期, 1 今天, 2 最近添加
        int priority;
        std::string deadline;
    };
    std::vector<Candidate> pool;

    for (const Task& t : m_tasks) {
        if (t.status == 2) continue;
        Candidate c{t.id, 2, t.priority, t.deadline};
        if (t.hasDeadline()) {
            const QDate due = QDate::fromString(QString::fromStdString(t.deadline.substr(0, 10)),
                                               "yyyy-MM-dd");
            if (due.isValid()) {
                const qint64 offset = today.daysTo(due);
                if (offset < 0) c.bucket = 0;
                else if (offset == 0) c.bucket = 1;
            }
        }
        pool.push_back(c);
    }

    std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) {
        if (a.bucket != b.bucket) return a.bucket < b.bucket;
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        return a.id < b.id;
    });

    std::vector<int> out;
    for (const Candidate& c : pool) {
        out.push_back(c.id);
        if (limit > 0 && static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

void TaskDocument::setNextIds(int taskId, int listId) {
    m_nextTaskId = taskId < 0 ? 0 : taskId;
    m_nextListId = listId < kDefaultListId ? kDefaultListId : listId;
}

// ------------------------------------------------------------ 拖拽排序

void TaskDocument::normalizeManualOrder() {
    // 按「manualOrder, id」排好，然后重新发号 0,1,2,...
    std::vector<Task *> ordered;
    ordered.reserve(m_tasks.size());
    for (Task &t : m_tasks) ordered.push_back(&t);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Task *a, const Task *b) {
        if (a->manualOrder != b->manualOrder) return a->manualOrder < b->manualOrder;
        return a->id < b->id;
    });
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        ordered[i]->manualOrder = static_cast<int>(i);
    }
}

bool TaskDocument::moveTask(int taskId, int targetPos, const std::vector<int> &positions) {
    if (positions.empty()) return false;

    // 找到源位置
    int from = -1;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (positions[i] == taskId) {
            from = static_cast<int>(i);
            break;
        }
    }
    if (from < 0) return false;

    // 目标位置夹到合法范围
    int to = targetPos;
    if (to < 0) to = 0;
    if (to >= static_cast<int>(positions.size())) to = static_cast<int>(positions.size()) - 1;
    if (from == to) return false;

    std::vector<int> reordered = positions;
    const int moved = reordered[static_cast<std::size_t>(from)];
    reordered.erase(reordered.begin() + from);
    reordered.insert(reordered.begin() + to, moved);

    // 按新顺序给 manualOrder 发号；只作用于视图里的这些任务，
    // 视图外的任务保持原来的相对次序（它们的值会与视图内重叠，
    // 所以随后统一 normalize 一次，保证全局唯一有序）。
    Snapshot snap = takeSnapshot();
    for (std::size_t i = 0; i < reordered.size(); ++i) {
        Task *t = findTask(reordered[i]);
        if (t) t->manualOrder = static_cast<int>(i);
    }
    normalizeManualOrder();
    rebuildSegmentTree(m_windowDays);      // 释放 snapshot 前保持索引自洽
    pushSnapshot(std::move(snap));
    return true;
}

// ------------------------------------------------------------------ 撤销

void TaskDocument::beginUndo() {
    pushSnapshot(takeSnapshot());
}

TaskDocument::Snapshot TaskDocument::takeSnapshot() const {
    Snapshot snap;
    snap.tasks = m_tasks;
    snap.lists = m_lists;
    snap.nextTaskId = m_nextTaskId;
    snap.nextListId = m_nextListId;
    snap.expandedId = m_expandedForUndo;
    return snap;
}

void TaskDocument::pushSnapshot(Snapshot snap) {
    m_undoStack.push_back(std::move(snap));
    // 只保留最近若干步，避免长时间运行后内存无限增长。
    constexpr std::size_t kMaxUndo = 50;
    if (m_undoStack.size() > kMaxUndo) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

bool TaskDocument::undo() {
    if (m_undoStack.empty()) return false;

    Snapshot snap = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    m_tasks = std::move(snap.tasks);
    m_lists = std::move(snap.lists);
    m_nextTaskId = snap.nextTaskId;
    m_nextListId = snap.nextListId;
    m_expandedForUndo = snap.expandedId;

    // 整篇恢复后，四种数据结构的索引都要跟着回到当时的状态。
    rebuildTagIndex();
    rebuildTrieIndex();
    rebuildSegmentTree(m_windowDays);
    m_groups.reset(static_cast<int>(m_lists.size()));
    rebuildGroupsFromListIds();
    syncGroupIds();
    return true;
}

void TaskDocument::rebuildIndexes() {
    rebuildTagIndex();
    rebuildTrieIndex();
    rebuildSegmentTree(m_windowDays);
    // 注意：这里故意不调用 syncGroupIds()。分组关系要么已经由
    // rebuildGroupsFromListIds() 从 groupId 恢复，要么调用方自己负责重算，
    // 否则会把刚读进来的 groupId 覆盖成「互不相交」。
    ensureGroupSets();
}

void TaskDocument::replaceLists(const std::vector<TaskList>& lists) {
    if (lists.empty()) return;
    m_lists = lists;
    // 默认列表必须存在，否则界面会把任务挂到不存在的列表上。
    const bool hasDefault = std::any_of(m_lists.begin(), m_lists.end(),
                                        [](const TaskList& l) { return l.isDefault; });
    if (!hasDefault) {
        m_lists.front().isDefault = true;
    }
    ensureGroupSets();
}

void TaskDocument::replaceTasks(const std::vector<Task>& tasks) {
    m_tasks = tasks;
    // 任务若指向不存在的列表，归属到默认列表，避免出现「孤儿任务」。
    const int fallback = defaultListId();
    for (Task& t : m_tasks) {
        if (t.listId < 0 || findList(t.listId) == nullptr) t.listId = fallback;
    }
    rebuildTagIndex();
    rebuildTrieIndex();
    rebuildSegmentTree(m_windowDays);
}

void TaskDocument::rebuildGroupsFromListIds() {
    m_groups.reset(static_cast<int>(m_lists.size()));
    // 同一个 groupId 的列表属于一个分组。groupId 是「组内最小的列表 id」，
    // 只依赖它相同 / 不同的语义，所以读回来直接按值分组即可。
    for (std::size_t i = 0; i < m_lists.size(); ++i) {
        const int groupId = m_lists[i].groupId;
        if (groupId < 0) continue;
        for (std::size_t j = i + 1; j < m_lists.size(); ++j) {
            if (m_lists[j].groupId == groupId) {
                m_groups.join(static_cast<int>(i), static_cast<int>(j));
            }
        }
    }
    m_groupSets = m_groups.setCount();
}
