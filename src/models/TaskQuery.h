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
    bool hideDone = false;        // filter out status == 2
    SortScope scope = SortCreated;
    SortOrder order = OrderAsc;
};

// 已完成的任务一律排到最后。列表默认把完成项压到底部，无论按哪个字段排序、
// 升序还是降序，都只影响未完成项之间的相对顺序。
inline bool taskIsDone(int status) {
    return status == 2;
}

// ---------------------------------------------------------------------------
//  「计划内」的时间分桶（F-007）
//
//  分桶规则放在模型层，是为了能脱离界面单测：给定「今天」和截止时间，
//  结果必须是确定的，不能依赖系统时钟。
//
//  桶的顺序也是显示顺序：逾期 -> 今天 -> 明天 -> 本周 -> 本月 -> 更晚 -> 无日期。
// ---------------------------------------------------------------------------
enum class PlannedBucket {
    Overdue,     // 已经过了截止日
    Today,
    Tomorrow,
    ThisWeek,    // 后天 .. 本周日（周一为一周之始）
    ThisMonth,   // 下周 .. 本月最后一天
    Later,       // 更晚
    NoDate,      // 没有截止时间（界面上默认折叠，放最后）
};

inline const char* plannedBucketText(PlannedBucket bucket) {
    switch (bucket) {
        case PlannedBucket::Overdue:  return "逾期";
        case PlannedBucket::Today:    return "今天";
        case PlannedBucket::Tomorrow: return "明天";
        case PlannedBucket::ThisWeek: return "本周";
        case PlannedBucket::ThisMonth:return "本月";
        case PlannedBucket::Later:    return "更晚";
        case PlannedBucket::NoDate:   return "无日期";
    }
    return "无日期";
}

// 公历日期 -> 1970-01-01 起的天数。用 Howard Hinnant 的算法，不引入任何依赖，
// 也就不会因为 QDate 的时区/本地化细节让单测在别的机器上飘。
inline long long civilDayNumber(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// 周一 = 0，周日 = 6。
inline int weekdayFromDayNumber(long long day) {
    return static_cast<int>(((day + 3) % 7 + 7) % 7);
}

// 把 "yyyy-MM-dd HH:mm"（或只有前 10 个字符）解析成天数；解析不出来返回 false。
inline bool dayNumberFromDeadline(const std::string& deadline, long long& out) {
    if (deadline.size() < 10) return false;
    // 只认前 10 位，格式必须是 yyyy-MM-dd，避免把 "2026/12/30" 这类脏数据当成有效值。
    if (deadline[4] != '-' || deadline[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (deadline[static_cast<std::size_t>(i)] < '0' || deadline[static_cast<std::size_t>(i)] > '9') {
            return false;
        }
    }
    const int y = (deadline[0] - '0') * 1000 + (deadline[1] - '0') * 100
                  + (deadline[2] - '0') * 10 + (deadline[3] - '0');
    const int m = (deadline[5] - '0') * 10 + (deadline[6] - '0');
    const int d = (deadline[8] - '0') * 10 + (deadline[9] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    out = civilDayNumber(y, m, d);
    return true;
}

// 判定一个截止时间落在哪个桶里。today 由调用方给，便于测试。
inline PlannedBucket plannedBucketOf(const std::string& deadline,
                                     int todayYear, int todayMonth, int todayDay) {
    long long day = 0;
    if (!dayNumberFromDeadline(deadline, day)) {
        return PlannedBucket::NoDate;
    }

    const long long today = civilDayNumber(todayYear, todayMonth, todayDay);
    if (day < today) return PlannedBucket::Overdue;
    if (day == today) return PlannedBucket::Today;
    if (day == today + 1) return PlannedBucket::Tomorrow;

    // 本周日 = 今天 + (6 - 今天星期几)
    const long long weekEnd = today + (6 - weekdayFromDayNumber(today));
    if (day <= weekEnd) return PlannedBucket::ThisWeek;

    // 本月最后一天 = 下个月 1 号 - 1
    const int nextMonthYear = todayMonth == 12 ? todayYear + 1 : todayYear;
    const int nextMonth = todayMonth == 12 ? 1 : todayMonth + 1;
    const long long monthEnd = civilDayNumber(nextMonthYear, nextMonth, 1) - 1;
    if (day <= monthEnd) return PlannedBucket::ThisMonth;

    return PlannedBucket::Later;
}

// 桶的显示顺序，也是排序时的主键。
inline int plannedBucketRank(PlannedBucket bucket) {
    switch (bucket) {
        case PlannedBucket::Overdue:  return 0;
        case PlannedBucket::Today:    return 1;
        case PlannedBucket::Tomorrow: return 2;
        case PlannedBucket::ThisWeek: return 3;
        case PlannedBucket::ThisMonth:return 4;
        case PlannedBucket::Later:    return 5;
        case PlannedBucket::NoDate:   return 6;
    }
    return 6;
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
