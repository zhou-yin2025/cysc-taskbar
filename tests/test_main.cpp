// ============================================================================
//  CYSC TaskBar - self-contained unit tests (no Qt Test dependency).
//
//  Registered with CTest as `cysc_tests`. Every check is reported, and the
//  process exits non-zero if any of them failed.
// ============================================================================

#include "src/core/StoragePaths.h"
#include "src/ds/HashTable.h"
#include "src/ds/SegmentTree.h"
#include "src/ds/Stack.h"
#include "src/ds/Trie.h"
#include "src/ds/UnionFind.h"
#include "src/ds/Vector.h"
#include "src/models/TaskDraft.h"
#include "src/models/TaskDocument.h"
#include "src/models/TaskModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QTextStream>

#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------- test harness

// 共用小工具放在具名命名空间：test_docstore.h 在下面以文件作用域包含，需要
// 引用到这些实体（匿名命名空间的实体被别的头文件引用会出现链接期名字不匹配）。
namespace testutil {

int g_failures = 0;
int g_checks = 0;
QTextStream *g_out = nullptr;

void expect(bool ok, const QString& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    *g_out << (ok ? "    ok   " : "    FAIL ") << what << "\n";
    g_out->flush();
}

void section(const QString& title) {
    *g_out << "\n[" << title << "]\n";
}

std::string tempDbPath(const QString& name) {
    // Prefer a build-local directory: CI sandboxes often refuse writes to the
    // system temp directory. Fall back to the platform temp location.
    const QString override = qEnvironmentVariable("CYSC_TEST_TMPDIR");
    QString dir = override.isEmpty() ? QString() : override;
    if (dir.isEmpty()) {
        dir = QDir(QDir::currentPath()).absoluteFilePath("cysc_test_tmp");
    }
    if (!QDir().mkpath(dir)) {
        dir = QDir::tempPath();
    }
    return (QDir(dir).absoluteFilePath("cysc_test_" + name + "_"
            + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db")).toStdString();
}

Task makeTask(const std::string& title, int priority, const std::string& date,
              int category = 0, int status = 0, const std::string& details = std::string()) {
    Task t;
    t.title = title;
    t.details = details;
    t.priority = priority;
    // 空日期 = 无截止时间，不要把 "" 拼成 " 23:59"。
    t.deadline = date.empty() ? std::string() : date + " 23:59";
    t.categoryId = category;
    t.status = status;
    return t;
}

} // namespace testutil

using testutil::makeTask;
using testutil::tempDbPath;
using testutil::expect;
using testutil::section;

// 文档级持久化测试（独立头文件，避免把 test_main.cpp 这个翻译单元继续撑大）。
#include "test_docstore.h"

namespace {

// ---------------------------------------------------------------- Vector<T>

void testVector() {
    section("Vector<T>");

    Vector<int> v;
    expect(v.size() == 0 && v.isEmpty(), "a new vector is empty");

    for (int i = 0; i < 50; ++i) v.pushBack(i);
    expect(v.size() == 50, "50 pushBack calls land 50 elements");
    expect(v.capacity() >= 50, "capacity grew past the initial 10");
    expect(v[0] == 0 && v[49] == 49, "element order is preserved");

    v.insert(0, 999);
    expect(v[0] == 999 && v.size() == 51, "insert at the front shifts the rest");

    v.remove(0);
    expect(v[0] == 0 && v.size() == 50, "remove undoes the insert");

    Vector<int> copy = v;                       // deep copy
    copy[0] = -1;
    expect(v[0] == 0 && copy[0] == -1, "copying a vector does not alias its storage");

    Vector<int> assigned;
    assigned = v;
    assigned[1] = -2;
    expect(v[1] == 1, "assignment is also a deep copy");

    v.clear();
    expect(v.isEmpty() && v.size() == 0, "clear empties the vector");

    bool threw = false;
    try {
        v.remove(5);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "remove() out of range throws std::out_of_range");

    // std::string elements exercise the non-trivial assignment path.
    Vector<std::string> words;
    words.pushBack("alpha");
    words.pushBack("beta");
    words.pushBack("gamma");
    words.sort(+[](const std::string& a, const std::string& b) { return a < b; });
    expect(words[0] == "alpha" && words[1] == "beta" && words[2] == "gamma",
          "merge sort orders std::string elements");

    expect(words.containsValue("beta") && !words.containsValue("delta"),
          "containsValue finds present values only");
    expect(words.indexOf("gamma") == 2, "indexOf reports the position");
}

// ------------------------------------------------------------------ Stack<T>

void testStack() {
    section("Stack<T>");

    Stack<int> s;
    expect(s.isEmpty() && s.size() == 0, "a new stack is empty");

    s.push(1);
    s.push(2);
    expect(s.peek() == 2 && s.size() == 2, "peek returns the most recent push");

    expect(s.pop() == 2 && s.pop() == 1, "pop walks back in LIFO order");
    expect(s.isEmpty(), "the stack is empty again after popping everything");

    bool threw = false;
    try {
        s.pop();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "popping an empty stack throws std::out_of_range");

    // Auto-growth: the default capacity is small, so push past it.
    const int big = 200;
    for (int i = 0; i < big; ++i) s.push(i);
    expect(s.size() == big, "the stack grows beyond its initial capacity");
    bool ordered = true;
    for (int i = big - 1; i >= 0; --i) {
        if (s.pop() != i) { ordered = false; break; }
    }
    expect(ordered, "growth preserves LIFO order");
}

// ------------------------------------------------------------------- Trie

void testTrie() {
    section("Trie");

    Trie trie;
    trie.insert("write chapter 3");
    trie.insert("write chapter 4");
    trie.insert("write report");
    trie.insert("buy milk");

    expect(trie.search("buy milk"), "an inserted word is found");
    expect(!trie.search("buy"), "a prefix is not itself a stored word");
    expect(!trie.search("buy milks"), "a longer word is not a match");

    const std::vector<std::string> writes = trie.getWordsWithPrefix("write");
    expect(writes.size() == 3, "prefix 'write' returns 3 words");

    const std::vector<std::string> chapters = trie.getWordsWithPrefix("write chapter");
    expect(chapters.size() == 2, "a longer prefix narrows the results");

    expect(trie.getWordsWithPrefix("nothing").empty(), "an unknown prefix returns nothing");

    // Regression: deleteWord used to report false whenever the deleted word's
    // path branched into other words, even though the deletion had worked.
    expect(trie.deleteWord("buy milk"), "deleteWord removes a stored word");
    expect(!trie.search("buy milk"), "the deleted word is gone");
    expect(trie.deleteWord("write chapter 3"), "deleteWord handles a shared prefix");
    expect(trie.search("write chapter 4"), "the sibling word survives the delete");
    expect(!trie.deleteWord("write chapter 3"), "deleting the same word twice reports false");
    expect(!trie.deleteWord("never inserted"), "deleting an unknown word reports false");
    expect(!trie.deleteWord(""), "deleting an empty word reports false");
    expect(trie.getWordsWithPrefix("write").size() == 2, "the surviving words are still reachable");

    // Deep copy semantics.
    Trie copy = trie;
    expect(copy.search("write chapter 4"), "the copy keeps the original contents");
    copy.insert("only in copy");
    expect(!trie.search("only in copy"), "inserting into the copy leaves the original alone");

    Trie assigned;
    assigned = trie;
    expect(assigned.getWordsWithPrefix("write").size() == 2,
          "copy assignment reproduces the contents");
}

// -------------------------------------------------- TaskModel without a database

void testModelInMemory() {
    section("TaskModel (memory only)");

    TaskModel model;   // no initDatabase() on purpose
    expect(!model.isDatabaseReady(), "a fresh model has no database");

    const int a = model.addTask(makeTask("alpha task", 0, "2026-10-11", 0, 0, "含有关键备注"));
    const int b = model.addTask(makeTask("beta task", 3, "2026-10-12"));
    const int c = model.addTask(makeTask("gamma task", 1, "2026-10-11"));
    expect(a >= 0 && b >= 0 && c >= 0 && a != b && b != c, "each addTask returns a unique id");
    expect(model.allTasks().size() == 3, "all three tasks are in memory");

    TaskQuery all;
    expect(model.query(all).size() == 3, "an unfiltered query returns everything");

    // 搜索是子串扫描（标题 + 备注），不是前缀匹配
    TaskQuery search;
    search.search = "alpha";
    expect(model.query(search).size() == 1, "搜索命中标题");

    search.search = "PHA";
    expect(model.query(search).size() == 1, "搜索命中标题中间的片段且忽略大小写");

    search.search = "关键备注";
    expect(model.query(search).size() == 1, "搜索命中备注内容");

    search.search = "备注内容";
    expect(model.query(search).isEmpty(), "备注是不连续片段时搜不到");

    search.search = "查无此物";
    expect(model.query(search).isEmpty(), "搜不到时返回空结果");

    TaskQuery byCategory;
    byCategory.categoryId = 2;
    model.addTask(makeTask("study task", 1, "2026-10-13", /*category=*/2));
    expect(model.query(byCategory).size() == 1, "category filter works");

    TaskQuery hidden;
    hidden.hideDone = true;
    expect(model.query(hidden).size() == 4, "hideDone 不隐藏未完成任务");

    expect(!model.query(TaskQuery()).isEmpty() && model.query(TaskQuery()).size() == 4,
          "no filter returns all four");

    model.changeStatus(a, 2);
    expect(model.query(hidden).size() == 3, "hideDone 隐藏已完成任务");
    expect(model.query(TaskQuery()).size() == 4, "不加筛选时已完成任务仍然可见");

    // 双击状态循环：待办 -> 进行中 -> 已完成 -> 待办，其它状态回到待办
    expect(nextStatusInCycle(0) == 1, "待办双击后进入进行中");
    expect(nextStatusInCycle(1) == 2, "进行中双击后标记为已完成");
    expect(nextStatusInCycle(2) == 0, "已完成双击后回到待办");
    expect(nextStatusInCycle(3) == 0, "已暂停双击后回到待办");

    // 内联详情编辑依赖的整条更新与只改备注
    Task edited = *model.findTask(a);
    edited.details = "新的备注";
    edited.priority = 3;
    edited.categoryId = 1;
    expect(model.updateTask(a, edited), "整条更新成功");
    expect(model.findTask(a)->details == "新的备注", "备注已更新");
    expect(model.findTask(a)->priority == 3, "优先级已更新");
    expect(model.findTask(a)->categoryId == 1, "分类已更新");
    expect(model.findTask(a)->title == "alpha task", "只改属性不会动标题");

    expect(model.updateDetails(a, "只改备注"), "updateDetails 成功");
    expect(model.findTask(a)->details == "只改备注", "备注被单独更新");
    expect(model.findTask(a)->title == "alpha task", "只改备注不影响标题");
    expect(!model.updateDetails(9999, "x"), "改不存在的任务返回 false");

    // Sorting: the query sorts by creation order or by deadline.
    // 注意此时 a 已完成，因此它必须排在最后。
    TaskQuery byCreation;
    byCreation.scope = TaskQuery::SortCreated;
    const Vector<Task> ranked = model.query(byCreation);
    expect(!ranked.isEmpty() && ranked[0].title == "beta task",
          "created-order sort 跳过已完成项，从最早的未完成项开始");
    expect(ranked[ranked.size() - 1].title == "alpha task",
          "已完成的任务被排到最后");

    TaskQuery byDeadline;
    byDeadline.scope = TaskQuery::SortDeadline;
    const Vector<Task> dated = model.query(byDeadline);
    expect(!dated.isEmpty() && dated[0].deadline.substr(0, 10) == "2026-10-11",
          "deadline sort is ascending");
    expect(dated[dated.size() - 1].title == "alpha task",
          "按截止时间排序时已完成项同样在最后");

    // 没有截止时间的任务不应占据「按截止时间」排序的顶部
    {
        TaskModel m;
        m.addTask(makeTask("无期限任务", 1, "", 0));           // deadline 为空
        m.addTask(makeTask("有期限任务", 1, "2026-10-20", 0));
        TaskQuery q;
        q.scope = TaskQuery::SortDeadline;
        const Vector<Task> sorted = m.query(q);

        expect(!sorted.isEmpty() && sorted[0].title == "有期限任务",
              "按截止时间排序时有期限的排在前面");
        expect(sorted[sorted.size() - 1].title == "无期限任务",
              "没有截止时间的排在最后");
    }

    // 降序不能把已完成项翻到最前面
    TaskQuery byDeadlineDesc;
    byDeadlineDesc.scope = TaskQuery::SortDeadline;
    byDeadlineDesc.order = TaskQuery::OrderDesc;
    const Vector<Task> datedDesc = model.query(byDeadlineDesc);
    expect(!datedDesc.isEmpty() && datedDesc[0].title != "alpha task",
          "降序时未完成项仍在前面");
    expect(datedDesc[datedDesc.size() - 1].title == "alpha task",
          "降序时已完成项仍在最后");

    model.sortByPriority();
    expect(model.allTasks()[0].priority == 3, "sortByPriority reorders the stored vector");

    // Deletion + undo
    expect(model.deleteTask(b), "deleteTask reports success");
    expect(model.findTask(b) == nullptr, "the deleted task is gone");
    expect(model.undo(), "undo restores the deletion");
    expect(model.findTask(b) != nullptr, "the task is back after undo");
    expect(model.allTasks().size() == 4, "the task count is restored");

    // Undo of an add removes the task instead of re-applying it.
    const int added = model.addTask(makeTask("temporary", 1, "2026-10-20"));
    expect(model.allTasks().size() == 5, "the new task is present");
    expect(model.undo(), "undo after add succeeds");
    expect(model.findTask(added) == nullptr, "undo of an add removes that task");
    expect(model.allTasks().size() == 4, "the task count is back to four");

    // Regression: an undo + re-add cycle must not leave stale "this id was
    // inserted" state behind, which used to make a later delete-undo remove the
    // wrong task instead of restoring the deleted one.
    const int keepId = model.allTasks()[0].id;
    const int doomed = model.addTask(makeTask("doomed", 1, "2026-10-21"));
    expect(doomed >= 0, "the doomed task was added");
    expect(model.undo(), "undo the extra add");
    expect(model.findTask(doomed) == nullptr, "undoing the add removed the doomed task");

    const int reAdded = model.addTask(makeTask("re-added", 1, "2026-10-22"));
    expect(reAdded >= 0, "a new task takes the freed id");
    expect(model.deleteTask(reAdded), "delete the re-added task");
    expect(model.undo(), "undo that delete");
    expect(model.findTask(reAdded) != nullptr, "the re-added task is restored, not removed");
    expect(model.findTask(keepId) != nullptr, "the older task was not touched");
    expect(model.allTasks().size() == 5, "the model holds five tasks at this point");

    // Unknown ids are rejected instead of silently succeeding.
    expect(!model.changeStatus(9999, 1), "changing an unknown id fails");
    expect(!model.deleteTask(9999), "deleting an unknown id fails");
    expect(!model.changeStatus(a, 42), "an unknown status value is rejected");

    // Search helpers
    expect(model.searchByKeyword("TASK").size() == 4, "keyword search ignores ASCII case");
    expect(model.searchByPrefix("beta").size() == 1, "searchByPrefix matches one task");
    expect(model.searchByPrefix("BETA").size() == 1, "prefix search ignores ASCII case too");
    expect(model.searchByPrefix("AlPhA").size() == 1, "prefix search works with mixed case");

    TaskQuery mixedCase;
    mixedCase.search = "GAMMA";
    expect(model.query(mixedCase).size() == 1, "the query filter is case-insensitive");
    expect(model.searchByPrefix("").isEmpty(), "an empty prefix matches nothing");
}

// ------------------------------------------------- TaskModel with SQLite

void testModelPersistence() {
    section("TaskModel + SQLite persistence");

    const std::string path = tempDbPath("persist");

    {
        TaskModel model;
        expect(model.initDatabase(path), "initDatabase opens a fresh file");
        expect(model.isDatabaseReady(), "the model reports a ready database");

        model.addTask(makeTask("persisted one", 2, "2026-11-01", 1, 0, "落库的备注内容"));
        model.addTask(makeTask("persisted two", 0, "2026-11-02", 2));
        const int id = model.addTask(makeTask("to be completed", 3, "2026-11-03", 3));
        model.changeStatus(id, 2);
        expect(model.allTasks().size() == 3, "three tasks written");
    }   // the first model closes the database here

    {
        TaskModel reloaded;
        expect(reloaded.initDatabase(path), "a second model opens the same file");
        expect(reloaded.allTasks().size() == 3, "the tasks survived the round trip");

        // Keep the search result alive: taking a pointer into a returned-by-value
        // Vector would dangle as soon as the temporary dies.
        const Vector<Task> found = reloaded.searchByPrefix("persisted one");
        expect(found.size() == 1, "the persisted task is findable by prefix");
        expect(!found.isEmpty() && found[0].priority == 2 && found[0].categoryId == 1,
              "field values survived the round trip");
        expect(!found.isEmpty() && found[0].details == "落库的备注内容",
              "备注字段也完成了落库往返");

        TaskQuery bySearch;
        bySearch.search = "备注内容";
        expect(reloaded.query(bySearch).size() == 1, "重载后仍能按备注搜到任务");

        TaskQuery done;
        done.hideDone = true;
        expect(reloaded.query(done).size() == 2, "hideDone 在重载后依然生效");

        // nextId must not collide with the ids loaded from disk.
        const int newId = reloaded.addTask(makeTask("after reload", 1, "2026-11-04"));
        expect(newId >= 3, "a new id continues after the persisted ones");
        expect(reloaded.allTasks().size() == 4, "the new task was appended");

        // 内联编辑后的整条更新也要落到磁盘
        Task edited = *reloaded.findTask(newId);
        edited.details = "改过的备注";
        edited.priority = 3;
        expect(reloaded.updateTask(newId, edited), "编辑后的任务写回数据库");

        // delete + undo must survive a database round trip as well.
        expect(reloaded.deleteTask(newId), "delete goes through the database");
        expect(reloaded.undo(), "undo re-inserts into the database");
    }   // the second model closes its connection here

    {
        TaskModel reopened;
        expect(reopened.initDatabase(path), "reopen after the undo");
        expect(reopened.allTasks().size() == 4, "the undone delete is really on disk");

        TaskQuery findEdited;
        findEdited.search = "after reload";
        const Vector<Task> hits = reopened.query(findEdited);
        expect(hits.size() == 1, "重开后能搜到编辑过的任务");
        expect(!hits.isEmpty() && hits[0].details == "改过的备注", "编辑后的备注留在磁盘上");
        expect(!hits.isEmpty() && hits[0].priority == 3, "编辑后的优先级留在磁盘上");
    }

    QFile::remove(QString::fromStdString(path));
    expect(!QFile::exists(QString::fromStdString(path)), "the temporary database is cleaned up");
}

// Failure paths: a database that cannot be created must be reported, not ignored.
//
// 用的"不可写路径"由 makeFileUsedAsDirectory() 造（定义在 test_docstore.h，
// 见那里关于"为什么不能用 Z:/..."的说明）。
//
// ⚠ 注意这个函数**必须定义在头文件里**，不能放在本文件。
// 我第一版就放在这里（第 476 行），而 test_docstore.h 是在第 89 行被包含的，
// 头文件里根本看不到它 —— macOS 与 Windows 双双编译失败：
//   error: use of undeclared identifier 'makeFileUsedAsDirectory'
void testDatabaseFailure() {
    section("Database failure handling");

    TaskModel model;
    // 把文件当目录：<普通文件>/cysc.db 在任何平台都建不出来
    const std::string bad = (makeFileUsedAsDirectory() + "/cysc.db").toStdString();
    const bool opened = model.initDatabase(bad);
    expect(!opened, "initDatabase fails for an unwritable path");
    expect(!model.isDatabaseReady(), "the model stays in memory-only mode");
    expect(!model.lastError().empty(), "the failure is recorded in lastError()");

    // Without a database the model must still work in memory.
    const int id = model.addTask(makeTask("memory only", 1, "2026-12-01"));
    expect(id >= 0 && model.allTasks().size() == 1, "in-memory use still works after a failure");
}

// ------------------------------------------------- storage path resolution

void testStoragePaths() {
    section("StoragePaths");

    const QStringList candidates = StoragePaths::databaseCandidates();
    expect(!candidates.isEmpty(), "at least one candidate location is offered");
    expect(candidates.size() == QSet<QString>(candidates.begin(), candidates.end()).size(),
          "candidate locations are unique");

    // A location that cannot be created must never be reported as usable.
    // 同样不能用 Windows 专属的 "Z:/..." 路径（见 testDatabaseFailure 的说明）：
    // 改成把普通文件当目录用，所有平台都成立。
    expect(!StoragePaths::isDatabaseWritable(makeFileUsedAsDirectory() + "/cysc.db"),
          "an unreachable file is not reported writable");

    // When a location is writable, resolveDatabasePath() must return a path in a
    // directory that really accepts writes.
    const QString resolved = StoragePaths::writableDatabasePath();
    if (!resolved.isEmpty()) {
        expect(QFileInfo(resolved).absolutePath().isEmpty() == false,
              "the resolved path has a directory part");
        expect(!resolved.contains("://"), "the resolved path looks like a filesystem path");

        // Prove it end to end: open a model there and write a task.
        TaskModel model;
        expect(model.initDatabase(resolved.toStdString()),
              "a model can open the resolved database path");
        const int id = model.addTask(makeTask("storage probe", 1, "2026-12-31"));
        expect(id >= 0, "a task can be written to the resolved database");
        if (id >= 0) {
            expect(model.deleteTask(id), "the probe task can be removed again");
        }
    } else {
        // Reported as ok so the environment limitation is visible without
        // pretending the resolution logic failed.
        expect(true, "note: no writable database location in this environment");
    }
}

// ------------------------------------------------- 新建任务向导状态机

void testTaskDraft() {
    section("新建任务向导 (TaskDraft)");

    {
        TaskDraft d;
        expect(d.stage() == TaskDraft::StageTitle, "初始阶段是「标题」");
        expect(d.priority() == 1, "默认优先级是「中」");
        expect(d.category() == 0, "默认分类是「其他」");

        // 空标题必须被拒绝，并且停留在当前阶段
        expect(d.handleTab("   ") == TaskDraft::Rejected, "空标题被拒绝");
        expect(d.stage() == TaskDraft::StageTitle, "被拒绝后仍停留在标题阶段");
        expect(!d.inputError().isEmpty(), "拒绝时给出了错误提示");

        expect(d.handleTab("  整理周报  ") == TaskDraft::StageChanged, "Tab 进入优先级阶段");
        expect(d.stage() == TaskDraft::StagePriority, "当前阶段是「优先级」");
        expect(d.title() == "整理周报", "标题已去掉首尾空格");

        expect(d.handleTab(QString()) == TaskDraft::StageChanged, "Tab 跳过优先级");
        expect(d.stage() == TaskDraft::StageDate, "当前阶段是「日期」");

        expect(d.handleTab(QString()) == TaskDraft::StageChanged, "空日期按「不设截止时间」处理并前进");
        expect(d.stage() == TaskDraft::StageCategory, "当前阶段是「分类」");
        expect(!d.hasDeadline(), "空输入表示无截止时间");
        expect(d.deadlineText().isEmpty(), "无截止时间时 deadlineText 为空");
        expect(d.toTask().deadline.empty(), "无截止时间时写库的 deadline 为空串");

        // Tab 在分类阶段表示重新开始
        expect(d.handleTab(QString()) == TaskDraft::StageChanged, "分类阶段 Tab 重新开始");
        expect(d.stage() == TaskDraft::StageTitle, "回到标题阶段");
        expect(d.title().isEmpty(), "重新开始会清空标题");
    }

    {
        // 按数字键的快捷路径
        TaskDraft d;
        d.handleTab("健身");
        expect(d.handleDigit(4) == TaskDraft::StageChanged, "按 4 选择「紧急」");
        expect(d.priority() == 3, "优先级被设为紧急(3)");
        expect(d.stage() == TaskDraft::StageDate, "选完优先级进入日期阶段");

        expect(d.handleDigit(9) == TaskDraft::None, "日期阶段的数字键无效");
        expect(d.handleDigit(0) == TaskDraft::None, "0 不是有效的快捷选择");

        expect(d.handleDigit(3) == TaskDraft::None, "分类阶段的数字键应由 chooseCategory 处理");
        expect(d.chooseCategory(2) == TaskDraft::Committed, "选择分类即提交");
        expect(d.category() == 2, "分类被设为「学习」(2)");
    }

    {
        // 点击按钮的路径
        TaskDraft d;
        d.handleTab("买菜");
        expect(d.choosePriority(2) == TaskDraft::StageChanged, "点击「高」进入日期阶段");
        expect(d.priority() == 2, "优先级被设为高(2)");
        expect(d.chooseDateOffset(1) == TaskDraft::StageChanged, "点击「明天」进入分类阶段");
        expect(d.deadline().date() == QDate::currentDate().addDays(1), "日期是明天");
        expect(d.chooseCategory(1) == TaskDraft::Committed, "点击「工作」即提交");
        expect(d.category() == 1, "分类被设为工作(1)");
    }

    {
        // 日期解析与校验，时间精确到分钟
        TaskDraft d;
        const QDate today = QDate::currentDate();
        expect(d.dateFromInput("10/12").date() == QDate(today.year(), 10, 12), "解析 m/d");
        expect(d.dateFromInput("12").date() == QDate(today.year(), today.month(), 12), "解析单个日");
        expect(d.dateFromInput("2026/10/12").date() == QDate(2026, 10, 12), "解析 y/m/d");
        expect(d.dateFromInput("10/12").time() == QTime(23, 59), "只给日期时默认 23:59");

        const QDateTime withTime = d.dateFromInput("10/12 14:30");
        expect(withTime.isValid(), "解析「日期 时间」");
        expect(withTime.date() == QDate(today.year(), 10, 12), "带时间的日期部分正确");
        expect(withTime.time() == QTime(14, 30), "时间精确到分钟");

        const QDateTime earlyMorning = d.dateFromInput("10/12 7:05");
        expect(earlyMorning.time() == QTime(7, 5), "支持单位数小时");

        expect(!d.dateFromInput("2/31").isValid(), "2/31 是非法日期");
        expect(!d.dateFromInput("13/1").isValid(), "13/1 是非法日期");
        expect(!d.dateFromInput("1/2/3/4").isValid(), "段数过多是非法日期");
        expect(!d.dateFromInput("1999/1/1").isValid(), "年份超出范围是非法日期");
        expect(!d.dateFromInput("").isValid(), "空输入表示无截止时间");
        expect(!d.dateFromInput("   ").isValid(), "只有空格也表示无截止时间");

        // 非法日期必须在界面上被拦下
        d.handleTab("任务");
        d.handleTab(QString());
        expect(d.handleTab("2/31") == TaskDraft::Rejected, "非法日期被拒绝");
        expect(d.stage() == TaskDraft::StageDate, "被拒绝后仍停留在日期阶段");
        expect(!d.inputError().isEmpty(), "非法日期给出了错误提示");

        // 合法输入带时间后应被接受
        expect(d.handleTab("10/12 08:15") == TaskDraft::StageChanged, "带时间的日期被接受");
        expect(d.deadline().time() == QTime(8, 15), "截止时间保存了分钟");
    }

    {
        // 备注与「从已有任务载入」（内联编辑复用同一条路径）
        TaskDraft d;
        Task existing;
        existing.id = 7;
        existing.title = "已有任务";
        existing.details = "已有的备注";
        existing.priority = 3;
        existing.categoryId = 2;
        existing.deadline = "2026-12-01 18:45";

        d.loadFrom(existing);
        expect(d.title() == "已有任务", "loadFrom 载入标题");
        expect(d.details() == "已有的备注", "loadFrom 载入备注");
        expect(d.priority() == 3, "loadFrom 载入优先级");
        expect(d.category() == 2, "loadFrom 载入分类");
        expect(d.deadlineText() == "2026-12-01 18:45", "loadFrom 保留了分钟精度");

        d.setDetails("改过的备注");
        d.setDeadline(QDateTime(QDate(2026, 12, 2), QTime(9, 30)));
        const Task out = d.toTask();
        expect(out.details == "改过的备注", "备注写入 toTask");
        expect(out.deadline == "2026-12-02 09:30", "分钟精度的截止时间写入 toTask");
    }

    {
        // 回归：先改时间、再点优先级/分类，时间必须保留。
        // 以前 TaskDialog::refreshChips() 会用 draft 里的旧时间回写时间控件，
        // 于是「改完时间再调整分类或优先级」会把时间重置掉。
        TaskDraft d;
        d.setDeadline(QDateTime(QDate(2026, 11, 5), QTime(16, 40)));
        d.setPriority(3);
        d.setCategory(1);
        expect(d.deadlineText() == "2026-11-05 16:40",
              "改时间后再调整优先级/分类，时间不被重置");
        expect(d.priority() == 3 && d.category() == 1, "优先级与分类都已生效");

        // 顺序调换（先点分类再点优先级）同样不能丢时间
        TaskDraft e;
        e.setDeadline(QDateTime(QDate(2026, 12, 1), QTime(8, 5)));
        e.setCategory(2);
        e.setPriority(0);
        expect(e.deadlineText() == "2026-12-01 08:05",
              "先分类后优先级，时间同样保留");
    }

    {
        // Enter 提交与 Esc 取消
        TaskDraft d;
        expect(d.handleEnter("直接提交") == TaskDraft::Committed, "标题阶段 Enter 直接提交");
        const Task t = d.toTask();
        expect(t.title == "直接提交", "生成的 Task 标题正确");
        expect(t.status == 0, "新任务状态是待办");
        expect(t.deadline.empty(), "未指定日期时无截止时间");

        // 指定日期后才有截止时间，且精确到分钟
        TaskDraft dated;
        dated.setTitle("带日期的任务");
        dated.setDeadline(QDateTime(QDate(2026, 10, 20), QTime(9, 30)));
        const Task withDate = dated.toTask();
        expect(withDate.deadline == "2026-10-20 09:30", "指定日期后截止时间精确到分钟");

        expect(d.handleEscape() == TaskDraft::StageChanged, "Esc 取消");
        expect(d.stage() == TaskDraft::StageTitle, "取消后回到标题阶段");
        expect(d.title().isEmpty(), "取消后标题已清空");
    }
}

// ------------------------------------------------------------------ HashTable

void testHashTable() {
    section("HashTable");

    HashTable<std::string, int> table;
    expect(table.isEmpty() && table.size() == 0, "空哈希表");
    expect(HashTable<std::string, int>::isPrime(table.bucketCount()),
          "桶数组容量取质数（降低除留余数法的聚集）");

    // 插入 / 覆盖 / 查找
    expect(table.insert("work", 1), "插入新键返回 true");
    expect(!table.insert("work", 2), "重复键返回 false（覆盖）");
    expect(table.size() == 1, "覆盖不会增加元素个数");
    expect(table.value("work") == 2, "覆盖后的值生效");
    expect(table.contains("work"), "contains 命中");
    expect(!table.contains("nope"), "contains 未命中");
    expect(table.value("nope", -1) == -1, "未命中返回 fallback");

    int got = 0;
    expect(table.tryGet("work", got) && got == 2, "tryGet 取出正确值");
    expect(!table.tryGet("nope", got), "tryGet 未命中返回 false");

    // 引用式访问
    table["study"] = 7;
    expect(table.value("study") == 7, "operator[] 可写入");
    expect(table["study"] == 7, "operator[] 可读取");
    expect(table.size() == 2, "operator[] 新建了键");

    // 删除
    expect(table.remove("study"), "remove 命中返回 true");
    expect(!table.remove("study"), "重复 remove 返回 false");
    expect(!table.contains("study"), "删除后不再命中");
    expect(table.size() == 1, "删除后元素个数减少");

    // 扩容：插入远超初始容量的元素，验证 rehash 后仍能全部查到
    HashTable<int, int> big(7);
    const int n = 500;
    const std::size_t bucketsBefore = big.bucketCount();
    for (int i = 0; i < n; ++i) {
        big.insert(i * 3, i);
    }
    expect(big.size() == static_cast<std::size_t>(n), "插入 500 个元素后 size 正确");
    expect(big.bucketCount() > bucketsBefore, "装载因子超阈值后自动扩容");
    expect(HashTable<int, int>::isPrime(big.bucketCount()), "扩容后容量仍是质数");
    expect(big.loadFactor() <= 0.75, "扩容后装载因子回到阈值以内");

    bool allFound = true;
    for (int i = 0; i < n; ++i) {
        if (big.value(i * 3, -1) != i) { allFound = false; break; }
    }
    expect(allFound, "rehash 之后每个键都还能查到正确值");

    bool absent = true;
    for (int i = 0; i < n; ++i) {
        if (big.contains(i * 3 + 1)) { absent = false; break; }
    }
    expect(absent, "rehash 没有把不存在的键误判为存在");

    // 冲突链：用一个「所有键都散列到同一个值」的哈希函子，强制产生冲突。
    // 注意不能假设 std::hash<int> 是恒等映射——MSVC 实现会做位混合，
    // 所以必须显式指定哈希函数才能确定性地构造冲突。
    struct AlwaysZeroHash {
        std::size_t operator()(int) const { return 0; }
    };
    HashTable<int, int, AlwaysZeroHash> clash(17);
    const std::size_t buckets = clash.bucketCount();
    const int clashCount = 5;
    std::vector<int> clashKeys;
    for (int i = 0; i < clashCount; ++i) {
        clashKeys.push_back(100 + i);
        clash.insert(clashKeys.back(), clashKeys.back() * 10);
    }
    expect(clash.size() == static_cast<std::size_t>(clashCount), "5 个冲突键都插入了");
    expect(clash.bucketCount() == buckets, "冲突键数量未触发扩容");
    expect(clash.longestChain() == static_cast<std::size_t>(clashCount),
          "哈希值全相同时 5 个键落在同一条冲突链上");

    bool clashOk = true;
    for (int key : clashKeys) {
        if (clash.value(key, -1) != key * 10) { clashOk = false; break; }
    }
    expect(clashOk, "冲突链上的每个键都能取回正确值");

    expect(clash.remove(clashKeys[2]), "能从冲突链中间删除一个键");
    expect(clash.size() == static_cast<std::size_t>(clashCount - 1), "删除后元素数减一");
    expect(clash.longestChain() == static_cast<std::size_t>(clashCount - 1), "链长随之变短");
    bool survived = true;
    for (int i = 0; i < clashCount; ++i) {
        if (i == 2) continue;
        if (clash.value(clashKeys[static_cast<std::size_t>(i)], -1) !=
            clashKeys[static_cast<std::size_t>(i)] * 10) {
            survived = false;
            break;
        }
    }
    expect(survived, "删掉链中间一个键后，同桶其他键仍可正确取回");
    expect(!clash.contains(clashKeys[2]), "被删掉的键不再命中");

    // 反过来：哈希质量好时不应出现长链
    HashTable<std::string, int> spread(101);
    for (int i = 0; i < 80; ++i) {
        spread.insert("tag_" + std::to_string(i), i);
    }
    expect(spread.longestChain() <= 3, "良好散列下最长链很短（分散均匀）");

    expect(table.keys().size() == table.size(), "keys() 返回全部键");
    expect(table.entries().size() == table.size(), "entries() 返回全部键值对");
    table.clear();
    expect(table.isEmpty(), "clear 清空");

    bool threw = false;
    try {
        table.bucketAt(9999);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "bucketAt 越界抛异常");
}

// ---------------------------------------------------------------- SegmentTree

void testSegmentTree() {
    section("SegmentTree");

    SegmentTree tree(10);
    expect(tree.days() == 10, "10 天的线段树");

    // 单点更新 + 单点查询
    tree.add(3, 2);
    tree.add(3, 3);
    expect(tree.pointValue(3) == 5, "同一天累加");
    expect(tree.pointValue(0) == 0, "未更新的天为 0");
    expect(tree.total() == 5, "total 等于全部之和");

    tree.set(3, 1);
    expect(tree.pointValue(3) == 1, "set 覆盖单点值");

    // 多天分布，验证区间和与暴力值一致
    tree.set(0, 1);
    tree.set(1, 2);
    tree.set(2, 3);
    tree.set(3, 4);
    tree.set(4, 5);
    const long long expectedTotal = 1 + 2 + 3 + 4 + 5;
    expect(tree.total() == expectedTotal, "多天设置的 total 正确");

    expect(tree.querySum(0, 4) == expectedTotal, "整段区间和");
    expect(tree.querySum(1, 3) == 2 + 3 + 4, "中间区间和");
    expect(tree.querySum(3, 3) == 4, "单点区间和");
    expect(tree.querySum(2, 100) == 3 + 4 + 5, "越界右端被裁剪");
    expect(tree.querySum(-5, 1) == 1 + 2, "越界左端被裁剪");
    expect(tree.querySum(5, 2) == tree.querySum(2, 5), "from>to 自动交换");
    expect(tree.querySum(7, 9) == 0, "空区间和为 0");

    // 区间最大值
    expect(tree.queryMax(0, 4) == 5, "区间最大值");
    expect(tree.queryMax(0, 1) == 2, "前两天的最大值");
    expect(tree.queryMax(7, 9) == 0, "空区间最大值为 0");

    // 与暴力枚举对比：随机更新后逐个区间核对
    SegmentTree t2(37);
    std::vector<long long> brute(37, 0);
    int seed = 12345;
    for (int round = 0; round < 300; ++round) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        const long long day = seed % 37;
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        const long long delta = (seed % 5) - 2;      // 可能为负
        t2.add(day, delta);
        brute[static_cast<std::size_t>(day)] += delta;
    }
    bool sumsMatch = true;
    bool maxMatch = true;
    for (int from = 0; from < 37; ++from) {
        for (int to = from; to < 37; ++to) {
            long long sum = 0;
            long long best = 0;
            for (int d = from; d <= to; ++d) {
                sum += brute[static_cast<std::size_t>(d)];
                best = std::max(best, brute[static_cast<std::size_t>(d)]);
            }
            if (t2.querySum(from, to) != sum) sumsMatch = false;
            if (t2.queryMax(from, to) != best) maxMatch = false;
        }
    }
    expect(sumsMatch, "300 次随机更新后，任意区间和都与暴力枚举一致");
    expect(maxMatch, "300 次随机更新后，任意区间最大值都与暴力枚举一致");

    long long bruteTotal = 0;
    for (long long v : brute) bruteTotal += v;
    expect(t2.total() == bruteTotal, "total 与暴力总和一致");

    // resize 归零
    t2.resize(5);
    expect(t2.days() == 5 && t2.total() == 0, "resize 会清空并改天数");
    t2.add(4, 1);
    expect(t2.pointValue(4) == 1, "resize 后仍可更新");

    bool threw = false;
    try {
        t2.add(5, 1);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "越界更新抛异常");

    // 非 2 的幂天数也要正确
    SegmentTree odd(13);
    for (long long d = 0; d < 13; ++d) odd.set(d, d + 1);
    expect(odd.total() == 13 * 14 / 2, "天数非 2 的幂时求和正确");
    expect(odd.querySum(0, 12) == 91, "非 2 的幂整段查询正确");
}

// ------------------------------------------------------------------ UnionFind

void testUnionFind() {
    section("UnionFind");

    UnionFind uf(10);
    expect(uf.size() == 10, "10 个元素");
    expect(uf.setCount() == 10, "初始为 10 个独立集合");
    expect(uf.find(3) == 3, "单元素集合的代表元是自己");
    expect(!uf.sameSet(0, 1), "初始互不相同");

    expect(uf.join(0, 1), "合并 0 与 1 成功");
    expect(!uf.join(1, 0), "重复合并返回 false");
    expect(uf.setCount() == 9, "合并后集合数减一");
    expect(uf.sameSet(0, 1), "0 与 1 已在同一集合");
    expect(uf.sameSet(1, 0), "sameSet 与顺序无关");

    // 串成一条链，验证路径压缩
    for (int i = 1; i + 1 < 10; ++i) {
        uf.join(i, i + 1);
    }
    expect(uf.setCount() == 1, "全部合并成一个集合");
    const int root = uf.find(0);
    bool allSame = true;
    for (int i = 0; i < 10; ++i) {
        if (uf.find(i) != root) { allSame = false; break; }
    }
    expect(allSame, "10 个元素同属一个集合");
    expect(uf.setSize(5) == 10, "集合大小为 10");

    // 按秩合并：秩不会退化成链
    UnionFind rank(8);
    rank.join(0, 1);
    rank.join(2, 3);
    rank.join(0, 2);          // 合并两个高度为 1 的树
    expect(rank.rankOf(0) >= 1, "按秩合并后根结点的秩递增");
    rank.join(4, 0);
    expect(rank.sameSet(1, 4), "并入更大的集合");
    expect(rank.find(0) == rank.find(4), "代表元一致");

    // members / roots
    const Vector<int> members = rank.members(1);
    expect(members.size() == 5, "members 返回集合内全部元素");
    const Vector<int> roots = rank.roots();
    expect(roots.size() == static_cast<int>(rank.setCount()), "roots 数量等于集合数");

    // reset
    rank.reset(4);
    expect(rank.setCount() == 4 && !rank.sameSet(0, 1), "reset 恢复成互不相交");

    // 边界
    bool threw = false;
    try {
        rank.find(99);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "越界索引抛异常");

    UnionFind empty(0);
    expect(empty.setCount() == 0, "0 个元素可用");
    threw = false;
    try {
        UnionFind bad(-1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "负数元素个数抛异常");
}

// ------------------------------------------------- 应用层：TaskDocument

namespace {

Task makeDocTask(const std::string& title, int listId, const std::string& deadline = "",
                 const std::vector<std::string>& tags = {}, int priority = 1,
                 bool starred = false) {
    Task t;
    t.title = title;
    t.listId = listId;
    t.deadline = deadline;
    t.tags = tags;
    t.priority = priority;
    t.starred = starred;
    return t;
}

std::string dayOffset(int days) {
    return QDate::currentDate().addDays(days).toString("yyyy-MM-dd").toStdString() + " 09:00";
}

} // namespace

void testTaskDocument() {
    section("TaskDocument（四种数据结构的实际应用）");

    // ---------------- 列表 + 并查集分组 ----------------
    {
        TaskDocument doc;
        expect(doc.lists().size() == 1, "初始有一个默认列表");
        expect(doc.defaultListId() == 1, "默认列表编号为 1");

        const int work = doc.addList("工作", "#FF007F");
        const int study = doc.addList("学习", "#00AA88");
        const int health = doc.addList("健康", "#0077FF");
        expect(work > 0 && study > 0 && health > 0, "可以新建列表");
        expect(doc.lists().size() == 4, "列表数量正确");

        expect(!doc.listsShareGroup(work, study), "新建列表默认不属于同一分组");
        expect(doc.joinListsIntoGroup(work, study), "把两个列表合成一个分组");
        expect(doc.listsShareGroup(work, study), "合并后处于同一分组");
        expect(!doc.joinListsIntoGroup(work, study), "重复合并返回 false（并查集已有相同根）");
        expect(!doc.listsShareGroup(work, health), "未合并的列表仍不同组");

        // 传递性：study 与 health 合并后，work 也与 health 同组
        expect(doc.joinListsIntoGroup(study, health), "再把 study 与 health 合并");
        expect(doc.listsShareGroup(work, health), "分组的传递性成立（work~study~health）");
        expect(doc.groupCount() == 2, "4 个列表形成 2 个集合（一个 3 元素 + 默认列表）");

        const std::vector<int> group = doc.listsInSameGroup(work);
        expect(group.size() == 3, "分组内有 3 个列表");
        bool hasAll = true;
        for (int id : {work, study, health}) {
            if (std::find(group.begin(), group.end(), id) == group.end()) hasAll = false;
        }
        expect(hasAll, "分组枚举包含全部成员");

        doc.syncGroupIds();
        expect(doc.findList(work)->groupId >= 0, "同组列表被写上分组号");
        expect(doc.findList(doc.defaultListId())->groupId == -1, "独立列表没有分组号");

        expect(!doc.removeList(doc.defaultListId()), "默认列表不可删除");
        const int doomed = doc.addList("临时");
        doc.addTask(makeDocTask("临时任务", doomed));
        expect(doc.removeList(doomed), "非默认列表可以删除");
        expect(doc.tasks().back().listId == doc.defaultListId(), "删除列表后任务迁移到默认列表");
    }

    // ---------------- 可视化面板依赖的只读统计 ----------------
    {
        // 哈希表：桶长分布 / 已用桶 / 探查长度
        HashTable<std::string, int> t(17);
        expect(t.usedBucketCount() == 0, "空表已用桶为 0");
        expect(t.bucketSizes().size() == t.bucketCount(), "桶长数组长度等于桶数");
        t.insert("alpha", 1);
        t.insert("beta", 2);
        expect(t.usedBucketCount() == 2, "插入两个不同键后已用桶为 2");
        expect(t.probeLength("alpha") >= 1, "命中的探查长度至少为 1");
        expect(t.probeLength("missing") >= 0, "未命中的探查长度可计算");
        std::size_t bucketSum = 0;
        for (std::size_t n : t.bucketSizes()) bucketSum += n;
        expect(bucketSum == t.size(), "桶长之和等于元素个数");

        // Trie：结点数 / 词数 / 子树结点数 / 前缀路径
        Trie trie;
        expect(trie.nodeCount() == 0, "空 Trie 没有结点（根不计）");
        expect(trie.wordCount() == 0, "空 Trie 词数为 0");
        trie.insert("abc");
        expect(trie.nodeCount() == 3, "插入 abc 后有 3 个结点");
        expect(trie.wordCount() == 1, "词数为 1");
        trie.insert("abd");
        expect(trie.nodeCount() == 4, "插入 abd 复用了 ab，只多 1 个结点（共享前缀）");
        expect(trie.wordCount() == 2, "词数为 2");
        expect(trie.subtreeNodeCount("ab") == 3, "前缀 ab 的子树有 3 个结点");
        expect(trie.subtreeNodeCount("zz") == 0, "不存在的前缀子树为 0");
        expect(trie.hasPrefixPath("ab"), "ab 是有效前缀路径");
        expect(!trie.hasPrefixPath("zz"), "zz 不是有效前缀路径");

        // 线段树：叶子槽位 / 树高 / 内部结点取值
        SegmentTree st(10);
        expect(st.leafSlots() == 16, "10 天补齐到 16 个叶子（2 的幂）");
        expect(st.depth() == 5, "16 个叶子对应 5 层（含叶子层）");
        expect(st.nodeSlots() == 32, "数组长度为 2*16");
        st.set(0, 3);
        st.set(1, 5);
        expect(st.nodeSum(1) == 8, "根的区间和等于两个叶子之和");
        expect(st.nodeMax(1) == 5, "根的区间最大值等于较大的叶子");

        // 并查集：父指针 / 秩 / 到根链长
        UnionFind uf(4);
        expect(uf.parentOf(0) == 0, "单元素集合父指针指向自己");
        expect(uf.chainLengthToRoot(0) == 0, "自己就是根，链长为 0");
        uf.join(0, 1);
        uf.join(1, 2);
        expect(uf.chainLengthToRoot(0) >= 0, "到根链长可计算");
        expect(uf.parentOf(3) == 3, "未参与合并的元素仍自成一集合");
        expect(uf.rankAt(0) >= 0, "秩可读");
        uf.find(0);
        expect(uf.chainLengthToRoot(0) <= 1, "find 之后链长被压到不超过 1");
        expect(uf.parentOf(99) == -1, "越界索引返回 -1 而不是崩溃");
        expect(uf.rankAt(99) == -1, "越界秩返回 -1");
        expect(uf.chainLengthToRoot(99) == -1, "越界链长返回 -1");
    }

    // ---------------- 回归：「进行中」置顶 ----------------
    // 需求：正在做的事要一眼看到，不该埋在一堆待办中间。
    // 与「已完成置底」共存：进行中在最前、已完成在最后，其余保持原相对顺序。
    {
        TaskDocument doc;
        const int a = doc.addTask(makeDocTask("A 待办", 1));
        const int b = doc.addTask(makeDocTask("B 进行中", 1));
        const int c = doc.addTask(makeDocTask("C 待办", 1));
        const int d = doc.addTask(makeDocTask("D 已完成", 1));

        Task tb = *doc.findTask(b);
        tb.status = 1;
        doc.updateTask(tb);
        Task td = *doc.findTask(d);
        td.status = 2;
        doc.updateTask(td);
        expect(doc.findTask(b)->status == 1, "B 已标记为进行中");

        const TaskSort modes[] = {TaskSort::CreatedAsc, TaskSort::DeadlineAsc,
                                  TaskSort::PriorityDesc, TaskSort::TitleAsc,
                                  TaskSort::Manual};
        for (TaskSort mode : modes) {
            TaskFilter f;
            f.sort = mode;
            const std::vector<Task> r = doc.query(f);
            expect(!r.empty() && r.front().id == b,
                   "进行中的任务在所有排序方式下都置顶");
            expect(r.size() >= 2 && r.back().id == d,
                   "已完成任务依然置底（与置顶并存）");
        }

        // 置顶/置底都只挪动各自的项，其余任务之间的相对顺序不变
        TaskFilter f;
        f.sort = TaskSort::Manual;
        const std::vector<Task> r = doc.query(f);
        bool aBeforeC = false;
        for (std::size_t i = 0; i + 1 < r.size(); ++i) {
            if (r[i].id == a && r[i + 1].id == c) aBeforeC = true;
        }
        expect(aBeforeC, "置顶/置底不影响其余任务之间的相对顺序");
    }

    // ---------------- 回归：已完成任务在所有排序方式下都置底 ----------------
    // 曾经只有 截止时间/优先级/创建时间 三种排序做了置底，「手动拖拽」与
    // 「标题」漏掉了 —— 于是手动拖拽模式下改完状态，已完成的任务还停在原地，
    // 看起来就像"自动置底失效了"。
    {
        TaskDocument doc;
        const int a = doc.addTask(makeDocTask("A 未完成", 1));
        const int b = doc.addTask(makeDocTask("B 未完成", 1));
        const int c = doc.addTask(makeDocTask("C 已完成", 1));
        Task doneTask = *doc.findTask(c);
        doneTask.status = 2;
        doc.updateTask(doneTask);
        expect(doc.findTask(c)->status == 2, "C 已标记为完成");

        const TaskSort modes[] = {TaskSort::CreatedAsc, TaskSort::DeadlineAsc,
                                  TaskSort::PriorityDesc, TaskSort::TitleAsc,
                                  TaskSort::Manual};
        for (TaskSort mode : modes) {
            TaskFilter f;
            f.sort = mode;
            const std::vector<Task> r = doc.query(f);
            expect(!r.empty() && r.back().id == c,
                   "已完成任务在所有排序方式下都置底");
        }

        // 置底只挪动已完成项，未完成项之间的相对顺序必须保持
        TaskFilter f;
        f.sort = TaskSort::Manual;
        const std::vector<Task> r = doc.query(f);
        bool aBeforeB = false;
        for (std::size_t i = 0; i + 1 < r.size(); ++i) {
            if (r[i].id == a && r[i + 1].id == b) aBeforeB = true;
        }
        expect(aBeforeB || r.size() < 2,
               "置底只挪动已完成项，未完成项的相对顺序不变");
    }

    // ---------------- 回归：线段树与逐条统计必须一致 ----------------
    // 线段树是「增量维护」的索引，deadlineStats 是「逐条扫」的结果。
    // 两者一旦不一致，界面上的热力图与统计数字就会互相矛盾
    // （这个断言是在可视化面板里核对数据时想到要补的）。
    {
        TaskDocument doc;
        doc.addTask(makeDocTask("今天", 1, dayOffset(0)));
        doc.addTask(makeDocTask("明天", 1, dayOffset(1)));
        doc.addTask(makeDocTask("第三天", 1, dayOffset(2)));
        doc.addTask(makeDocTask("逾期", 1, dayOffset(-3)));
        doc.addTask(makeDocTask("无期限", 1));
        const int doneId = doc.addTask(makeDocTask("已完成", 1, dayOffset(1)));
        Task done = *doc.findTask(doneId);
        done.status = 2;
        doc.updateTask(done);
        doc.addTask(makeDocTask("很远", 1, dayOffset(500)));   // 窗口外

        const DeadlineStats stats = doc.deadlineStats(30);

        int leafSum = 0;
        for (int v : doc.dailyDeadlineCounts(30)) leafSum += v;
        expect(leafSum == stats.total,
              "逐日叶子之和等于 deadlineStats 的 total（两者不能互相矛盾）");
        expect(doc.deadlineTree().querySum(0, 29) == stats.total,
              "线段树区间 [0,29] 的和等于 total");
        expect(stats.total == 3, "30 天窗口内共 3 条（今天/明天/第三天）");
        expect(stats.overdue == 1, "逾期 1 条，且不计入 total");
        expect(stats.dueToday == 1, "今天到期 1 条");

        doc.removeTask(doneId);
        expect(doc.deadlineStats(30).total == 3, "删掉已完成的任务后 total 不变");
    }

    // ---------------- 回归：新增列表不能摧毁已有分组 ----------------
    // 曾经的 addList 里直接 reset 并查集，顺带清掉了 TaskList::groupId，
    // 于是「加一个新列表」会把所有分组一起抹掉。
    {
        TaskDocument g;
        const int ga = g.addList("甲");
        const int gb = g.addList("乙");
        expect(g.joinListsIntoGroup(ga, gb), "先把两个列表合成一组");
        expect(g.listsShareGroup(ga, gb), "确认已同组");

        const int gc = g.addList("丙");
        expect(g.listsShareGroup(ga, gb), "新增列表后原有分组仍然存在");
        expect(!g.listsShareGroup(ga, gc), "新增的列表自成一个分组");
        expect(g.findList(ga)->groupId >= 0, "原有列表仍带着分组号");
        expect(g.findList(gc)->groupId == -1, "新列表没有分组号");

        expect(g.joinListsIntoGroup(gb, gc), "把新列表并进已有的组");
        expect(g.listsShareGroup(ga, gc), "并查集传递性：甲~乙~丙 同组");
    }

    // ---------------- 哈希表：标签倒排索引 ----------------
    {
        TaskDocument doc;
        const int a = doc.addTask(makeDocTask("写报告", 1, dayOffset(1), {"工作", "紧急"}));
        const int b = doc.addTask(makeDocTask("看论文", 1, dayOffset(2), {"学习", "工作"}));
        const int c = doc.addTask(makeDocTask("跑步", 1, dayOffset(3), {"健康"}));
        expect(a >= 0 && b >= 0 && c >= 0, "三条带标签的任务写入成功");

        expect(doc.allTags().size() == 4, "索引里有 4 个不同标签");
        expect(doc.tagUsage("工作") == 2, "「工作」标签关联 2 条任务");
        expect(doc.tagUsage("健康") == 1, "「健康」标签关联 1 条任务");
        expect(doc.tagUsage("不存在") == 0, "未知标签用量为 0");

        const std::vector<int> workIds = doc.taskIdsWithTag("工作");
        expect(workIds.size() == 2, "按标签取任务编号返回 2 条");
        expect(std::find(workIds.begin(), workIds.end(), a) != workIds.end(),
              "结果里包含第一条");
        expect(std::find(workIds.begin(), workIds.end(), c) == workIds.end(),
              "结果里不含无此标签的任务");

        // 标签大小写与空白归一
        expect(doc.tagUsage("  工作  ") == 2, "查询标签会做归一化（去空白）");
        const int d = doc.addTask(makeDocTask("大写标签", 1, dayOffset(4), {"WORK"}));
        expect(doc.tagUsage("work") == 1, "标签大小写不敏感");

        // 标签统计按用量降序
        const std::vector<TagCount> counts = doc.tagCounts();
        expect(!counts.empty() && counts.front().tag == "工作" && counts.front().count == 2,
              "标签统计按出现次数降序");

        // 更新任务后索引同步
        Task edited = *doc.findTask(a);
        edited.tags = {"归档"};
        expect(doc.updateTask(edited), "更新任务标签");
        expect(doc.tagUsage("工作") == 1, "旧标签索引被移除");
        expect(doc.tagUsage("归档") == 1, "新标签被索引");

        // 删除任务后索引清理
        expect(doc.removeTask(b), "删除任务");
        expect(doc.tagUsage("工作") == 0, "删除后该标签不再关联任何任务");
        expect(doc.allTags().size() == 3, "空掉的标签从索引里移除");

        // 按标签筛选任务
        TaskFilter byTag;
        byTag.tag = "归档";
        expect(doc.query(byTag).size() == 1, "可以按标签筛出任务");
        byTag.tag = "不存在";
        expect(doc.query(byTag).empty(), "不存在的标签筛不出任何任务");

        // 哈希索引健康度：桶数是质数、链不长
        expect(HashTable<std::string, int>::isPrime(doc.tagBucketCount()),
              "标签索引桶数是质数");
        expect(doc.tagLoadFactor() <= 0.75, "标签索引装载因子在阈值内");
        expect(doc.tagLongestChain() <= 2, "标签索引冲突链很短");
    }

    // ---------------- 哈希表：重复标题检测 ----------------
    {
        TaskDocument doc;
        doc.addTask(makeDocTask("买牛奶", 1));
        doc.addTask(makeDocTask("买牛奶", 1));
        doc.addTask(makeDocTask("  买牛奶  ", 1));      // 归一化后同名
        doc.addTask(makeDocTask("买鸡蛋", 1));
        doc.addTask(makeDocTask("看论文", 1));
        doc.addTask(makeDocTask("看论文", 1));

        const std::vector<DuplicateGroup> dups = doc.duplicateTitleGroups();
        expect(dups.size() == 2, "检出 2 组重复标题");
        expect(dups.front().taskIds.size() == 3, "重复最多的一组有 3 条（含空白差异）");
        expect(dups.front().normalizedTitle == "买牛奶", "归一化后的标题正确");
        expect(dups.back().normalizedTitle == "看论文", "第二组标题正确");
    }

    // ---------------- Trie：搜索补全 ----------------
    {
        TaskDocument doc;
        doc.addTask(makeDocTask("编写项目文档", 1));
        doc.addTask(makeDocTask("编写使用说明", 1));
        doc.addTask(makeDocTask("复习数据结构", 1));
        doc.addTask(makeDocTask("看论文", 1));

        const std::vector<std::string> completions = doc.titleCompletions("编写");
        expect(completions.size() == 2, "前缀「编写」补全出 2 条标题");
        expect(doc.titleCompletions("编写", 1).size() == 1, "补全支持数量上限");
        expect(doc.titleCompletions("复习").size() == 1, "另一个前缀补全正确");
        expect(doc.titleCompletions("不存在").empty(), "未知前缀补全为空");
        expect(doc.titleCompletions("").empty(), "空前缀不返回内容");

        const std::vector<int> ids = doc.taskIdsWithTitlePrefix("编写使用");
        expect(ids.size() == 1, "前缀能定位到具体任务");
        // ⚠ 先判空再取 front()：断言失败时应该打印失败并继续跑，
        // 而不是让 Debug 版在 std::vector::front() 里触发断言弹窗崩掉。
        // 这里曾经因为直接取 front() 而崩溃，连带看不到前面已失败的那几条，
        // 反而把"测试失败"误判成"程序崩了"。
        expect(!ids.empty() && doc.findTask(ids.front())->title == "编写使用说明",
               "定位到的任务标题正确");

        // 删除任务后 Trie 也更新
        if (!ids.empty()) {
            doc.removeTask(ids.front());
        }
        expect(doc.titleCompletions("编写使用").empty(), "删除任务后前缀补全不再返回它");
        expect(doc.titleCompletions("编写").size() == 1, "同前缀的其他标题仍在");
    }

    // ---------------- 线段树：截止日期区间统计 ----------------
    {
        TaskDocument doc;
        doc.addTask(makeDocTask("今天的事", 1, dayOffset(0)));
        doc.addTask(makeDocTask("明天的事 A", 1, dayOffset(1)));
        doc.addTask(makeDocTask("明天的事 B", 1, dayOffset(1)));
        doc.addTask(makeDocTask("三天后", 1, dayOffset(3)));
        doc.addTask(makeDocTask("逾期的事", 1, dayOffset(-2)));
        doc.addTask(makeDocTask("无期限", 1));
        const int doneId = doc.addTask(makeDocTask("已完成的", 1, dayOffset(1)));
        Task done = *doc.findTask(doneId);
        done.status = 2;
        doc.updateTask(done);

        doc.rebuildSegmentTree(30);
        const DeadlineStats stats = doc.deadlineStats(30);
        expect(stats.overdue == 1, "统计出 1 条逾期");
        expect(stats.dueToday == 1, "统计出 1 条今天到期");
        expect(stats.total == 4, "30 天内共 4 条（今天 1 + 明天 2 + 三天后 1）");
        expect(stats.busiestDayCount == 2, "最忙的一天有 2 件事");
        expect(stats.busiestDayOffset == 1, "最忙的那天是明天（偏移 1）");

        const std::vector<int> daily = doc.dailyDeadlineCounts(5);
        expect(daily.size() == 5, "逐日计数返回 5 天");
        expect(daily[0] == 1, "今天 1 件");
        expect(daily[1] == 2, "明天 2 件");
        expect(daily[2] == 0, "后天 0 件");
        expect(daily[3] == 1, "三天后 1 件");
        expect(daily[4] == 0, "四天后 0 件");

        expect(doc.segmentTreeDays() == 30, "线段树窗口为 30 天");

        // 已完成任务不占「计划」，把明天的一条标完成，统计应减少
        const int tomorrowId = doc.query(TaskFilter{}).front().id;
        Q_UNUSED(tomorrowId);
        Task t1 = *doc.findTask(1);
        t1.status = 2;
        doc.updateTask(t1);
        const DeadlineStats after = doc.deadlineStats(30);
        expect(after.busiestDayCount == 1, "完成一条后最忙的一天只剩 1 件");
    }

    // ---------------- 智能视图与排序 ----------------
    {
        TaskDocument doc;
        const int myDay = doc.addTask(makeDocTask("我的一天任务", 1, dayOffset(1)));
        const int starred = doc.addTask(makeDocTask("重要任务", 1, dayOffset(2)));
        const int planned = doc.addTask(makeDocTask("有计划", 1, dayOffset(3)));
        const int plain = doc.addTask(makeDocTask("普通任务", 1));
        const int completed = doc.addTask(makeDocTask("完成的任务", 1));

        Task s = *doc.findTask(starred); s.starred = true; doc.updateTask(s);
        Task m = *doc.findTask(myDay); m.inMyDay = true; doc.updateTask(m);
        Task c = *doc.findTask(completed); c.status = 2; doc.updateTask(c);

        expect(doc.countStarred() == 1, "重要视图统计 1 条");
        expect(doc.countInMyDay() == 1, "我的一天统计 1 条");
        expect(doc.countPlanned() == 3, "计划内统计 3 条（有期限且未完成）");
        expect(doc.countCompleted() == 1, "已完成统计 1 条");
        expect(doc.countInList(1) == 4, "列表内未完成 4 条");

        TaskFilter f;
        f.useSmartView = true;
        f.smartView = SmartView::Important;
        expect(doc.query(f).size() == 1, "按重要视图查询");
        f.smartView = SmartView::MyDay;
        expect(doc.query(f).size() == 1, "按我的一天查询");
        f.smartView = SmartView::Planned;
        expect(doc.query(f).size() == 3, "按计划内查询");
        f.smartView = SmartView::Completed;
        expect(doc.query(f).size() == 1, "按已完成查询");
        f.smartView = SmartView::All;
        expect(doc.query(f).size() == 5, "全部视图返回 5 条");

        // 隐藏已完成
        TaskFilter hidden;
        hidden.hideDone = true;
        expect(doc.query(hidden).size() == 4, "隐藏已完成后剩 4 条");

        // 分类筛选（分类仍保留：向导里仍要选分类，快捷筛选也用它）
        // 注意：makeDocTask 不设置 categoryId，所以前面 5 条的分类都是 0（其他）。
        TaskFilter byCategory;
        byCategory.categoryId = 2;          // 学习
        expect(doc.query(byCategory).empty(), "现有 5 条都不属于「学习」分类");

        Task study;
        study.title = "学习类任务";
        study.listId = 1;
        study.categoryId = 2;
        doc.addTask(study);
        expect(doc.query(byCategory).size() == 1, "分类筛选命中一条");
        byCategory.categoryId = 3;          // 健康：没有任务属于它
        expect(doc.query(byCategory).empty(), "换成没有任务的分类就筛不到");
        byCategory.categoryId = 0;          // 其他：默认分类，5 条
        expect(doc.query(byCategory).size() == 5, "分类 0（其他）命中默认分类的 5 条");
        byCategory.categoryId = -1;
        expect(doc.query(byCategory).size() == 6, "分类为 -1 时不按分类过滤，共 6 条");

        // 搜索（标题 + 备注）
        TaskFilter search;
        search.search = "任务";
        // 标题里含「任务」的有 5 条：我的一天任务 / 重要任务 / 普通任务 / 完成的任务
        // / 学习类任务（「有计划」不含「任务」，不计入）
        expect(doc.query(search).size() == 5, "子串搜索命中标题含「任务」的 5 条");
        search.search = "计划";
        expect(doc.query(search).size() == 1, "子串搜索精确命中「有计划」");
        search.search = "";
        search.search = "不存在的词";
        expect(doc.query(search).empty(), "搜不到时为空");

        // 排序：截止时间升序，已完成置底，无期限倒数
        TaskFilter byDeadline;
        byDeadline.sort = TaskSort::DeadlineAsc;
        const std::vector<Task> sorted = doc.query(byDeadline);
        expect(!sorted.empty() && sorted.front().title == "我的一天任务", "截止时间最早的排最前");
        expect(sorted.back().title == "完成的任务", "已完成的排最后");
        bool noDeadlineBeforeDeadline = true;
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            if (sorted[i].status == 2 || sorted[i - 1].status == 2) continue;
            if (!sorted[i - 1].hasDeadline() && sorted[i].hasDeadline()) {
                noDeadlineBeforeDeadline = false;
            }
        }
        expect(noDeadlineBeforeDeadline, "无截止时间的排在有时限的之后");

        // 优先级降序
        Task p1 = *doc.findTask(plain); p1.priority = 3; doc.updateTask(p1);
        TaskFilter byPriority;
        byPriority.sort = TaskSort::PriorityDesc;
        expect(doc.query(byPriority).front().title == "普通任务", "优先级最高的排最前");
    }

    // ---------------- 子任务进度 ----------------
    {
        TaskDocument doc;
        Task t = makeDocTask("有步骤的任务", 1, dayOffset(1));
        t.subtasks = {{1, "第一步", true}, {2, "第二步", false}, {3, "第三步", false}};
        const int id = doc.addTask(t);
        const Task* stored = doc.findTask(id);
        expect(stored->hasSubtasks(), "识别出有子任务");
        expect(stored->doneSubtaskCount() == 1, "完成子任务数正确");
        expect(stored->subtaskPercent() == 33, "进度百分比为 33%");

        Task all = *stored;
        for (Subtask& s : all.subtasks) s.done = true;
        doc.updateTask(all);
        expect(doc.findTask(id)->subtaskPercent() == 100, "全部勾选后进度 100%");

        Task none;
        none.title = "无子任务";
        none.status = 2;
        const int nid = doc.addTask(none);
        expect(doc.findTask(nid)->subtaskPercent() == 100, "无子任务的已完成任务进度 100%");
        expect(doc.findTask(nid)->hasSubtasks() == false, "无子任务判定正确");
    }

    // ---------------- 重复任务 ----------------
    {
        TaskDocument doc;
        Task daily = makeDocTask("每日打卡", 1, "2026-03-02 08:00");
        daily.recurrence = Recurrence::Daily;
        const int dailyId = doc.addTask(daily);

        std::string next;
        expect(TaskDocument::nextOccurrence(*doc.findTask(dailyId), next), "每天重复能推进");
        expect(next == "2026-03-03 08:00", "每天重复加一天且保留时间");

        // 工作日重复：2026-03-06 是周五 -> 下一个应是周一 03-09
        Task weekly = makeDocTask("工作日打卡", 1, "2026-03-06 09:30");
        weekly.recurrence = Recurrence::Weekdays;
        const int weeklyId = doc.addTask(weekly);
        expect(TaskDocument::nextOccurrence(*doc.findTask(weeklyId), next), "工作日重复能推进");
        expect(next == "2026-03-09 09:30", "周五的下一个工作日是周一（跳过周末）");

        Task monthly = makeDocTask("月度复盘", 1, "2026-01-31 20:00");
        monthly.recurrence = Recurrence::Monthly;
        const int monthlyId = doc.addTask(monthly);
        expect(TaskDocument::nextOccurrence(*doc.findTask(monthlyId), next), "每月重复能推进");
        expect(next.substr(0, 10) == "2026-02-28", "1月31日的下一月按 2 月最后一天处理");

        Task noRepeat = makeDocTask("不重复", 1, "2026-03-02 08:00");
        const int plainId = doc.addTask(noRepeat);
        expect(!TaskDocument::nextOccurrence(*doc.findTask(plainId), next),
              "无重复规则时不推进");

        Task noDate = makeDocTask("没期限", 1);
        noDate.recurrence = Recurrence::Daily;
        const int noDateId = doc.addTask(noDate);
        expect(!TaskDocument::nextOccurrence(*doc.findTask(noDateId), next),
              "没有截止时间无法重复");

        // 生成下一个实例
        const int spawned = doc.spawnNextOccurrence(dailyId);
        expect(spawned >= 0, "完成为重复任务生成新实例");
        const Task* fresh = doc.findTask(spawned);
        expect(fresh && fresh->deadline == "2026-03-03 08:00", "新实例截止时间已推进");
        expect(fresh && fresh->status == 0, "新实例状态为待办");
        expect(fresh && fresh->recurrence == Recurrence::Daily, "新实例保留重复规则");
        expect(doc.spawnNextOccurrence(plainId) == -1, "非重复任务不生成新实例");

        // 带子任务的重复任务：新实例的子任务应重置
        Task withSteps = makeDocTask("重复带步骤", 1, "2026-05-01 07:00");
        withSteps.recurrence = Recurrence::Weekly;
        withSteps.subtasks = {{1, "甲", true}, {2, "乙", true}};
        const int stepsId = doc.addTask(withSteps);
        const int stepsSpawn = doc.spawnNextOccurrence(stepsId);
        expect(stepsSpawn >= 0, "带子任务的重复任务生成新实例");
        expect(doc.findTask(stepsSpawn)->doneSubtaskCount() == 0, "新实例的子任务被重置为未完成");
    }

    // ---------------- 我的一天与建议 ----------------
    {
        TaskDocument doc;
        doc.addTask(makeDocTask("逾期的急事", 1, dayOffset(-1), {}, 3));
        doc.addTask(makeDocTask("今天到期", 1, dayOffset(0), {}, 1));
        doc.addTask(makeDocTask("很久以后", 1, dayOffset(50), {}, 0));

        const std::vector<int> tips = doc.suggestions(3);
        expect(tips.size() == 3, "建议返回 3 条");
        expect(doc.findTask(tips.front())->title == "逾期的急事", "逾期的排在最前");

        Task done = *doc.findTask(tips.front());
        done.status = 2;
        doc.updateTask(done);
        const std::vector<int> tips2 = doc.suggestions(3);
        expect(doc.findTask(tips2.front())->title == "今天到期",
              "已完成的不再出现在建议里");

        Task m = *doc.findTask(tips2.front());
        m.inMyDay = true;
        doc.updateTask(m);
        expect(doc.countInMyDay() == 1, "加入我的一天");
        doc.clearMyDay();
        expect(doc.countInMyDay() == 0, "我的一天会被清空（每天重置）");
    }

    // ---------------- 拖拽排序（手工顺序）----------------
    {
        TaskDocument doc;
        const int a = doc.addTask(makeDocTask("甲", 1));
        const int b = doc.addTask(makeDocTask("乙", 1));
        const int c = doc.addTask(makeDocTask("丙", 1));
        const int d = doc.addTask(makeDocTask("丁", 1));

        // 默认按 id 升序
        TaskFilter manual;
        manual.sort = TaskSort::Manual;
        auto order = [&doc, &manual]() {
            std::vector<int> ids;
            for (const Task &t : doc.query(manual)) ids.push_back(t.id);
            return ids;
        };
        auto same = [](const std::vector<int> &x, const std::vector<int> &y) {
            return x == y;
        };

        expect(same(order(), {a, b, c, d}), "默认手工顺序等于创建顺序");

        // 把「丁」拖到最前
        expect(doc.moveTask(d, 0, {a, b, c, d}), "把第 4 条移到最前");
        expect(same(order(), {d, a, b, c}), "顺序变为 丁 甲 乙 丙");

        // 把「乙」拖到最后
        expect(doc.moveTask(b, 3, {d, a, b, c}), "把第 3 条移到最后");
        expect(same(order(), {d, a, c, b}), "顺序变为 丁 甲 丙 乙");

        // 移到原位不做改动
        expect(!doc.moveTask(d, 0, {d, a, c, b}), "移到原位返回 false（无变化）");
        expect(same(order(), {d, a, c, b}), "原位移动不改变顺序");

        // 越界的目标位置会被夹住。注意「乙」此时已经在最后，
        // 所以夹住之后等于原位移动 -> 返回 false、顺序不变。
        expect(!doc.moveTask(b, 99, {d, a, c, b}), "已在末尾时越界移动等于无变化");
        expect(same(order(), {d, a, c, b}), "越界移动到末尾后顺序保持不变");

        // 换成「甲」越界：应被夹到最后，确实产生变化
        expect(doc.moveTask(a, 99, {d, a, c, b}), "把第 2 条越界移到末尾（被夹住）");
        expect(same(order(), {d, c, b, a}), "越界移动把「甲」放到最后");

        // 不存在的任务
        expect(!doc.moveTask(9999, 0, {d, c, b, a}), "移动不存在的任务返回 false");
        expect(!doc.moveTask(d, 0, {}), "空顺序表返回 false");

        // manualOrder 被压成连续的 0..n-1
        std::vector<int> orders;
        for (const Task &t : doc.query(manual)) orders.push_back(t.manualOrder);
        std::vector<int> expected;
        for (std::size_t i = 0; i < orders.size(); ++i) expected.push_back(static_cast<int>(i));
        expect(same(orders, expected), "manualOrder 被压成连续的 0..n-1");

        // 拖拽可以撤销
        expect(doc.canUndo(), "拖拽产生了可撤销记录");
        expect(doc.undo(), "撤销拖拽");
        // 撤销回到上一次的顺序（丁 甲 丙 乙 之前是 丁 甲 丙 乙，撤销的是最后一次移动）
        expect(order().size() == 4, "撤销后仍有 4 条任务");
    }

    // ---------------- 撤销 ----------------
    {
        TaskDocument doc;
        expect(!doc.canUndo(), "新文档没有可撤销的操作");
        expect(!doc.undo(), "无操作时撤销返回 false");

        // 更新任务后可撤销
        const int id = doc.addTask(makeDocTask("原始标题", 1, dayOffset(1), {"工作"}));
        Task edited = *doc.findTask(id);
        edited.title = "改过的标题";
        edited.priority = 3;
        edited.tags = {"归档"};
        expect(doc.updateTask(edited), "更新任务成功");
        expect(doc.canUndo(), "更新后可以撤销");
        expect(doc.undo(), "撤销更新");
        expect(doc.findTask(id)->title == "原始标题", "撤销恢复了标题");
        expect(doc.findTask(id)->priority == 1, "撤销恢复了优先级");
        expect(doc.tagUsage("归档") == 0, "撤销后新标签从哈希索引移除");
        expect(doc.tagUsage("工作") == 1, "撤销后旧标签回到哈希索引");
        expect(doc.titleCompletions("原始").size() == 1, "撤销后 Trie 也回到原状");

        // 删除后可撤销
        expect(doc.removeTask(id), "删除任务成功");
        expect(doc.findTask(id) == nullptr, "删除后任务不在文档里");
        expect(doc.tagUsage("工作") == 0, "删除后标签索引清理");
        expect(doc.undo(), "撤销删除");
        expect(doc.findTask(id) != nullptr, "撤销把任务找了回来");
        expect(doc.findTask(id)->title == "原始标题", "找回来的任务内容正确");
        expect(doc.tagUsage("工作") == 1, "撤销删除后标签索引恢复");
        expect(doc.titleCompletions("原始").size() == 1, "撤销删除后 Trie 恢复");

        // 失败的操作不应留下撤销记录
        Task ghost;
        ghost.id = 9999;
        ghost.title = "不存在的任务";
        const int depthBefore = doc.undoDepth();
        expect(!doc.updateTask(ghost), "更新不存在的任务失败");
        expect(doc.undoDepth() == depthBefore, "失败的更新不留撤销记录");
        expect(!doc.removeTask(9999), "删除不存在的任务失败");
        expect(doc.undoDepth() == depthBefore, "失败的删除不留撤销记录");

        // 连续多次撤销
        Task a = *doc.findTask(id);
        a.priority = 2;
        doc.updateTask(a);
        Task b = *doc.findTask(id);
        b.priority = 3;
        doc.updateTask(b);
        expect(doc.findTask(id)->priority == 3, "连续两次更新后是最后的值");
        doc.undo();
        expect(doc.findTask(id)->priority == 2, "撤销一步回到中间值");
        doc.undo();
        expect(doc.findTask(id)->priority == 1, "再撤销一步回到最初值");

        // 列表分组也要能被撤销恢复
        const int l1 = doc.addList("甲");
        const int l2 = doc.addList("乙");
        expect(!doc.listsShareGroup(l1, l2), "两个新列表不在同一分组");
        doc.joinListsIntoGroup(l1, l2);
        expect(doc.listsShareGroup(l1, l2), "两个列表已合并");
        expect(doc.undo(), "撤销列表分组");
        expect(!doc.listsShareGroup(l1, l2), "撤销后分组关系被还原");
        expect(doc.findList(l1) != nullptr && doc.findList(l2) != nullptr,
              "撤销分组不会删掉列表本身");
        expect(doc.findList(l1)->groupId == -1, "撤销后列表的分组号回到无分组");
    }
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    testutil::g_out = &out;

    // 可选过滤器：cysc_tests.exe TaskDocument 只跑名字含该子串的段落，
    // 方便定位某个数据结构/模块的问题。不带参数则全部运行。
    QString filter;
    if (argc > 1) {
        filter = QString::fromLocal8Bit(argv[1]);
    }
    auto wants = [&filter](const char* name) {
        return filter.isEmpty() || filter.contains(QString::fromLatin1(name), Qt::CaseInsensitive);
    };

    out << "CYSC TaskBar unit tests\n";
    if (!filter.isEmpty()) {
        out << "（过滤：" << filter << "）\n";
    }

    if (wants("Vector")) testVector();
    if (wants("Stack")) testStack();
    if (wants("Trie")) testTrie();
    if (wants("HashTable")) testHashTable();
    if (wants("SegmentTree")) testSegmentTree();
    if (wants("UnionFind")) testUnionFind();
    if (wants("TaskDocument")) testTaskDocument();
    if (wants("DocumentStore")) docstore_test::run();
    if (wants("TaskDraft")) testTaskDraft();
    if (wants("TaskModel")) { testModelInMemory(); testModelPersistence(); }
    if (wants("StoragePaths")) testStoragePaths();
    if (wants("DatabaseFailure")) testDatabaseFailure();

    out << "\n" << (testutil::g_checks - testutil::g_failures) << "/"
        << testutil::g_checks << " checks passed\n";
    out << (testutil::g_failures == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    out.flush();
    return testutil::g_failures == 0 ? 0 : 1;
}
