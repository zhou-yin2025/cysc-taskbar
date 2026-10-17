#ifndef TEST_DOCSTORE_H
#define TEST_DOCSTORE_H

#include <QDate>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <string>
#include <vector>

#include "src/core/DocumentStore.h"

// ---------------------------------------------------------------- 共享测试工具
//
// 造一个"**不可写**"的路径：拿一个普通文件当目录用。
// <某个普通文件>/xxx.db 在任何平台上都不可能创建成功，
// 而且失败原因（不是目录）跨平台一致。
//
// ⚠ 不要退回用 "Z:/definitely/not/a/real/drive/x.db" 这种 Windows 专属写法：
// 那在 Windows 上确实会失败（Z: 盘不存在），但在 macOS/Linux 上 `Z:/...`
// 是**合法的相对路径**（当前目录下建一个叫 "Z:" 的文件夹），操作会成功，
// 断言就崩了。本项目曾经因此在 macOS 上挂了 6 条断言。
//
// 放在头文件里是有意的：test_main.cpp 与 test_docstore.h 都要用它，
// 而 test_docstore.h 是在 test_main.cpp 的第 89 行被包含的 ——
// 如果把它定义在 test_main.cpp 里，头文件就看不到它（编译报
// "use of undeclared identifier"）。
inline QString makeFileUsedAsDirectory() {
    const QString dir = qEnvironmentVariable("CYSC_TEST_TMPDIR", QDir::tempPath());
    QDir().mkpath(dir);
    const QString filePath = QDir(dir).absoluteFilePath("cysc_not_a_directory.tmp");
    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("x");
        f.close();
    }
    return filePath;
}

// 文档级持久化的往返测试 + 旧库迁移测试。
// 定义在独立头文件里，避免再往 test_main.cpp 这个已经很长的翻译单元里堆代码。
namespace docstore_test {

inline std::string storeDayOffset(int days) {
    return QDate::currentDate().addDays(days).toString("yyyy-MM-dd").toStdString() + " 09:00";
}

// tempDbPath 由 test_main.cpp 提供：优先用 CYSC_TEST_TMPDIR，并保证文件名唯一。
inline QString freshDbPath(const QString& name) {
    const QString path = QString::fromStdString(tempDbPath(name));
    QFile::remove(path);
    return path;
}

inline void run() {
    section("DocumentStore（文档级持久化：列表/任务/子任务/标签/分组）");

    // ---------------- 标签编解码 ----------------
    {
        const std::vector<std::string> tags = {"工作", "紧急", "a,b|c"};
        const QString encoded = DocumentStore::encodeTags(tags);
        const std::vector<std::string> back = DocumentStore::decodeTags(encoded);
        expect(back.size() == 3, "标签编解码数量一致");
        expect(back.size() == 3 && back[2] == "a,b|c",
              "含逗号和竖线的标签也能正确还原（用 Unit Separator 分隔）");
        expect(DocumentStore::decodeTags(QString()).empty(), "空文本解出空标签");
        expect(DocumentStore::encodeTags({}).isEmpty(), "空标签列表编出空文本");
        const std::vector<std::string> padded = DocumentStore::decodeTags(" a \x1f\x1f b ");
        expect(padded.size() == 2, "解码会跳过空段并去空白");
    }

    // ---------------- 往返：存进去再读出来 ----------------
    {
        const QString path = freshDbPath("roundtrip.db");

        TaskDocument original;
        const int work = original.addList("工作", "#FF007F");
        const int study = original.addList("学习", "#00AA88");
        original.joinListsIntoGroup(work, study);

        Task withSteps;
        withSteps.title = "编写项目文档";
        withSteps.details = "含有关键备注";
        withSteps.listId = work;
        withSteps.priority = 3;
        withSteps.deadline = storeDayOffset(2);
        withSteps.tags = {"工作", "紧急"};
        withSteps.starred = true;
        withSteps.inMyDay = true;
        withSteps.reminder = storeDayOffset(1);
        withSteps.recurrence = Recurrence::Weekly;
        withSteps.subtasks = {{0, "收集资料", true}, {1, "撰写正文", false}};
        const int mainId = original.addTask(withSteps);

        original.addTask([] {
            Task t;
            t.title = "看论文";
            t.listId = 1;
            t.tags = {"学习"};
            return t;
        }());

        Task done;
        done.title = "已完成的事";
        done.listId = study;
        done.status = 2;
        done.completedAt = storeDayOffset(-1);
        original.addTask(done);

        DocumentStore store;
        expect(store.open(path), "打开（新建）数据库成功");
        expect(store.saveDocument(original), "saveDocument 成功");
        store.close();

        DocumentStore reader;
        expect(reader.open(path), "重新打开数据库成功");
        TaskDocument loaded;
        expect(reader.loadDocument(loaded), "loadDocument 成功");

        expect(loaded.lists().size() == 3, "列表数量一致（默认 + 工作 + 学习）");
        const TaskList* workList = loaded.findList(work);
        expect(workList != nullptr && workList->name == "工作", "列表名还原正确");
        expect(workList != nullptr && workList->color == "#FF007F", "列表颜色还原正确");

        expect(loaded.tasks().size() == 3, "任务数量一致");

        const Task* t = loaded.findTask(mainId);
        expect(t != nullptr, "任务 id 还原正确");
        if (t) {
            expect(t->title == "编写项目文档", "标题还原");
            expect(t->details == "含有关键备注", "备注还原");
            expect(t->priority == 3, "优先级还原");
            expect(t->deadline == withSteps.deadline, "截止时间还原（精确到分钟）");
            expect(t->listId == work, "所属列表还原");
            expect(t->starred, "星标还原");
            expect(t->inMyDay, "我的一天标记还原");
            expect(t->reminder == withSteps.reminder, "提醒时间还原");
            expect(t->recurrence == Recurrence::Weekly, "重复规则还原");
            expect(t->tags.size() == 2, "标签数量还原");
            expect(t->hasTag("工作") && t->hasTag("紧急"), "标签内容还原");
            expect(t->subtasks.size() == 2, "子任务数量还原");
            expect(t->subtasks.size() == 2 && t->subtasks[0].title == "收集资料"
                       && t->subtasks[0].done,
                   "第一个子任务（标题 + 勾选）还原");
            expect(t->subtasks.size() == 2 && !t->subtasks[1].done, "第二个子任务未完成状态还原");
            expect(t->subtaskPercent() == 50, "子任务进度还原为 50%");
        }

        // 索引必须重建：标签哈希 / Trie / 线段树
        expect(loaded.tagUsage("工作") == 1, "读库后标签哈希索引可用");
        expect(loaded.tagUsage("学习") == 1, "第二个标签也在索引里");
        expect(loaded.titleCompletions("编写项目").size() == 1, "读库后 Trie 补全可用");
        expect(loaded.titleCompletions("编写项目").size() == 1
                   && loaded.titleCompletions("编写项目").front() == "编写项目文档",
               "补全结果正确");
        expect(loaded.segmentTreeDays() > 0, "读库后线段树可用");
        expect(loaded.deadlineStats(30).total == 1, "读库后日期区间统计正确");

        // 分组关系往返
        expect(loaded.listsShareGroup(work, study), "并查集分组关系被持久化并还原");
        expect(!loaded.listsShareGroup(work, 1), "未合并的列表读回来后仍不同组");

        // 手工拖拽顺序往返。
        // 注意两点：
        //  1. 不要假设任务 id 与位置的关系，先查出当前顺序再确定性地移动；
        //  2. **必须挑一条未完成的任务**来移 —— 已完成的任务会被统一置底
        //     （见 TaskDocument::sortTasks），移到最前也会被顶回去。
        {
            TaskFilter manual;
            manual.sort = TaskSort::Manual;
            std::vector<int> ids;
            for (const Task &t : loaded.query(manual)) ids.push_back(t.id);
            expect(ids.size() >= 2, "读库后至少有两条任务可排序");

            // 找一条「未完成、且当前不在最前」的任务
            const std::vector<Task> beforeMove = loaded.query(manual);
            int moveId = -1;
            for (std::size_t i = 1; i < beforeMove.size(); ++i) {
                if (beforeMove[i].status != 2) { moveId = beforeMove[i].id; break; }
            }
            expect(moveId >= 0,
                   "存在未完成且不在最前的任务（可用于验证手工顺序）");

            if (moveId >= 0) {
                expect(loaded.moveTask(moveId, 0, ids), "读库后仍可调整手工顺序");
                const std::vector<Task> afterMove = loaded.query(manual);
                expect(!afterMove.empty() && afterMove.front().id == moveId,
                      "调整后那条任务排在最前");
            }

            // 另外确认：已完成的任务确实被置底
            const std::vector<Task> afterMove = loaded.query(manual);
            bool doneAtBottom = true;
            bool seenDone = false;
            for (const Task &t : afterMove) {
                if (t.status == 2) seenDone = true;
                else if (seenDone) doneAtBottom = false;   // 已完成后面还有未完成 -> 没置底
            }
            expect(doneAtBottom, "读库后已完成任务依然置底");
        }

        // 新任务的 id 必须接在已有最大值之后
        const int fresh = loaded.addTask([] {
            Task t;
            t.title = "新任务";
            t.listId = 1;
            return t;
        }());
        expect(fresh > mainId, "读库后新建任务的 id 大于已有最大 id（不会覆盖旧数据）");

        reader.close();

        // 同一个库再存一次并读回，确认 manual_order 真的落库了。
        // 注意：这一轮要复用同一个 path，所以先不能删文件。
        {
            DocumentStore store2;
            expect(store2.open(path), "重新打开同一个库");

            // 先记下当前手工顺序的首条，再存盘、读回、核对
            TaskFilter mf;
            mf.sort = TaskSort::Manual;
            std::vector<int> beforeIds;
            for (const Task &t : loaded.query(mf)) beforeIds.push_back(t.id);
            expect(!beforeIds.empty(), "存盘前能取到手工顺序");

            expect(store2.saveDocument(loaded), "把调整过顺序的文档存盘");
            TaskDocument reloaded;
            expect(store2.loadDocument(reloaded), "读回文档");

            std::vector<int> afterIds;
            for (const Task &t : reloaded.query(mf)) afterIds.push_back(t.id);
            expect(afterIds == beforeIds, "读回后的手工顺序与存盘前完全一致");

            bool contiguous = true;
            for (std::size_t i = 0; i < afterIds.size(); ++i) {
                const Task *t = reloaded.findTask(afterIds[i]);
                if (!t || t->manualOrder != static_cast<int>(i)) contiguous = false;
            }
            expect(contiguous, "读回后 manualOrder 仍是连续的 0..n-1");
            store2.close();
        }

        QFile::remove(path);
    }

    // ---------------- 旧库迁移：没有新列的老数据库 ----------------
    {
        const QString path = freshDbPath("legacy.db");

        // 手工造一个 V0.0.3 时代的表结构（没有 list_id / tags 等列）
        {
            const QString connection = "cysc_legacy_setup";
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(path);
                expect(db.open(), "造旧库：打开成功");
                QSqlQuery q(db);
                expect(q.exec("CREATE TABLE tasks ("
                              "id INTEGER PRIMARY KEY, title TEXT NOT NULL, details TEXT DEFAULT '',"
                              "priority INTEGER DEFAULT 1, deadline TEXT, category_id INTEGER DEFAULT 0,"
                              "status INTEGER DEFAULT 0, created_at TEXT, completed_at TEXT)"),
                       "造旧库：建旧版 tasks 表");
                expect(q.exec("INSERT INTO tasks (id, title, details, priority, deadline, "
                              "category_id, status, created_at, completed_at) VALUES "
                              "(0, '老数据', '旧备注', 2, '2026-10-20 09:00', 1, 0, "
                              "'2026-10-01 08:00', '')"),
                       "造旧库：插入一条老任务");
                db.close();
            }
            QSqlDatabase::removeDatabase(connection);
        }

        DocumentStore store;
        expect(store.open(path), "打开旧库会自动迁移（ALTER TABLE 补列）");
        TaskDocument loaded;
        expect(store.loadDocument(loaded), "旧库能被读取");

        expect(loaded.tasks().size() == 1, "旧数据被读出");
        const Task* legacy = loaded.findTask(0);
        expect(legacy != nullptr && legacy->title == "老数据", "旧任务标题正确");
        expect(legacy != nullptr && legacy->details == "旧备注", "旧任务备注正确");
        expect(legacy != nullptr && legacy->priority == 2, "旧任务优先级正确");
        expect(legacy != nullptr && legacy->deadline == "2026-10-20 09:00", "旧任务截止时间正确");
        expect(legacy != nullptr && legacy->listId == 1, "缺失的 list_id 落到默认列表");
        expect(legacy != nullptr && !legacy->starred, "缺失的 starred 默认为否");
        expect(legacy != nullptr && legacy->recurrence == Recurrence::None,
              "缺失的 recurrence 默认为不重复");
        expect(legacy != nullptr && legacy->tags.empty(), "缺失的 tags 默认为空");
        expect(loaded.lists().size() == 1, "旧库没有 lists 表内容时保留默认列表");

        // 迁移后应能正常回写
        expect(store.saveDocument(loaded), "迁移后可以回写");
        store.close();
        QFile::remove(path);
    }

    // ---------------- 删除列表后任务迁移，再存读 ----------------
    {
        const QString path = freshDbPath("listremove.db");

        TaskDocument doc;
        const int temp = doc.addList("临时列表");
        doc.addTask([temp] {
            Task t;
            t.title = "挂在临时列表的任务";
            t.listId = temp;
            return t;
        }());
        expect(doc.removeList(temp), "删除列表成功");

        DocumentStore store;
        expect(store.open(path), "打开数据库");
        expect(store.saveDocument(doc), "删除列表后仍可保存");
        TaskDocument loaded;
        expect(store.loadDocument(loaded), "读回成功");
        expect(loaded.tasks().size() == 1, "任务数量保留");
        expect(loaded.tasks().front().listId == loaded.defaultListId(),
              "任务已迁移到默认列表（不会变成孤儿）");
        store.close();
        QFile::remove(path);
    }

    // ---------------- 未打开数据库时的错误处理 ----------------
    {
        DocumentStore store;
        TaskDocument doc;
        expect(!store.isOpen(), "未打开时 isOpen 为 false");
        expect(!store.saveDocument(doc), "未打开时 saveDocument 失败");
        expect(!store.loadDocument(doc), "未打开时 loadDocument 失败");
        expect(!store.lastError().isEmpty(), "失败会记录错误信息");

        // ⚠ 不能用 "Z:/definitely/not/a/real/drive/x.db" 这种 Windows 专属路径：
        // 在 macOS/Linux 上 `Z:/...` 是合法的相对路径（当前目录下建个 "Z:" 文件夹），
        // 打开会成功，断言就崩了。改用"拿普通文件当目录用"，
        // 任何平台都不可能在其下建文件。
        // 注意 DocumentStore::open() 收的是 QString，不要 toStdString()。
        expect(!store.open(makeFileUsedAsDirectory() + "/x.db"),
               "不可写路径打开失败");
        expect(!store.lastError().isEmpty(), "打开失败也记录错误");
    }
}

} // namespace docstore_test

#endif
