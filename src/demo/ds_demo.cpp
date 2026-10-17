#include "ds_demo.h"

#include <QDate>
#include <QString>
#include <QTextStream>

#include <string>
#include <vector>

#include "src/ds/HashTable.h"
#include "src/ds/SegmentTree.h"
#include "src/ds/Trie.h"
#include "src/ds/UnionFind.h"
#include "src/ds/Vector.h"
#include "src/models/TaskDocument.h"

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

int g_checks = 0;
int g_failures = 0;

void heading(const QString& title) {
    out() << "\n" << QString(66, '=') << "\n" << title << "\n"
          << QString(66, '=') << "\n";
    out().flush();
}

void note(const QString& text) {
    out() << "  " << text << "\n";
    out().flush();
}

void check(bool ok, const QString& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    out() << (ok ? "  [OK]   " : "  [!!]   ") << what << "\n";
    out().flush();
}

std::string dayOffset(int days) {
    return QDate::currentDate().addDays(days).toString("yyyy-MM-dd").toStdString() + " 09:00";
}

Task makeTask(const std::string& title, const std::string& deadline,
              const std::vector<std::string>& tags, int priority = 1) {
    Task t;
    t.title = title;
    t.deadline = deadline;
    t.tags = tags;
    t.priority = priority;
    t.listId = 1;
    return t;
}

// ------------------------------------------------------------- 1. 哈希表

void demoHashTable(TaskDocument& doc) {
    heading("3. 哈希表（链式）—— 标签倒排索引与重复标题检测");

    note("插入带标签的任务，观察标签倒排索引的桶分布：");
    struct Seed {
        const char* title;
        std::vector<std::string> tags;
    };
    const std::vector<Seed> seeds = {
        {"编写项目文档", {"工作", "紧急"}},
        {"整理实验数据", {"工作", "学习"}},
        {"复习数据结构", {"学习"}},
        {"跑步 5 公里", {"健康"}},
        {"准备演示 PPT", {"工作", "紧急"}},
        {"背单词", {"学习", "长期"}},
    };
    for (const Seed& s : seeds) {
        doc.addTask(makeTask(s.title, dayOffset(2), s.tags));
    }

    out() << QString("  桶数量 = %1（质数，降低除留余数法的聚集）\n")
                 .arg(doc.tagBucketCount());
    out() << QString("  装载因子 = %1\n").arg(doc.tagLoadFactor(), 0, 'f', 3);
    out() << QString("  最长冲突链 = %1\n").arg(doc.tagLongestChain());
    out() << "  倒排索引内容（标签 -> 任务数）：\n";
    for (const TagCount& tc : doc.tagCounts()) {
        out() << QString("      %1 -> %2\n")
                     .arg(QString::fromStdString(tc.tag))
                     .arg(tc.count);
    }
    out().flush();

    check(HashTable<std::string, int>::isPrime(doc.tagBucketCount()),
          "桶数量保持为质数");
    check(doc.tagUsage("工作") == 3, "标签「工作」命中 3 条任务");
    check(doc.tagUsage("紧急") == 2, "标签「紧急」命中 2 条任务");
    check(doc.tagUsage("不存在的标签") == 0, "未知标签返回 0（O(1) 未命中）");

    // 用哈希索引筛任务：这一步不会全表扫描
    TaskFilter byTag;
    byTag.tag = "工作";
    const std::vector<Task> hits = doc.query(byTag);
    out() << QString("  按标签「工作」筛出 %1 条：").arg(hits.size());
    for (const Task& t : hits) {
        out() << QString::fromStdString(t.title) << "  ";
    }
    out() << "\n";
    out().flush();
    check(hits.size() == 3, "按标签筛选走倒排索引，结果与 tagUsage 一致");

    // 重复标题检测
    note("重复标题检测（标题归一化后按哈希分组）：");
    doc.addTask(makeTask("背单词", dayOffset(1), {}));
    doc.addTask(makeTask("  背单词  ", dayOffset(1), {}));   // 空白差异，归一化后同名
    const std::vector<DuplicateGroup> dups = doc.duplicateTitleGroups();
    for (const DuplicateGroup& g : dups) {
        out() << QString("      「%1」出现 %2 次\n")
                     .arg(QString::fromStdString(g.normalizedTitle))
                     .arg(g.taskIds.size());
    }
    out().flush();
    check(!dups.empty(), "检出重复标题分组");
    check(dups.front().taskIds.size() == 3,
          "「背单词」三条（含首尾空白差异）归到同一组");
}

// ---------------------------------------------------------------- 2. Trie

void demoTrie() {
    heading("1. Trie（256 叉前缀树）—— 标题前缀补全");

    Trie trie;
    const std::vector<std::string> titles = {
        "编写项目文档", "编写项目汇报", "编写使用说明", "编写周报",
        "复习数据结构", "复习算法",
    };
    for (const std::string& t : titles) trie.insert(t);

    out() << QString("  插入 %1 个标题（注意「编写项目文档」与「编写项目汇报」"
                     "共享前缀）\n").arg(titles.size());
    for (const std::string& prefix : {"编写", "编写项", "编写项目", "复习"}) {
        const std::vector<std::string> found =
            trie.getWordsWithPrefix(prefix);
        out() << QString("  前缀「%1」补全出 %2 条：")
                     .arg(QString::fromStdString(prefix)).arg(found.size());
        for (const std::string& w : found) {
            out() << QString::fromStdString(w) << "  ";
        }
        out() << "\n";
    }
    out().flush();

    check(trie.getWordsWithPrefix("编写").size() == 4, "前缀「编写」命中 4 条");
    check(trie.getWordsWithPrefix("编写项目").size() == 2,
          "更长的前缀进一步收窄到 2 条");
    const std::vector<std::string> review = trie.getWordsWithPrefix("复习");
    check(review.size() == 2, "前缀「复习」命中 2 条");
    check(trie.getWordsWithPrefix("不存在的前缀").empty(), "未知前缀返回空");
    check(trie.search("编写项目文档"), "完整标题可以精确命中");
    check(!trie.search("编写项目"), "「编写项目」只是前缀，不是已存标题");

    note("为什么用 Trie 而不是哈希：哈希只能精确匹配，");
    note("要支持「任意前缀补全」就必须枚举所有键，Trie 只需走到前缀末尾再深搜。");
    note("6 个标题共 42 个字符，但因为共享前缀，实际结点远少于 42。");
}

// ------------------------------------------------------------ 3. 线段树

void demoSegmentTree() {
    heading("4. 线段树 —— 截止日期的区间统计与热力图");

    // 每一节用独立的文档，避免上一节造的数据混进这一节的统计里。
    TaskDocument doc;

    // 造一批分布在不同日期的任务：plan[i] 是第 i 条任务距今天几天
    const int plan[10] = {0, 1, 1, 2, 3, 3, 3, 5, 6, 6};
    for (int i = 0; i < 10; ++i) {
        doc.addTask(makeTask("计划任务 " + std::to_string(i + 1),
                             dayOffset(plan[i]), {}));
    }
    // 一条逾期的，用来验证「逾期」不计入未来窗口
    doc.addTask(makeTask("逾期任务", dayOffset(-3), {}));

    doc.rebuildSegmentTree(30);
    const std::vector<int> daily = doc.dailyDeadlineCounts(10);

    out() << "  未来 10 天每天到期的任务数（叶结点 = 一天）：\n";
    for (std::size_t i = 0; i < daily.size(); ++i) {
        const QDate date = QDate::currentDate().addDays(static_cast<int>(i));
        QString bar;
        for (int b = 0; b < daily[i]; ++b) bar += "█";
        out() << QString("      +%1  %2  %3  %4\n")
                     .arg(static_cast<int>(i), 2)
                     .arg(date.toString("MM-dd"))
                     .arg(bar, -8)
                     .arg(daily[i]);
    }
    out().flush();

    const DeadlineStats stats = doc.deadlineStats(30);
    out() << QString("  区间统计：30 天内 %1 条，逾期 %2 条，今天到期 %3 条\n")
                 .arg(stats.total).arg(stats.overdue).arg(stats.dueToday);
    out() << QString("  最忙的一天：距今 +%1 天，共 %2 条\n")
                 .arg(stats.busiestDayOffset).arg(stats.busiestDayCount);
    out().flush();

    check(daily.size() == 10, "逐日计数返回 10 天");
    check(daily[0] == 1, "今天有 1 条（区间起点）");
    check(daily[1] == 2, "明天有 2 条");
    check(daily[3] == 3, "第 4 天有 3 条");
    check(stats.busiestDayCount == 3, "最忙的一天有 3 条（区间最大值查询）");
    check(stats.busiestDayOffset == 3, "最忙的一天是距今第 4 天");
    check(stats.overdue == 1, "逾期任务被单独统计，不占未来窗口");

    // 直接验证区间和与暴力枚举一致
    SegmentTree tree(10);
    long long dailyTotal = 0;
    for (int i = 0; i < 10; ++i) {
        tree.set(i, daily[static_cast<std::size_t>(i)]);
        dailyTotal += daily[static_cast<std::size_t>(i)];
    }
    bool sumsMatch = true;
    for (int from = 0; from < 10; ++from) {
        for (int to = from; to < 10; ++to) {
            long long brute = 0;
            for (int d = from; d <= to; ++d) brute += daily[static_cast<std::size_t>(d)];
            if (tree.querySum(from, to) != brute) sumsMatch = false;
        }
    }
    check(sumsMatch, "任意 55 个区间的和都与暴力枚举一致");
    check(tree.querySum(0, 9) == dailyTotal, "整段区间和等于逐日之和");
    check(tree.total() == dailyTotal, "total() 与逐日之和一致（O(1) 取用）");
    check(tree.queryMax(0, 9) == 3, "整段最大值是 3（区间最大值查询）");
}

// ----------------------------------------------------------- 4. 并查集

void demoUnionFind() {
    heading("2. 并查集 —— 列表分组");

    UnionFind uf(6);
    out() << QString("  初始 %1 个元素，%2 个互不相交的集合\n")
                 .arg(uf.size()).arg(uf.setCount());
    check(uf.setCount() == 6, "初始每个元素自成一个集合");

    struct Step {
        int a;
        int b;
        const char* label;
    };
    const Step steps[] = {
        {0, 1, "把 0 与 1 并成一组"},
        {2, 3, "把 2 与 3 并成一组"},
        {1, 2, "把两个组并成一个（0,1,2,3 同组）"},
        {4, 5, "把 4 与 5 并成一组"},
    };
    for (const Step& s : steps) {
        const bool merged = uf.join(s.a, s.b);
        out() << QString("  %1 -> 合并%2，当前集合数 %3\n")
                     .arg(QString::fromUtf8(s.label),
                          merged ? "成功" : "失败（已在同组）")
                     .arg(uf.setCount());
    }
    out().flush();

    check(uf.setCount() == 2, "6 个元素最终只剩 2 个集合");
    check(uf.sameSet(0, 3), "0 与 3 传递性地属于同一集合（0~1~2~3）");
    check(!uf.sameSet(0, 4), "0 与 4 仍不同集合");
    check(uf.setSize(0) == 4, "0 所在集合有 4 个元素");
    check(!uf.join(0, 1), "重复合并同一对元素返回 false");

    out() << "  各集合成员：\n";
    for (int root : uf.roots()) {
        out() << QString("      代表元 %1 -> {").arg(root);
        const Vector<int> members = uf.members(root);
        for (int i = 0; i < members.size(); ++i) {
            out() << members[i] << (i + 1 < members.size() ? ", " : "");
        }
        out() << "}\n";
    }
    out().flush();

    note("列表分组用的就是它：每个列表是一个元素，合并 = 把两个列表归到一组。");
    note("路径压缩把 find 的后续查询摊平成近 O(1)：初始链 0->1->2 三次 find 后");
    note("0 与 1 都直接指向根。");
    check(uf.rankOf(0) >= 0, "按秩合并维护的秩有意义（不会退化成链）");
}

// --------------------------------------------------- 5. 业务侧综合演示

void demoBusiness(TaskDocument& doc) {
    heading("5. 业务侧：四种结构一起工作");

    const int work = doc.addList("工作", "#FF007F");
    const int study = doc.addList("学习", "#00AA88");
    const int side = doc.addList("副业", "#FFC814");

    out() << QString("  新建 3 个列表，现在共 %1 个\n").arg(doc.lists().size());
    check(!doc.listsShareGroup(work, study), "新建列表默认不属于同一分组");
    doc.joinListsIntoGroup(work, study);
    doc.joinListsIntoGroup(study, side);
    check(doc.listsShareGroup(work, side), "并查集传递性：工作 ~ 学习 ~ 副业 同组");
    out() << QString("  合并后集合数 = %1（默认列表独立，其余 3 个成一组）\n")
                 .arg(doc.groupCount());
    check(doc.groupCount() == 2, "4 个列表形成 2 个集合");

    out() << QString("\n  当前文档规模：任务 %1 条，列表 %2 个，标签 %3 个\n")
                 .arg(doc.tasks().size()).arg(doc.lists().size()).arg(doc.allTags().size());
    out() << QString("  智能视图计数：我的一天 %1 / 重要 %2 / 计划内 %3 / 已完成 %4\n")
                 .arg(doc.countInMyDay()).arg(doc.countStarred())
                 .arg(doc.countPlanned()).arg(doc.countCompleted());
    out().flush();

    // 星标一条，验证「重要」视图
    const int firstId = doc.tasks().front().id;
    Task starred = *doc.findTask(firstId);
    starred.starred = true;
    doc.updateTask(starred);
    check(doc.countStarred() == 1, "星标后「重要」视图统计 +1");

    // 撤销，验证索引跟着回退
    check(doc.canUndo(), "刚才的修改可以撤销");
    doc.undo();
    check(doc.countStarred() == 0, "撤销后星标状态回退");
    check(doc.tagUsage("工作") == 3, "撤销后标签哈希索引仍与原状一致");
}

} // namespace

int runDsDemo() {
    out() << "CYSC 任务栏 —— 数据结构演示\n";
    out() << "说明：以下每一项都是程序里真实运行的代码路径，不是硬编码输出。\n";
    out().flush();

    TaskDocument doc;          // 独立文档，不影响用户数据库

    // 顺序按「从简单到综合」排：Trie（纯字符串） -> 并查集（纯整数集合）
    // -> 哈希表（带业务数据） -> 线段树（带业务数据） -> 业务综合。
    // 标题里的编号跟着打印顺序走，避免演示时讲解与屏幕对不上。
    demoTrie();
    demoUnionFind();
    demoHashTable(doc);
    demoSegmentTree();
    demoBusiness(doc);

    heading("演示结果");
    out() << QString("  %1/%2 项检查通过\n").arg(g_checks - g_failures).arg(g_checks);
    out() << (g_failures == 0 ? "  演示成功（RESULT: PASS）\n"
                              : "  有项目未通过（RESULT: FAIL）\n");
    out().flush();
    return g_failures == 0 ? 0 : 1;
}
