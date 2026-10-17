#ifndef TASKGROUPDIALOG_H
#define TASKGROUPDIALOG_H

// ============================================================================
//  TaskGroupDialog.h
//  职责：列表分组对话框（勾选若干列表归为一组）
//  依赖：models/TaskDocument.h
// ============================================================================

#include <QDialog>
#include <vector>

#include "src/models/TaskDocument.h"

class QCheckBox;
class QVBoxLayout;

// 「把若干列表合成一个分组」的对话框。
//
// 对应 Microsoft To Do 的列表分组：勾选两个或更多列表，确定后它们归到同一组。
// 底层是并查集（UnionFind）——两两合并，传递性自动成立。
class TaskGroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskGroupDialog(TaskDocument* doc, QWidget* parent = nullptr);

    // 用户勾选的列表 id（按勾选顺序无关，升序返回）。
    std::vector<int> selectedListIds() const;

    // 界面自检用：可勾选项数量。
    int optionCount() const { return static_cast<int>(m_boxes.size()); }
    // 按列表名勾选，便于测试。
    bool setCheckedByListName(const QString& name, bool checked);

    // 按当前主题重新拼样式表。
    void applyTheme();

private:
    struct Option {
        int listId = -1;
        QString name;
        QCheckBox* box = nullptr;
    };

    TaskDocument* m_doc = nullptr;
    std::vector<Option> m_boxes;
    QVBoxLayout* m_layout = nullptr;
};

#endif
