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

// 拖拽任务的 MIME 类型（F-007 跨视图拖拽）。
// 侧栏认这个类型才接收放下，避免把外部的任意拖拽当成任务。
namespace TaskMime {
inline const char *kTaskId = "application/x-cysc-task-id";
// 拖动侧栏里的「列表」本身（拖进别的分组）
inline const char *kListId = "application/x-cysc-list-id";
} // namespace TaskMime

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
    // 界面自检用：拿到某个 (kind,id) 对应的拖放目标控件，便于精确投递拖放事件。
    QWidget *testDropTarget(int kind, int id) const;
    // 界面自检用：某个智能视图 / 列表当前显示的计数徽标文本。
    QString smartBadgeText(SmartView view) const;
    QString listBadgeText(int listId) const;

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
    // 有任务被拖到侧栏某项上。kind: 0 = 智能视图，1 = 列表。
    // 侧栏只报告「落到哪」，改模型/落库仍由主窗口负责。
    void taskDropRequested(int kind, int targetId, int taskId);
    // 把列表移出它所在的分组（解散该列表的分组关系）。
    void removeListFromGroupRequested(int listId);
    // 给分组改名 / 解散整个分组（组内列表都变成「无分组」）
    void renameGroupRequested(int groupId);
    void dissolveGroupRequested(int groupId);
    // 把某个列表拖到了 (kind, targetId) 上：
    //   kind 2 = 分组标题（id 是分组号）  -> 加入该分组
    //   kind 1 = 另一个列表（id 是列表号）-> 与它同组；它没有分组时则把拖动的移出分组
    void listDropRequested(int listId, int kind, int targetId);

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
    // 把一个行控件登记成拖放目标（kind: 0 智能视图 / 1 列表）
    void registerDropTarget(QWidget *widget, int kind, int id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
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
    // 组标题（QLabel*）对应的分组号：改名/解散时要知道点的是哪一组
    QHash<QLabel*, int> m_groupHeaderIds;

    // 控件 -> (kind, id)：拖到哪个控件上就知道要落到哪个视图/列表
    QHash<QObject*, QPair<int, int>> m_dropTargets;

    // 列表行的拖拽：按下时记下是哪一行，移动超过阈值就发起拖动
    QHash<QObject*, int> m_listRowWidgets;   // 控件 -> 列表号
    int m_dragListId = -1;
    QPoint m_dragPressPos;

    bool m_smartViewSelected = true;
    SmartView m_view = SmartView::MyDay;
    int m_listId = -1;
};

#endif
