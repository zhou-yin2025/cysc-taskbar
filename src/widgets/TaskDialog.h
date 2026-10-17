#ifndef TASKDIALOG_H
#define TASKDIALOG_H

// ============================================================================
//  TaskDialog.h
//  职责：完整表单（一屏填完，含备注与精确时间）
//  依赖：models/TaskDraft.h
// ============================================================================

#include <QDialog>
#include "src/models/Task.h"
#include "src/models/TaskDraft.h"

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 新建任务 / 编辑任务窗口。
//
// Replaces the V0.0.2 inline wizard: every field is visible at once, including
// the 备注 (details) box, and the deadline has minute precision. The keyboard
// shortcuts survived the move: 1-4 selects priority while the title box has
// focus, and Ctrl+1..4 selects the category.
class TaskDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskDialog(QWidget *parent = nullptr);

    // Pre-fills the fields for editing an existing task.
    void setDraft(const Task& task);

    // The edited task. For a new task the id stays -1; setDraft() preserves it.
    Task task() const;

    // 按当前主题重新拼样式表。主窗口换主题、或窗口已开着时要调一次。
    void applyTheme();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onAccept();
    void onDeadlineChanged(const QDateTime& value);
    void onDeadlineToggled(bool enabled);

private:
    void buildUi();
    void refreshChips();
    void setPriority(int priority);
    void setCategory(int category);
    QString chipStyle(const QString& accent, bool active) const;

    TaskDraft m_draft;
    int m_editingId = -1;
    QString m_createdAt;

    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_detailsEdit = nullptr;
    QCheckBox *m_hasDeadline = nullptr;      // 取消勾选 = 无截止时间
    QDateTimeEdit *m_deadlineEdit = nullptr;
    QPushButton *m_priorityButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QPushButton *m_categoryButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_errorLabel = nullptr;
};

#endif
