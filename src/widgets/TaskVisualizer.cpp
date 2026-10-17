#include "TaskVisualizer.h"

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QStringList>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

#include "src/models/TaskDocument.h"
#include "src/ui/Theme.h"

namespace {

// 全部取色走主题（原来这里是 4 个写死的深色常量）。
QString themeHex(const QColor &c) { return ThemeManager::hex(c); }

QString tableStyle() {
    const Theme &th = ThemeManager::current();
    return QString(
               "QTableWidget { background-color: %1; color: %2; gridline-color: %3; "
               "border: 1px solid %3; font-size: 10px; }"
               "QHeaderView::section { background-color: %4; color: %5; "
               "border: none; padding: 3px; font-size: 10px; }"
               "QTableWidget::item:selected { background-color: %6; }")
        .arg(themeHex(th.panelBg), themeHex(th.cardTitleInk),
             themeHex(th.cardBorder), themeHex(th.toolbarBg),
             themeHex(th.accent), themeHex(th.sidebarActive));
}

QString summaryStyle() {
    return QString("QLabel { color: %1; font-size: 10px; padding: 4px 2px; }")
        .arg(themeHex(ThemeManager::current().cardTitleInk));
}

// 用方块字符画一个横向条形图，直观显示桶长。
QString bars(std::size_t n, int maxBars = 30) {
    if (n == 0) return QString();
    const std::size_t count = n > static_cast<std::size_t>(maxBars)
                                  ? static_cast<std::size_t>(maxBars)
                                  : n;
    return QString(count, QChar(0x2588));      // █
}

} // namespace

TaskVisualizer::TaskVisualizer(TaskDocument* doc, QWidget* parent)
    : QDialog(parent), m_doc(doc) {
    setWindowTitle("数据结构可视化 —— CYSC 任务栏");
    resize(760, 560);
    applyTheme();

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    QLabel* hint = new QLabel(
        "以下都是程序当前真实的内部状态（只读）。切换左侧栏的视图或改动任务后，"
        "点「刷新」即可看到结构跟着变化。",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; }")
            .arg(themeHex(ThemeManager::current().accentDim)));
    root->addWidget(hint);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildHashPage(), "哈希表（标签索引）");
    m_tabs->addTab(buildTriePage(), "Trie（标题前缀）");
    m_tabs->addTab(buildSegmentPage(), "线段树（截止日期）");
    m_tabs->addTab(buildGroupPage(), "并查集（列表分组）");
    root->addWidget(m_tabs, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* refresh = new QPushButton("刷新", this);
    m_refreshButton = refresh;
    refresh->setObjectName("visualizerRefresh");
    QPushButton* close = new QPushButton("关闭", this);
    m_closeButton = close;
    const Theme &th2 = ThemeManager::current();
    const QString btnStyle =
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                "padding: 3px 14px; font-size: 11px; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
            .arg(themeHex(th2.toolbarBg), themeHex(th2.accent), themeHex(th2.onAccent));
    refresh->setStyleSheet(btnStyle);
    close->setStyleSheet(btnStyle);
    connect(refresh, &QPushButton::clicked, this, &TaskVisualizer::refreshAll);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(refresh);
    buttons->addWidget(close);
    root->addLayout(buttons);

    refreshAll();
}

// ------------------------------------------------------------- 1. 哈希表

// 按当前主题拼这一窗的样式表。
//
// 注意：构造函数一开始就会调它，那时 m_tabs / 底部按钮还没建出来，
// 所以下面每处都要判空。
void TaskVisualizer::applyTheme() {
    const Theme &th = ThemeManager::current();
    setStyleSheet(
        QString("QDialog { background-color: %1; }"
                "QTabWidget::pane { border: 1px solid %2; }"
                "QTabBar::tab { background: %3; color: %4; padding: 5px 12px; "
                "font-size: 11px; }"
                "QTabBar::tab:selected { background: %5; color: %6; }")
            .arg(themeHex(th.panelBg), themeHex(th.cardBorder), themeHex(th.toolbarBg),
                 themeHex(th.accentDim), themeHex(th.sidebarActive), themeHex(th.accent)));

    // 四个页面的表格样式是各自设的，切主题时一并刷新。
    // 只扫标签页子树，不扫整棵窗口树（任务行多的时候开销很大）。
    const QString ts = tableStyle();
    if (m_tabs) {
        for (QTableWidget *t : m_tabs->findChildren<QTableWidget*>()) {
            t->setStyleSheet(ts);
        }
    }

    // 底部两个按钮
    const QString btnStyle =
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                "padding: 3px 14px; font-size: 11px; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
            .arg(themeHex(th.toolbarBg), themeHex(th.accent), themeHex(th.onAccent));
    if (m_refreshButton) m_refreshButton->setStyleSheet(btnStyle);
    if (m_closeButton) m_closeButton->setStyleSheet(btnStyle);
}

QWidget* TaskVisualizer::buildHashPage() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_hashSummary = new QLabel(page);
    m_hashSummary->setStyleSheet(summaryStyle());
    m_hashSummary->setWordWrap(true);
    layout->addWidget(m_hashSummary);

    m_hashTable = new QTableWidget(page);
    m_hashTable->setColumnCount(3);
    m_hashTable->setHorizontalHeaderLabels({"桶", "链长", "内容（标签）"});
    m_hashTable->setStyleSheet(tableStyle());
    m_hashTable->verticalHeader()->setVisible(false);
    m_hashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hashTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_hashTable, 1);
    return page;
}

void TaskVisualizer::refreshHash() {
    if (!m_doc || !m_hashTable) return;

    const HashTable<std::string, std::vector<int>>& index = m_doc->tagIndex();
    const std::size_t buckets = index.bucketCount();
    const std::size_t used = index.usedBucketCount();
    const std::size_t longest = index.longestChain();

    // 只列出非空桶：空的桶对讲解没帮助，还会淹没重点。
    std::vector<std::pair<std::size_t, const HashTable<std::string, std::vector<int>>::Bucket*>>
        nonEmpty;
    for (std::size_t b = 0; b < buckets; ++b) {
        const auto& bucket = index.bucketAt(b);
        if (!bucket.empty()) nonEmpty.emplace_back(b, &bucket);
    }

    m_hashSummary->setText(
        QString("桶数量 %1（质数）　装载因子 %2　已用桶 %3/%1　元素 %4　最长冲突链 %5<br>"
                "散列函数 std::hash&lt;string&gt;，冲突用<b>链式存储</b>；"
                "装载因子超过 0.75 会自动扩容并重新散列。")
            .arg(buckets)
            .arg(index.loadFactor(), 0, 'f', 3)
            .arg(used)
            .arg(index.size())
            .arg(longest));

    m_hashTable->setRowCount(static_cast<int>(nonEmpty.size()));
    for (std::size_t r = 0; r < nonEmpty.size(); ++r) {
        const std::size_t bucketIndex = nonEmpty[r].first;
        const auto* bucket = nonEmpty[r].second;

        QString names;
        for (const auto& entry : *bucket) {
            if (!names.isEmpty()) names += " → ";
            names += QString::fromStdString(entry.first)
                     + QString("(%1)").arg(entry.second.size());
        }

        m_hashTable->setItem(static_cast<int>(r), 0,
                             new QTableWidgetItem(QString::number(bucketIndex)));
        m_hashTable->setItem(static_cast<int>(r), 1,
                             new QTableWidgetItem(
                                 QString("%1 %2").arg(bucket->size()).arg(bars(bucket->size()))));
        m_hashTable->setItem(static_cast<int>(r), 2, new QTableWidgetItem(names));
    }
    m_hashRows = static_cast<int>(nonEmpty.size());
}

// ---------------------------------------------------------------- 2. Trie

QWidget* TaskVisualizer::buildTriePage() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_trieSummary = new QLabel(page);
    m_trieSummary->setStyleSheet(summaryStyle());
    m_trieSummary->setWordWrap(true);
    layout->addWidget(m_trieSummary);

    m_trieTable = new QTableWidget(page);
    m_trieTable->setColumnCount(4);
    m_trieTable->setHorizontalHeaderLabels({"前缀", "子树结点数", "可补全条数", "标题总字符数"});
    m_trieTable->setStyleSheet(tableStyle());
    m_trieTable->verticalHeader()->setVisible(false);
    m_trieTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trieTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_trieTable, 1);
    return page;
}

void TaskVisualizer::refreshTrie() {
    if (!m_doc || !m_trieTable) return;

    const Trie& trie = m_doc->titleTrie();
    const int nodes = trie.nodeCount();
    const int words = trie.wordCount();

    // 「如果不共享前缀需要多少结点」= 所有标题的长度之和。
    long long naive = 0;
    for (const Task& t : m_doc->tasks()) naive += static_cast<long long>(t.title.size());

    m_trieSummary->setText(
        QString("已存标题 %1 条　Trie 结点数 %2　若逐字符独立存储需 %3 个结点<br>"
                "共享前缀省下 <b>%4</b> 个结点（%5%）　结点数 = 去重后的前缀集合大小，"
                "256 叉数组存储。")
            .arg(words)
            .arg(nodes)
            .arg(naive)
            .arg(naive - nodes)
            .arg(naive > 0 ? QString::number(100.0 * (naive - nodes) / naive, 'f', 1)
                           : QString("0.0")));

    // 用每个标题的前缀作为下钻示例。
    //
    // 关键：中文在 UTF-8 里是多字节，按「字节数」截断会把一个字砍成半个，
    // 生成非法 UTF-8，界面显示成乱码。所以按「字符」截断：
    // 判断一个字节是不是 UTF-8 续字节（10xxxxxx），只允许切在字符边界上。
    auto isContinuation = [](unsigned char c) { return (c & 0xC0) == 0x80; };

    std::vector<std::string> prefixes;
    for (const Task& t : m_doc->tasks()) {
        const std::string& title = t.title;
        int chars = 0;
        for (std::size_t len = 1; len <= title.size(); ++len) {
            // 只在字符边界处取前缀
            if (len < title.size() && isContinuation(static_cast<unsigned char>(title[len]))) {
                continue;
            }
            if (++chars > 3) break;          // 最多取到第 3 个字符
            const std::string prefix = title.substr(0, len);
            if (trie.subtreeNodeCount(prefix) > 0) prefixes.push_back(prefix);
        }
    }
    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());

    // 只保留「能补全出 >= 2 条」的前缀，这些才说明共享前缀的价值
    std::vector<std::string> interesting;
    for (const std::string& p : prefixes) {
        if (trie.getWordsWithPrefix(p).size() >= 2) interesting.push_back(p);
    }
    if (interesting.empty()) interesting = prefixes;
    if (interesting.size() > 40) interesting.resize(40);

    m_trieTable->setRowCount(static_cast<int>(interesting.size()));
    for (std::size_t r = 0; r < interesting.size(); ++r) {
        const std::string& prefix = interesting[r];
        const int sub = trie.subtreeNodeCount(prefix);
        const std::size_t completions = trie.getWordsWithPrefix(prefix).size();

        m_trieTable->setItem(static_cast<int>(r), 0,
                             new QTableWidgetItem(QString::fromStdString(prefix)));
        m_trieTable->setItem(static_cast<int>(r), 1,
                             new QTableWidgetItem(QString::number(sub)));
        m_trieTable->setItem(static_cast<int>(r), 2,
                             new QTableWidgetItem(QString::number(completions)));
        m_trieTable->setItem(static_cast<int>(r), 3,
                             new QTableWidgetItem(QString::number(prefix.size())));
    }
    m_trieRows = static_cast<int>(interesting.size());
}

// ------------------------------------------------------------ 3. 线段树

QWidget* TaskVisualizer::buildSegmentPage() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_segmentSummary = new QLabel(page);
    m_segmentSummary->setStyleSheet(summaryStyle());
    m_segmentSummary->setWordWrap(true);
    layout->addWidget(m_segmentSummary);

    m_segmentView = new QTextBrowser(page);
    m_segmentView->setStyleSheet(
        QString("QTextBrowser { background-color: %1; color: %2; "
                "border: 1px solid %3; font-size: 10px; }")
            .arg(themeHex(ThemeManager::current().panelBg),
                 themeHex(ThemeManager::current().cardTitleInk),
                 themeHex(ThemeManager::current().cardBorder)));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(8.5);
    m_segmentView->setFont(mono);
    layout->addWidget(m_segmentView, 1);
    return page;
}

void TaskVisualizer::refreshSegment() {
    if (!m_doc || !m_segmentView) return;

    const SegmentTree& tree = m_doc->deadlineTree();
    const long long days = tree.days();
    // 注意：不要用 slots 作变量名——Qt 的 `slots` 是宏（展开成空），
    // 会把这行变成 "const long long = ..." 从而报出莫名其妙的语法错误。
    const long long leafSlots = tree.leafSlots();

    const DeadlineStats stats = m_doc->deadlineStats(30);
    m_segmentSummary->setText(
        QString("窗口 %1 天（补齐到 %2 个叶子）　树高 %3　数组长度 %4<br>"
                "30 天内 %5 条　逾期 %6 条　今天到期 %7 条　最忙的一天 %8 条")
            .arg(days)
            .arg(leafSlots)
            .arg(tree.depth())
            .arg(tree.nodeSlots())
            .arg(stats.total)
            .arg(stats.overdue)
            .arg(stats.dueToday)
            .arg(stats.busiestDayCount));

    QString html;
    html += "<b>内部结点（根在最上，index 1 = 根）</b><br>";
    html += "<pre style='margin:2px 0;'>";

    // 按层打印。纯 0 的结点对讲解没有价值（本窗口 400 天、512 个叶子、
    // 树高 10，绝大多数层都是空的），所以只打印非 0 的结点，其余折叠成一行说明。
    const int depth = tree.depth();
    for (int level = 0; level < depth; ++level) {
        const long long first = 1LL << level;
        const long long count = first;
        html += QString("第 %1 层（结点 %2..%3）\n")
                    .arg(level)
                    .arg(first)
                    .arg(first + count - 1);

        int shown = 0;
        long long zeros = 0;
        const long long show = qMin<long long>(count, 16);
        for (long long i = 0; i < show; ++i) {
            const long long idx = first + i;
            const long long sum = tree.nodeSum(idx);
            if (sum == 0) {
                ++zeros;
                continue;
            }
            html += QString("  [%1] sum=%2 max=%3\n")
                        .arg(idx)
                        .arg(sum)
                        .arg(tree.nodeMax(idx));
            ++shown;
        }
        if (shown == 0 && zeros > 0) {
            html += QString("  （本层前 %1 个结点全为 0）\n").arg(zeros);
        }
        if (count > show) {
            html += QString("  ...（本层共 %1 个结点，其余略）\n").arg(count);
        }
        html += "\n";
    }

    html += "</pre><b>叶子（每一天的到期任务数，前 30 天）</b><br><pre>";
    const QDate today = QDate::currentDate();
    const long long leafShown = days < 30LL ? days : 30LL;
    for (long long d = 0; d < leafShown; ++d) {
        const long long v = tree.pointValue(d);
        QString bar;
        for (long long k = 0; k < v; ++k) bar += QChar(0x2588);
        html += QString("+%1 %2  %3 %4\n")
                    .arg(d, 2)
                    .arg(today.addDays(static_cast<int>(d)).toString("MM-dd"))
                    .arg(bar, -12)
                    .arg(v);
    }
    html += "</pre>";

    m_segmentView->setHtml(html);
    m_segmentRows = depth + static_cast<int>(leafShown);
}

// ------------------------------------------------------------ 4. 并查集

QWidget* TaskVisualizer::buildGroupPage() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_groupSummary = new QLabel(page);
    m_groupSummary->setStyleSheet(summaryStyle());
    m_groupSummary->setWordWrap(true);
    layout->addWidget(m_groupSummary);

    m_groupTable = new QTableWidget(page);
    m_groupTable->setColumnCount(5);
    m_groupTable->setHorizontalHeaderLabels(
        {"列表（元素）", "parent", "rank", "到根链长", "所在集合成员"});
    m_groupTable->setStyleSheet(tableStyle());
    m_groupTable->verticalHeader()->setVisible(false);
    m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_groupTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(m_groupTable, 1);
    return page;
}

void TaskVisualizer::refreshGroups() {
    if (!m_doc || !m_groupTable) return;

    const UnionFind& sets = m_doc->listGroups();
    const std::vector<TaskList>& lists = m_doc->lists();

    m_groupSummary->setText(
        QString("元素个数 %1（= 列表数量）　集合个数 %2　"
                "已合并的列表在侧栏里会缩进显示为「分组 N」<br>"
                "find 用<b>路径压缩</b>、join 用<b>按秩合并</b>；"
                "「到根链长」是当前 parent 指针的真实深度（压缩后就变短）。")
            .arg(sets.size())
            .arg(sets.setCount()));

    m_groupTable->setRowCount(static_cast<int>(lists.size()));
    for (std::size_t i = 0; i < lists.size(); ++i) {
        const int idx = static_cast<int>(i);
        const int parent = sets.parentOf(idx);
        const int rank = sets.rankAt(idx);
        const int chain = sets.chainLengthToRoot(idx);

        // members() 需要非 const，这里用 parent/root 自己算同一集合的成员
        QString members;
        const int root = [&]() {
            int cur = idx;
            for (int guard = 0; guard <= static_cast<int>(lists.size()); ++guard) {
                const int p = sets.parentOf(cur);
                if (p < 0 || p == cur) break;
                cur = p;
            }
            return cur;
        }();
        for (std::size_t j = 0; j < lists.size(); ++j) {
            int cur = static_cast<int>(j);
            for (int guard = 0; guard <= static_cast<int>(lists.size()); ++guard) {
                const int p = sets.parentOf(cur);
                if (p < 0 || p == cur) break;
                cur = p;
            }
            if (cur == root) {
                if (!members.isEmpty()) members += ", ";
                members += QString::fromStdString(lists[j].name);
            }
        }

        const TaskList& list = lists[i];
        m_groupTable->setItem(idx, 0, new QTableWidgetItem(QString::fromStdString(list.name)));
        m_groupTable->setItem(idx, 1, new QTableWidgetItem(QString::number(parent)));
        m_groupTable->setItem(idx, 2, new QTableWidgetItem(QString::number(rank)));
        m_groupTable->setItem(idx, 3, new QTableWidgetItem(QString::number(chain)));
        m_groupTable->setItem(idx, 4, new QTableWidgetItem(members));
    }
    m_groupRows = static_cast<int>(lists.size());
}

// ------------------------------------------------------------------ 刷新

void TaskVisualizer::refreshAll() {
    refreshHash();
    refreshTrie();
    refreshSegment();
    refreshGroups();
}

int TaskVisualizer::hashBucketRows() const { return m_hashRows; }
int TaskVisualizer::trieRows() const { return m_trieRows; }
int TaskVisualizer::segmentRows() const { return m_segmentRows; }
int TaskVisualizer::groupRows() const { return m_groupRows; }

void TaskVisualizer::selectTab(int index) {
    if (!m_tabs) return;
    if (index < 0 || index >= m_tabs->count()) return;
    m_tabs->setCurrentIndex(index);
    refreshAll();
}

int TaskVisualizer::tabCount() const {
    return m_tabs ? m_tabs->count() : 0;
}

int TaskVisualizer::exportTabImages(const QString& prefix) {
    if (!m_tabs) return 0;

    QTabWidget* tabs = m_tabs;
    const QStringList names = {"1_hash", "2_trie", "3_segment", "4_unionfind"};

    // grab() 之前必须先完成布局：从未显示过的控件子项尺寸还是 0，
    // 直接抓会得到 0×0 的空图（save 直接失败）。这里显式激活布局并派发事件。
    const int savedIndex = tabs->currentIndex();
    resize(920, 660);
    QApplication::processEvents();
    if (layout()) layout()->activate();
    if (tabs->layout()) tabs->layout()->activate();
    QCoreApplication::sendPostedEvents();
    QApplication::processEvents();

    int saved = 0;
    for (int i = 0; i < tabs->count() && i < names.size(); ++i) {
        tabs->setCurrentIndex(i);
        QWidget* page = tabs->widget(i);
        if (!page) continue;

        // 页面尺寸必须 > 0，否则抓不到内容
        if (page->width() <= 0 || page->height() <= 0) {
            page->resize(tabs->width() > 0 ? tabs->width() : 880,
                         tabs->height() > 0 ? tabs->height() : 520);
        }
        if (page->layout()) page->layout()->activate();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents();

        const QPixmap shot = page->grab();
        if (shot.isNull() || shot.width() <= 0 || shot.height() <= 0) continue;

        // 逐个尝试可用的图片格式，返回第一个成功的。
        //
        // 为什么要试多个：PNG 不是 Qt 内置格式，靠 imageformats 插件提供。
        // 本机 Qt 6.11 的 imageformats 目录里只有 gif / ico / jpeg / svg
        //（没有 PNG），所以 save(..., "PNG") 会静默失败——用 JPG 兜底。
        //
        // 注意：若连 JPG 也失败（插件路径不可达），本函数返回 0，命令会输出
        // 「已导出 0 张」。此时改用系统截图即可；界面功能本身不受影响。
        const QString base = QString("%1_%2").arg(prefix, names.at(i));
        const char* kFormats[] = {"PNG", "JPG"};
        const char* kSuffixes[] = {".png", ".jpg"};
        bool ok = false;
        for (int f = 0; f < 2 && !ok; ++f) {
            ok = shot.save(base + QString::fromLatin1(kSuffixes[f]),
                           kFormats[f]);
        }
        if (ok) ++saved;
    }
    tabs->setCurrentIndex(savedIndex);
    return saved;
}
