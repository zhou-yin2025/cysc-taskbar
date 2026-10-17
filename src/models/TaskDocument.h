#ifndef TASKDOCUMENT_H
#define TASKDOCUMENT_H

// ============================================================================
//  TaskDocument.h
//  职责：应用层数据中枢：任务 + 列表 + 四种数据结构的索引
//  依赖：ds/*, models/Task.h
// ============================================================================

#include <QDate>
#include <QString>
#include <string>
#include <vector>

#include "Task.h"
#include "src/ds/HashTable.h"
#include "src/ds/SegmentTree.h"
#include "src/ds/Trie.h"
#include "src/ds/UnionFind.h"
#include "src/ds/Vector.h"

// 任务的排序方式。
enum class TaskSort {
    CreatedAsc = 0,
    DeadlineAsc,
    PriorityDesc,
    TitleAsc,
    Manual,          // 手工拖拽顺序（用 Task::manualOrder）
};

// 视图：要么看某个列表，要么看某个智能视图。
struct TaskFilter {
    int listId = -1;                    // >=0 时只看该列表
    SmartView smartView = SmartView::All;
    bool useSmartView = false;          // true 时忽略 listId
    int categoryId = -1;                // >=0 时只看该分类（0 其他/1 工作/2 学习/3 健康）
    std::string tag;                    // 非空时只看带该标签的任务（走哈希索引）
    std::string search;                 // 非空时做全文子串过滤
    bool hideDone = false;
    TaskSort sort = TaskSort::CreatedAsc;
};

// 日期区间统计结果（线段树查询）。
struct DeadlineStats {
    int total = 0;        // 窗口内总数
    int overdue = 0;      // 已逾期
    int dueToday = 0;     // 今天到期
    int busiestDayCount = 0;   // 最忙的一天有几件事
    int busiestDayOffset = -1; // 最忙那天距今几天
};

// 重复标题分组（哈希表去重）。
struct DuplicateGroup {
    std::string normalizedTitle;
    std::vector<int> taskIds;    // 同名的任务编号
};

// 标签统计项（哈希表倒排索引用量）。
struct TagCount {
    std::string tag;
    int count = 0;
};

// ---------------------------------------------------------------------------
//  任务文档：应用层的数据中枢。
//
//  四种数据结构在这里都有真实职责，而不是为了演示硬塞：
//    * HashTable  —— 标签 -> 任务编号 倒排索引（按标签筛任务）；
//                    标题 -> 任务编号 分组（重复标题检测）。
//    * Trie       —— 标题前缀索引，给搜索框做自动补全。
//    * SegmentTree—— 以天为叶结点的截止日期分布，支持任意区间统计与热力图。
//    * UnionFind  —— 列表分组的合并/查询（对应 To Do 的列表分组）。
// ---------------------------------------------------------------------------
class TaskDocument {
public:
    TaskDocument();

    // ---- 任务 ----
    int addTask(Task task);                     // 返回编号，失败返回 -1
    bool updateTask(const Task& task);
    bool removeTask(int id);
    Task* findTask(int id);
    const Task* findTask(int id) const;
    const std::vector<Task>& tasks() const { return m_tasks; }
    std::vector<Task>& tasks() { return m_tasks; }

    // ---- 列表 ----
    int addList(const std::string& name, const std::string& color = "#3D6BFF");
    bool removeList(int listId);
    bool renameList(int listId, const std::string& name);
    // 改列表颜色（侧栏圆点用它）。
    bool setListColor(int listId, const std::string& color);
    TaskList* findList(int listId);
    const std::vector<TaskList>& lists() const { return m_lists; }
    int defaultListId() const;

    // ---- 并查集：列表分组 ----
    bool joinListsIntoGroup(int listA, int listB);   // 合并成同一分组
    bool listsShareGroup(int listA, int listB);
    int groupCount() const { return m_groupSets; }
    // 「有几个分组」——即不同 groupId 的个数（未分组的列表不算）。
    // 注意别用 groupCount()：那是并查集的集合总数，**每个未分组列表也算一个
    // 单元素集合**，所以加/删一个列表它就会变，不能当分组个数。
    int groupNumberCount() const;
    std::vector<int> listsInSameGroup(int listId);
    // 把一个列表移出它所在的分组。
    // 并查集不支持「拆分集合」，所以做法是：清掉这个列表的 groupId，
    // 然后按剩余的 groupId 重建整个分组关系。
    bool removeListFromGroup(int listId);
    // 把分组关系写回 TaskList::groupId（每个集合给一个稳定的分组号）。
    void syncGroupIds();

    // ---- 哈希表：标签索引 ----
    const std::vector<std::string>& allTags() const { return m_tagNames; }
    std::vector<TagCount> tagCounts() const;            // 按出现次数降序
    std::vector<int> taskIdsWithTag(const std::string& tag) const;
    int tagUsage(const std::string& tag) const;
    std::size_t tagBucketCount() const { return m_tagIndex.bucketCount(); }
    std::size_t tagLongestChain() const { return m_tagIndex.longestChain(); }
    double tagLoadFactor() const { return m_tagIndex.loadFactor(); }

    // ---- 哈希表：重复标题检测 ----
    std::vector<DuplicateGroup> duplicateTitleGroups() const;

    // ---- Trie：搜索与补全 ----
    std::vector<std::string> titleCompletions(const std::string& prefix, int limit = 8) const;
    std::vector<int> taskIdsWithTitlePrefix(const std::string& prefix) const;
    std::size_t trieWordCount() const { return m_titleCount; }

    // ---- 线段树：截止日期区间统计 ----
    void rebuildSegmentTree(int windowDays = 400);
    DeadlineStats deadlineStats(int windowDays = 30) const;
    // 每一天的任务数（热力图用）。
    std::vector<int> dailyDeadlineCounts(int windowDays = 30) const;
    int segmentTreeDays() const { return static_cast<int>(m_deadlineTree.days()); }

    // ---- 查询与排序 ----
    std::vector<Task> query(const TaskFilter& filter) const;
    static bool matches(const Task& task, const TaskFilter& filter);
    static void sortTasks(std::vector<Task>& out, TaskSort sort);

    // ---- 统计 ----
    int countByStatus(int status) const;
    int countStarred() const;
    int countInMyDay() const;
    int countPlanned() const;
    int countCompleted() const;
    int countInList(int listId) const;

    // ---- 重复任务 ----
    // 把任务推进到下一个重复周期；返回 false 表示没有重复规则。
    static bool nextOccurrence(const Task& task, std::string& nextDeadline);
    // 按重复规则为已完成的任务生成下一个实例，返回新任务编号（-1 表示不需要）。
    int spawnNextOccurrence(int completedTaskId);

    // ---- 我的一天 ----
    void clearMyDay();
    // 「建议」：逾期 / 今天到期 / 最近添加，按优先级与截止时间排序。
    std::vector<int> suggestions(int limit = 5) const;

    // ---- 拖拽排序 ----
    // 把 taskId 移到「当前排序下第 targetPos 个」的位置。
    // positions 是当前视图的任务顺序（由界面按同样规则排序后传入），
    // 这样模型不必猜界面的过滤条件。改动记入撤销栈。
    bool moveTask(int taskId, int targetPos, const std::vector<int>& positions);
    // 把 manualOrder 压成 0,1,2,...（避免多次拖动后数值无界增长）。
    void normalizeManualOrder();

    // ---- 撤销 ----
    // 记录最近一次改动（增 / 删 / 改）并支持回退。栈里保存「改动前的整篇快照
    // （任务 + 列表）」，实现简单且不会因为字段增多而漏掉某一项。
    void beginUndo();
    bool undo();
    bool canUndo() const { return !m_undoStack.empty(); }
    int undoDepth() const { return static_cast<int>(m_undoStack.size()); }
    void clearUndo() { m_undoStack.clear(); }

    int nextTaskId() const { return m_nextTaskId; }
    int nextListId() const { return m_nextListId; }
    void setNextIds(int taskId, int listId);
    // 索引全部重建（载入数据后调用）。
    void rebuildIndexes();

    // ---- 只读访问四种数据结构：给「数据结构可视化面板」展示内部状态 ----
    // 都返回 const 引用 / 常量，面板不能借机改动业务数据。
    const HashTable<std::string, std::vector<int>>& tagIndex() const { return m_tagIndex; }
    // 标签倒排索引里某个键当前需要比较几次才能命中（哈希探测长度）。
    std::size_t tagProbeLength(const std::string& tag) const;
    const Trie& titleTrie() const { return m_titleTrie; }
    const SegmentTree& deadlineTree() const { return m_deadlineTree; }
    const UnionFind& listGroups() const { return m_groups; }

    // 撤销栈里记录的「展开项」（界面用来在撤销后恢复展开状态）。
    void setExpandedForUndo(int id) { m_expandedForUndo = id; }
    int expandedAfterUndo() const { return m_expandedForUndo; }

    // 载入持久化数据用：整体替换列表 / 任务，并同步内部索引。
    void replaceLists(const std::vector<TaskList>& lists);
    void replaceTasks(const std::vector<Task>& tasks);
    // 按 TaskList::groupId 重建并查集分组（读库后恢复分组关系）。
    void rebuildGroupsFromListIds();

private:
    void indexTags(int taskId, const std::vector<std::string>& tags);
    void unindexTags(int taskId);
    void rebuildTagIndex();
    void rebuildTrieIndex();
    void refreshTagNames();
    int listIndexOf(int listId) const;
    void ensureGroupSets();

    std::vector<Task> m_tasks;
    std::vector<TaskList> m_lists;
    int m_nextTaskId = 0;
    int m_nextListId = 1;

    // 撤销快照：整篇（任务 + 列表 + id 计数器）。
    struct Snapshot {
        std::vector<Task> tasks;
        std::vector<TaskList> lists;
        int nextTaskId = 0;
        int nextListId = 1;
        int expandedId = -1;
    };
    std::vector<Snapshot> m_undoStack;
    // 撤销时希望恢复「当时展开的是哪一条」，由界面在 beginUndo 前写入。
    int m_expandedForUndo = -1;

    Snapshot takeSnapshot() const;
    void pushSnapshot(Snapshot snap);

    // 四种数据结构
    HashTable<std::string, std::vector<int>> m_tagIndex;
    HashTable<std::string, std::vector<int>> m_titleIndex;
    Trie m_titleTrie;
    SegmentTree m_deadlineTree;
    UnionFind m_groups;
    int m_groupSets = 0;

    std::vector<std::string> m_tagNames;
    std::size_t m_titleCount = 0;
    int m_windowDays = 400;
};

#endif
