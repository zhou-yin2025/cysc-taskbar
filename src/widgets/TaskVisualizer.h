#ifndef TASKVISUALIZER_H
#define TASKVISUALIZER_H

// ============================================================================
//  TaskVisualizer.h
//  职责：数据结构可视化面板（演示用）：四种结构的内部状态
//  依赖：models/TaskDocument.h
// ============================================================================

#include <QDialog>
#include <QString>
#include <vector>

class QLabel;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class TaskDocument;

// 数据结构可视化面板（演示现场用）。
//
// 四个页签，各自把一种数据结构的**内部状态**画出来，而且是当前真实数据：
//   1. 哈希表 —— 每个桶的长度条形图、装载因子、最长冲突链、探查长度
//   2. Trie   —— 结点数 / 已存词数 / 共享前缀省下多少结点，以及前缀下钻
//   3. 线段树 —— 以树形文本展示内部结点（根在上的区间和 / 区间最大值）
//   4. 并查集 —— 父指针与秩、各集合成员、路径压缩前后的链长对比
//
// 面板只读：它拿到的都是 const 引用，不会改动业务数据。
class TaskVisualizer : public QDialog {
    Q_OBJECT
public:
    explicit TaskVisualizer(TaskDocument* doc, QWidget* parent = nullptr);

    // 重建全部四个页签（数据变化后调用）。
    void refreshAll();

    // 按当前主题重新拼样式表。
    void applyTheme();

    // 底部两个按钮（applyTheme 要单独给它们设样式）。
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_closeButton = nullptr;

    // 界面自检用：各页签渲染出的行数/条目数，0 表示没内容。
    int hashBucketRows() const;
    int trieRows() const;
    int segmentRows() const;
    int groupRows() const;

    // 把四个页签分别渲染成 PNG，文件名形如 <prefix>_1_hash.png 等。
    // 返回成功保存的张数。用于使用文档插图，也用于无抓屏环境下的视觉检查。
    int exportTabImages(const QString& prefix);

    // 切到第 index 个页签（0..3）。用于截图与演示脚本。
    void selectTab(int index);
    int tabCount() const;

private:
    QWidget* buildHashPage();
    QWidget* buildTriePage();
    QWidget* buildSegmentPage();
    QWidget* buildGroupPage();
    void refreshHash();
    void refreshTrie();
    void refreshSegment();
    void refreshGroups();

    TaskDocument* m_doc = nullptr;
    QTabWidget* m_tabs = nullptr;

    // 哈希表页
    QTableWidget* m_hashTable = nullptr;
    QLabel* m_hashSummary = nullptr;

    // Trie 页
    QTableWidget* m_trieTable = nullptr;
    QLabel* m_trieSummary = nullptr;

    // 线段树页
    QTextBrowser* m_segmentView = nullptr;
    QLabel* m_segmentSummary = nullptr;

    // 并查集页
    QTableWidget* m_groupTable = nullptr;
    QLabel* m_groupSummary = nullptr;

    int m_hashRows = 0;
    int m_trieRows = 0;
    int m_segmentRows = 0;
    int m_groupRows = 0;
};

#endif
