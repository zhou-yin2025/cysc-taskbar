#ifndef TASKIMPORTER_H
#define TASKIMPORTER_H

// ============================================================================
//  TaskImporter.h
//  职责：把「键：值」格式的纯文本解析成任务，并可批量写入 TaskDocument
//  依赖：只依赖标准库与 QtCore（日期），不依赖任何 widget —— 便于单元测试
// ============================================================================

#include <string>
#include <vector>

class TaskDocument;

namespace TaskImporter {

// 一个解析结果。预览界面直接读它；fatal 的任务不写入。
struct ImportTask {
    std::string title;
    std::string details;
    std::vector<std::string> steps;
    // 只有日期时是 "yyyy-MM-dd"（10 位）；带时间时是 "yyyy-MM-dd HH:mm"
    std::string deadline;
    // 提醒一律带时间（"yyyy-MM-dd HH:mm"）；空表示没有
    std::string reminder;
    int priority = 1;      // 0 低 / 1 中 / 2 高 / 3 紧急
    std::string listName;  // 空表示落到默认列表
    std::vector<std::string> tags;

    int startLine = 0;                  // 在原文里的起始行号（从 1 开始），预览定位用
    std::vector<std::string> warnings;  // 本条的非致命问题
    bool fatal = false;                 // true 表示无法导入
    std::string fatalReason;

    // 由调用方（预览层）填：是否与已有任务重名，以及是否被勾选导入
    bool duplicate = false;
    bool reminderPassed = false;  // 提醒时间已过 —— 导入时不写入该字段

    bool hasWarnings() const { return !warnings.empty(); }
};

struct ParseResult {
    std::vector<ImportTask> tasks;
    std::vector<std::string> warnings;  // 全局问题（带行号）
    int lineCount = 0;

    int countOk() const;      // 可导入且无警告
    int countWarned() const;  // 可导入但有警告
    int countFatal() const;   // 无法导入
};

// 解析整段文本。text 必须是 UTF-8。
ParseResult parse(const std::string &utf8Text);

// 读文本文件：自动识别 UTF-8（含 BOM）与 GBK/GB18030。
// 成功返回 true 并填 outUtf8；失败返回 false 并填 error。
bool readTextFile(const std::string &path, std::string &outUtf8, std::string &error);

// 批量写入。selected[i] 为 true 表示第 i 条要导入（越界视为 false）。
// 只在开头 push 一次撤销快照 —— 整批导入算一个撤销步骤。
struct ApplyStats {
    int added = 0;
    int skipped = 0;
    std::vector<std::string> createdLists;  // 本次自动新建的列表名
};
ApplyStats applyTo(TaskDocument &doc, const std::vector<ImportTask> &tasks,
                   const std::vector<bool> &selected);

// 给界面做格式提示用：全部认识的键名。
std::vector<std::string> knownKeys();

} // namespace TaskImporter

#endif // TASKIMPORTER_H
