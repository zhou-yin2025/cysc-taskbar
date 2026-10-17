#ifndef TASK_H
#define TASK_H

// ============================================================================
//  Task.h
//  职责：任务/子任务/列表 的数据定义与枚举
//  依赖：无
// ============================================================================

#include <string>
#include <vector>

// 子任务（对应 Microsoft To Do 的「步骤 / Steps」）。
struct Subtask {
    int id = -1;
    std::string title;
    bool done = false;
};

// 重复规则（对应 To Do 的「重复 / Repeat」）。
enum class Recurrence {
    None = 0,
    Daily = 1,
    Weekdays = 2,     // 工作日（周一到周五）
    Weekly = 3,
    Monthly = 4,
    Yearly = 5,
};

inline const char* recurrenceText(Recurrence r) {
    switch (r) {
        case Recurrence::Daily: return "每天";
        case Recurrence::Weekdays: return "每个工作日";
        case Recurrence::Weekly: return "每周";
        case Recurrence::Monthly: return "每月";
        case Recurrence::Yearly: return "每年";
        case Recurrence::None:
        default: return "不重复";
    }
}

// 任务。字段顺序与数据库列一一对应，便于存取。
struct Task {
    int id = -1;
    std::string title;
    std::string details;       // 备注 / 任务详情，可为空
    int priority = 1;          // 0 low, 1 mid, 2 high, 3 urgent
    std::string deadline;      // "yyyy-MM-dd HH:mm"，空串表示无截止时间
    int categoryId = 0;        // 0 其他, 1 工作, 2 学习, 3 健康
    int status = 0;            // 0 待办, 1 进行中, 2 已完成, 3 已暂停
    std::string createdAt;
    std::string completedAt;

    // ---- 以下为「应用版」新增字段（对应 To Do 的核心能力）----
    int listId = 1;                          // 所属列表（To Do 的 list）
    bool starred = false;                    // 星标 -> 「重要」智能视图
    bool inMyDay = false;                    // 加入我的一天（每天清空）
    std::vector<Subtask> subtasks;           // 步骤
    std::vector<std::string> tags;           // 标签（#work），由哈希表建倒排索引
    std::string reminder;                    // 提醒时间 "yyyy-MM-dd HH:mm"
    Recurrence recurrence = Recurrence::None;
    int manualOrder = 0;                     // 手工拖拽顺序（越小越靠前）

    // 完成进度：没有子任务时按状态判断，有子任务时按勾选比例。
    bool hasSubtasks() const { return !subtasks.empty(); }

    int doneSubtaskCount() const {
        int done = 0;
        for (const Subtask& s : subtasks) {
            if (s.done) ++done;
        }
        return done;
    }

    int subtaskPercent() const {
        if (subtasks.empty()) return status == 2 ? 100 : 0;
        return static_cast<int>(doneSubtaskCount() * 100 / static_cast<int>(subtasks.size()));
    }

    bool hasTag(const std::string& tag) const {
        for (const std::string& t : tags) {
            if (t == tag) return true;
        }
        return false;
    }

    bool hasDeadline() const { return deadline.size() >= 10; }
};

// 列表（To Do 左侧栏的每一行）。group 由并查集维护「分组」关系。
struct TaskList {
    int id = 1;
    std::string name = "任务";
    std::string color = "#3D6BFF";
    int groupId = -1;          // -1 表示不属于任何分组
    std::string createdAt;
    bool isDefault = false;    // 默认列表不可删除
};

// 智能视图（To Do 的 smart lists）：不是真实容器，而是对全部任务的过滤视图。
enum class SmartView {
    MyDay = 0,      // 我的一天
    Important,      // 重要（星标）
    Planned,        // 计划内（有截止时间）
    All,            // 全部
    Completed,      // 已完成
    Count
};

inline const char* smartViewText(SmartView v) {
    switch (v) {
        case SmartView::MyDay: return "我的一天";
        case SmartView::Important: return "重要";
        case SmartView::Planned: return "计划内";
        case SmartView::Completed: return "已完成";
        case SmartView::All:
        default: return "全部";
    }
}

#endif
