#ifndef TASKDRAFT_H
#define TASKDRAFT_H

// ============================================================================
//  TaskDraft.h
//  职责：新建/编辑共用的字段状态机（无控件依赖，可单测）
//  依赖：models/Task.h
// ============================================================================

#include "Task.h"
#include <QDate>
#include <QDateTime>
#include <QString>

// The "new task" wizard, with no widgets attached.
//
// Two things use it: the new-task dialog (all fields visible at once) and the
// inline detail editor (loaded from an existing task via loadFrom, saved via
// toTask). Keeping the state machine separate means the whole flow is
// unit-testable without a GUI.
//
// F-006：分类（其他/工作/学习/健康）与列表合并之后，这里不再有「分类」这一栏。
// 向导现在是三栏：任务名 -> 紧急度 -> 日期。列表归属由详情面板的下拉决定，
// 等 F-002/F-003 重做向导时再把「列表」和「备注」加进来。
class TaskDraft {
public:
    enum Stage { StageTitle, StagePriority, StageDate };

    // What the caller should do after feeding a key.
    enum Outcome {
        None,           // nothing changed
        StageChanged,   // a new stage is active: re-render and clear the input
        Rejected,       // input was refused; show InputError()
        Committed,      // a task is ready; build it with toTask()
    };

    TaskDraft();

    Stage stage() const { return m_stage; }
    const QString& title() const { return m_title; }
    const QString& details() const { return m_details; }
    int priority() const { return m_priority; }
    // 无截止时间时返回无效的 QDateTime。
    const QDateTime& deadline() const { return m_deadline; }
    bool hasDeadline() const { return m_deadline.isValid(); }
    const QString& inputError() const { return m_inputError; }

    // Direct field setters, used by the dialog's widgets and the inline editor.
    void setTitle(const QString& title) { m_title = title.trimmed(); }
    void setDetails(const QString& details) { m_details = details; }
    void setPriority(int priority);
    void setDeadline(const QDateTime& deadline) { m_deadline = deadline; }
    void clearDeadline() { m_deadline = QDateTime(); }

    // 直接切到某一栏（向导的前进/后退用），不清空已填内容。
    void setStageDirect(Stage stage) { m_stage = stage; clearInput(); }

    // 1-4 quick select, valid in the priority stage.
    Outcome handleDigit(int digit);
    Outcome handleTab(const QString& inputText);
    Outcome handleEnter(const QString& inputText);
    Outcome handleEscape();

    // Clicking a priority/date button.
    Outcome choosePriority(int priority);
    Outcome chooseDateOffset(int offsetDays);

    // Clears everything and returns to the title stage.
    Outcome restart();

    // Seeds the draft from an existing task (inline editing).
    void loadFrom(const Task& task);

    // 解析自由输入的日期；**空输入返回无效值**（表示不设截止时间）。
    // 只给日期时按当天 23:59 处理，支持 "10/12"、"12"、"2026/10/12"、"10/12 14:30"。
    QDateTime dateFromInput(const QString& input) const;

    // "yyyy-MM-dd HH:mm"；无截止时间时返回空串。
    QString deadlineText() const;
    bool isValid() const { return !m_title.isEmpty(); }

    Task toTask() const;

private:
    void clearInput();

    Stage m_stage;
    QString m_title;
    QString m_details;
    int m_priority;
    QDateTime m_deadline;
    QString m_inputError;
};

#endif
