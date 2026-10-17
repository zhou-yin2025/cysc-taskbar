#ifndef TASKSIDEBAR_H
#define TASKSIDEBAR_H

// ============================================================================
//  TaskSidebar.h
//  职责：左侧列表栏：智能视图（带计数）+ 列表 + 分组聚类渲染
//  依赖：models/TaskDocument.h
// ============================================================================

#include <QHash>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

#include "src/models/TaskDocument.h"

class QLabel;

// 左侧栏（对应 Microsoft To Do 的列表栏）。
//
// 结构：
//   智能视图区：我的一天 / 重要 / 计划内 / 全部 / 已完成
//   列表区    ：用户自建的列表（带颜色圆点与未完成计数），按分组显示
//   底部      ：+ 新建列表
//
// 选中状态通过信号发出去，主窗口据此切换右侧内容。
class TaskSidebar : public QWidget {
    Q_OBJECT
public:
    explicit TaskSidebar(QWidget *parent = nullptr);

    // 绑定数据源（不持有所有权）。调用后需要 refresh()。
    void setDocument(TaskDocument *doc);

    // 当前选中项
    bool isSmartViewSelected() const { return m_smartViewSelected; }
    SmartView currentView() const { return m_view; }
    int currentListId() const { return m_listId; }

    // 按文档内容重建列表项（数量、名称、高亮）。
    void refresh();
    // 选中某个智能视图 / 列表（会发信号）。
    void selectSmartView(SmartView view);
    void selectList(int listId);

    // 界面自检用：当前渲染出的行数。
    int smartRowCount() const { return static_cast<int>(m_smartButtons.size()); }
    int listRowCount() const { return static_cast<int>(m_listRows.size()); }
    // 界面自检用：当前选中状态与分组标题数量。
    bool smartViewSelected() const { return m_smartViewSelected; }
    int selectedListId() const { return m_listId; }
    int groupHeaderCount() const { return static_cast<int>(m_groupHeaders.size()); }

signals:
    void smartViewChosen(SmartView view);
    void listChosen(int listId);
    void addListRequested();
    void groupListsRequested();
    // 列表的编辑操作。侧栏只是视图、不拥有文档，所以只发信号，
    // 由主窗口去改模型并落库（与 addListRequested / groupListsRequested 一致）。
    void renameListRequested(int listId);
    void deleteListRequested(int listId);
    void changeListColorRequested(int listId);
    // 把列表移出它所在的分组（解散该列表的分组关系）。
    void removeListFromGroupRequested(int listId);

private:
    struct SmartRow {
        SmartView view;
        QPushButton *button = nullptr;
        QLabel *badge = nullptr;
    };
    struct ListRow {
        int listId;
        QWidget *container = nullptr;
        QPushButton *button = nullptr;
        QLabel *badge = nullptr;
    };

    QPushButton *makeSmartButton(const QString& text, const QString& color);
    void addSmartRow(SmartView view, const QString& text, const QString& color);
    void rebuildListRows();
    void updateHighlight();
    // 列表行右键菜单：重命名 / 改颜色 / 移出分组 / 删除。
    void showListMenu(int listId, const QPoint& globalPos);
    TaskDocument *m_doc = nullptr;

    QWidget *m_smartContainer = nullptr;
    QVBoxLayout *m_smartLayout = nullptr;
    QWidget *m_listContainer = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_listHeader = nullptr;
    QPushButton *m_addListButton = nullptr;
    QPushButton *m_groupButton = nullptr;

    std::vector<SmartRow> m_smartButtons;
    std::vector<ListRow> m_listRows;
    // 分组标题行（同组列表前的「分组 N」标签），refresh 时一起重建。
    std::vector<QWidget*> m_groupHeaders;

    bool m_smartViewSelected = true;
    SmartView m_view = SmartView::MyDay;
    int m_listId = -1;
};

#endif
