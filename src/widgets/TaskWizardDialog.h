#ifndef TASKWIZARDDIALOG_H
#define TASKWIZARDDIALOG_H

// ============================================================================
//  TaskWizardDialog.h
//  职责：逐栏向导：Tab 一栏栏推进、1-4 选择、Enter 随时完成
//  依赖：models/TaskDraft.h
// ============================================================================

#include <QDialog>
#include "src/models/Task.h"
#include "src/models/TaskDraft.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

// 「逐栏向导」新建任务窗口。
//
// 一次只显示一栏，Tab 一栏栏推进，Enter 随时结束：
//   标题(Tab) → 优先级(1-4 / Tab 跳过) → 分组(1-4 / Tab 跳过) → 日期(Tab / Enter 结束)
// 日期可留空 = 无截止时间。Backspace 回上一栏，Esc 取消。
//
// 与 TaskDialog 的区别：TaskDialog 是一屏填完所有字段（含备注）的完整表单，
// 适合编辑已有任务；本窗口是给「快速新建」用的键盘流。
class TaskWizardDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskWizardDialog(QWidget *parent = nullptr);

    Task task() const;

    // 按当前主题重新拼样式表。
    void applyTheme();

    // ---- 界面自检用的钩子 ----
    // 把向导直接拨到某一栏（跳过前置栏的输入校验），并可在日期栏填入文本。
    // 有了它才能在不进事件循环的前提下验证「停在日期栏按 Enter 会不会丢时间」。
    void testGoToStage(TaskDraft::Stage stage, const QString& dateText = QString());
    // 走一遍真实的「完成」路径（等价于按 Enter），之后可用 task() 取结果。
    void testCommit();
    // 走一遍真实的「进入下一栏」路径（等价于按 Tab / 点选项）。
    void testAdvance();
    // 当前所在栏（界面自检用来确认每一栏都真的显示过）。
    TaskDraft::Stage testStage() const;
    // 当前页签索引与总页数（用来抓"栏被跳过"这类问题）。
    int testPageIndex() const;
    int testPageCount() const;
    // 当前显示在最上面的栏目标题（"第 3 栏：分组" 之类）。
    QString testStepText() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Tab 在本窗口是「进入下一栏」，不是切换焦点，所以要挡掉 Qt 的焦点链。
    bool focusNextPrevChild(bool next) override;

private:
    void buildUi();
    void setStage(TaskDraft::Stage stage);
    void refreshChoices();
    void refreshSummary();
    void advance();          // Tab：提交当前栏并前进（最后一栏则完成）
    void retreat();          // Backspace：回上一栏
    // 把当前这一栏的输入收进 draft；返回 false 表示输入不合法、应停在当前栏。
    bool collectCurrentStage();
    void commit();           // Enter / 完成
    QString choiceStyle(const QString& accent, bool selected) const;
    bool stageUsesChoices() const;

    TaskDraft m_draft;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_stepLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_dateEdit = nullptr;
    QWidget *m_priorityPage = nullptr;
    QWidget *m_categoryPage = nullptr;
    QPushButton *m_priorityButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QPushButton *m_categoryButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_summary = nullptr;
};

#endif
