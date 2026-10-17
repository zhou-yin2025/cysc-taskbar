#ifndef TASKQUERY_H
#define TASKQUERY_H

// ============================================================================
//  TaskQuery.h
//  职责：状态文案与状态轮转规则（界面与模型共用）
//  依赖：无
// ============================================================================

#include <cstring>
#include <string>

// Status values used by Task::status.
inline bool isKnownStatus(int status) {
    return status >= 0 && status <= 3;
}

// 状态名，界面直接显示。源文件是 UTF-8，CMakeLists 里已开启 /utf-8。
inline const char* taskStatusText(int status) {
    switch (status) {
        case 0: return "待办";
        case 1: return "进行中";
        case 2: return "已完成";
        case 3: return "已暂停";
        default: return "未知";
    }
}

// 双击任务名时的状态推进顺序：待办 -> 进行中 -> 已完成 -> 待办。
inline int nextStatusInCycle(int status) {
    switch (status) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 0;
        default: return 0;   // 已暂停等状态回到待办
    }
}

// UI-independent description of "which tasks do I want, in what order".
// Kept in the model layer so the widget code never has to hand-roll filters.
struct TaskQuery {
    enum SortOrder { OrderAsc, OrderDesc };

    // Which field drives the comparison. Both sorts are naturally ascending:
    // creation order (by id) and deadline order.
    enum SortScope { SortCreated, SortDeadline };

    // Free text matched as a case-insensitive substring of the title OR the
    // details. Deliberately not a prefix-only match: a user who half-remembers a
    // title must still find it.
    std::string search;
    int categoryId = -1;          // -1 means "any category"
    bool hideDone = false;        // filter out status == 2
    SortScope scope = SortCreated;
    SortOrder order = OrderAsc;
};

// 已完成的任务一律排到最后。列表默认把完成项压到底部，无论按哪个字段排序、
// 升序还是降序，都只影响未完成项之间的相对顺序。
inline bool taskIsDone(int status) {
    return status == 2;
}

// 共享排序规则：先比「是否已完成」，再比所选字段；返回 0 表示在该维度上相等，
// 便于调用方继续接一个稳定的次级比较。
inline int taskCompareInScope(const std::string& aDeadline, int aId, int aStatus,
                              const std::string& bDeadline, int bId, int bStatus,
                              TaskQuery::SortScope scope) {
    // 关键：完成项永远排在后面。
    const bool aDone = taskIsDone(aStatus);
    const bool bDone = taskIsDone(bStatus);
    if (aDone != bDone) {
        return aDone ? 1 : -1;
    }

    switch (scope) {
        case TaskQuery::SortDeadline: {
            const std::string da = aDeadline.size() >= 10 ? aDeadline.substr(0, 10) : std::string();
            const std::string db = bDeadline.size() >= 10 ? bDeadline.substr(0, 10) : std::string();
            // 没有截止时间的排在后面：它们不该占据列表顶部。
            const bool aHas = !da.empty();
            const bool bHas = !db.empty();
            if (aHas != bHas) return aHas ? -1 : 1;
            if (da != db) return da < db ? -1 : 1;
            return 0;
        }
        case TaskQuery::SortCreated:
        default:
            if (aId != bId) return aId < bId ? -1 : 1;
            return 0;
    }
}

#endif
