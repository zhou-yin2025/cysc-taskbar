// ============================================================================
//  TaskImporter.cpp
//  职责：解析「键：值」格式的导入文本；把结果批量写入 TaskDocument
//
//  格式要点（见 BACKLOG.md 的 F-005）：
//    * 一行一个字段，写作 键：值；全角「：」和半角「:」都认
//    * 出现「任务名：」就开始一个新任务，不需要空行分隔
//    * 宽容匹配：行首允许杂字、空格、编号（例如 "b步骤1：" / "第2步："）
//    * 未知字段不丢弃，并入备注；所有问题都记进 warnings，界面必须显示出来
//    * 只写日期不写时间的，deadline 存 10 位日期（方案 1）
// ============================================================================

#include "TaskImporter.h"

#include "TaskDocument.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTime>

#include <algorithm>
#include <cstdio>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace TaskImporter {

namespace {

// ---------------------------------------------------------------- 字符串小工具

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim(const std::string &s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool equalsIgnoreCase(const std::string &a, const std::string &b) {
    return toLowerAscii(trim(a)) == toLowerAscii(trim(b));
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// 中文数字与阿拉伯数字混排时，统一取一个数值。
int chineseNumeral(QChar c) {
    switch (c.unicode()) {
        case 0x4E00: return 1;  // 一
        case 0x4E8C: return 2;  // 二
        case 0x4E09: return 3;  // 三
        case 0x56DB: return 4;  // 四
        case 0x4E94: return 5;  // 五
        case 0x516D: return 6;  // 六
        case 0x4E03: return 7;  // 七
        case 0x516B: return 8;  // 八
        case 0x4E5D: return 9;  // 九
        default: return 0;
    }
}

// ---------------------------------------------------------------- 字段名匹配

struct KeyAlias {
    const char *alias;   // UTF-8 的别名
    const char *canon;   // 归一化后的字段名
};

// 顺序不重要：匹配时取「最长命中、位置最靠前」的那个。
const KeyAlias kAliases[] = {
    {"任务名", "任务名"},
    {"标题", "任务名"},
    {"任务标题", "任务名"},
    {"备注", "备注"},
    {"说明", "备注"},
    {"详情", "备注"},
    {"步骤", "步骤"},
    {"子任务", "步骤"},
    {"截止时间", "截止时间"},
    {"截止日期", "截止时间"},
    {"截止", "截止时间"},
    {"到期", "截止时间"},
    {"ddl", "截止时间"},
    {"提醒时间", "提醒"},
    {"提醒", "提醒"},
    {"紧急度", "紧急度"},
    {"优先级", "紧急度"},
    {"紧急", "紧急度"},
    {"分类", "分类"},
    {"类别", "分类"},
    {"标签", "标签"},
    {"tag", "标签"},
    {"列表", "列表"},
    {"所属列表", "列表"},
};

// 在一段「键」文本里找最合适的字段名。命中不了返回空串。
//
// 宽容策略：不要求字段名在行首 —— "b步骤1"、"第2步"、"★ 截止时间" 都能认出来。
std::string matchKey(const QString &keyPart) {
    const QString lowered = keyPart.trimmed().toLower();
    std::string best;
    int bestLen = 0;
    int bestPos = -1;
    for (const KeyAlias &ka : kAliases) {
        const QString alias = QString::fromUtf8(ka.alias);
        const int pos = lowered.indexOf(alias);
        if (pos < 0) continue;
        const int len = alias.size();
        if (len > bestLen || (len == bestLen && (bestPos < 0 || pos < bestPos))) {
            best = ka.canon;
            bestLen = len;
            bestPos = pos;
        }
    }
    if (!best.empty()) return best;

    // 兜底："第2步" 这种没有「步骤」二字的写法。
    if (lowered.contains(QChar(0x6B65)) && lowered.size() <= 6) return "步骤";  // 步
    return std::string();
}

// ---------------------------------------------------------------- 值解析

// 解析日期（可带时间）。成功时 out 是 "yyyy-MM-dd" 或 "yyyy-MM-dd HH:mm"。
bool parseDateValue(const std::string &raw, std::string &out, bool &hasTime) {
    QString s = QString::fromStdString(raw).trimmed();
    if (s.isEmpty()) return false;

    // 统一日期分隔符：中文年月日 / 点 / 斜杠 → 短横线
    s.replace(QChar(0x5E74), '-');   // 年
    s.replace(QChar(0x6708), '-');   // 月
    s.remove(QChar(0x65E5));         // 日
    s.replace(QChar(0xFF0F), '-');   // ／
    s.replace('.', '-');
    s.replace('/', '-');

    // 拆出时间部分：空格 / T / 逗号 / 顿号
    QString datePart = s;
    QString timePart;
    const int sep = [&s]() {
        for (int i = 0; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (c == ' ' || c == 'T' || c == 't' || c == QChar(0xFF0C) || c == ',' ||
                c == QChar(0x3001)) {
                return i;
            }
        }
        return -1;
    }();
    if (sep >= 0) {
        datePart = s.left(sep).trimmed();
        timePart = s.mid(sep + 1).trimmed();
    }

    const QStringList dparts = datePart.split('-', Qt::SkipEmptyParts);
    if (dparts.size() != 3) return false;
    bool okY = false;
    bool okM = false;
    bool okD = false;
    const int year = dparts.at(0).trimmed().toInt(&okY);
    const int month = dparts.at(1).trimmed().toInt(&okM);
    const int day = dparts.at(2).trimmed().toInt(&okD);
    if (!okY || !okM || !okD) return false;
    const QDate date(year, month, day);
    if (!date.isValid()) return false;

    char buf[32] = {0};
    if (timePart.isEmpty()) {
        hasTime = false;
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date.year(), date.month(), date.day());
        out = buf;
        return true;
    }

    // 时间：18:00 / 18:00:00 / 18点30分 / 18时
    timePart.replace(QChar(0x70B9), ':');  // 点
    timePart.replace(QChar(0x65F6), ':');  // 时
    timePart.remove(QChar(0x5206));        // 分
    const QStringList tparts = timePart.split(':', Qt::SkipEmptyParts);
    if (tparts.isEmpty()) return false;
    bool okH = false;
    const int hour = tparts.at(0).trimmed().toInt(&okH);
    int minute = 0;
    if (tparts.size() >= 2) {
        bool okMin = false;
        minute = tparts.at(1).trimmed().toInt(&okMin);
        if (!okMin) return false;
    }
    const QTime time(hour, minute);
    if (!okH || !time.isValid()) return false;

    hasTime = true;
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", date.year(), date.month(),
                  date.day(), time.hour(), time.minute());
    out = buf;
    return true;
}

// 紧急度：1..4（1 低 … 4 紧急）或中文；转成内部的 0..3。
bool parsePriority(const std::string &raw, int &out) {
    const QString s = QString::fromStdString(raw).trimmed();
    if (s.isEmpty()) return false;
    if (s.contains(QString::fromUtf8("紧急")) || s.contains(QString::fromUtf8("急"))) {
        out = 3;
        return true;
    }
    if (s.contains(QString::fromUtf8("高"))) { out = 2; return true; }
    if (s.contains(QString::fromUtf8("中"))) { out = 1; return true; }
    if (s.contains(QString::fromUtf8("低"))) { out = 0; return true; }

    // 纯数字或中文数字
    int value = 0;
    for (const QChar c : s) {
        if (c.isDigit()) {
            value = c.digitValue();
            break;
        }
        const int cn = chineseNumeral(c);
        if (cn > 0) {
            value = cn;
            break;
        }
    }
    if (value < 1 || value > 4) return false;
    out = value - 1;
    return true;
}

bool parseCategory(const std::string &raw, int &out) {
    const QString s = QString::fromStdString(raw).trimmed();
    if (s.isEmpty()) return false;
    if (s.contains(QString::fromUtf8("工作"))) { out = 1; return true; }
    if (s.contains(QString::fromUtf8("学习"))) { out = 2; return true; }
    if (s.contains(QString::fromUtf8("健康"))) { out = 3; return true; }
    if (s.contains(QString::fromUtf8("其他")) || s.contains(QString::fromUtf8("其它"))) {
        out = 0;
        return true;
    }
    return false;
}

std::vector<std::string> splitTags(const std::string &raw) {
    std::vector<std::string> out;
    std::string cur;
    const QString s = QString::fromStdString(raw);
    for (const QChar c : s) {
        const bool sep = c.isSpace() || c == QChar(0x3001) || c == QChar(0xFF0C) || c == ',' ||
                         c == '#' || c == QChar(0xFF1B) || c == ';' || c == '|';
        if (sep) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            const QByteArray utf8 = QString(c).toUtf8();
            cur.append(utf8.constData(), static_cast<std::size_t>(utf8.size()));
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// 三个重载：调用处常常是 QString 拼装的，也有直接给字面量/ std::string 的。
void addWarning(ImportTask &t, const QString &msg) {
    t.warnings.push_back(msg.toStdString());
}
void addWarning(ImportTask &t, const std::string &msg) {
    t.warnings.push_back(msg);
}
void addWarning(ImportTask &t, const char *msg) {
    t.warnings.push_back(std::string(msg));
}

} // namespace

// ---------------------------------------------------------------- ParseResult

int ParseResult::countOk() const {
    int n = 0;
    for (const ImportTask &t : tasks) {
        if (!t.fatal && !t.hasWarnings()) ++n;
    }
    return n;
}

int ParseResult::countWarned() const {
    int n = 0;
    for (const ImportTask &t : tasks) {
        if (!t.fatal && t.hasWarnings()) ++n;
    }
    return n;
}

int ParseResult::countFatal() const {
    int n = 0;
    for (const ImportTask &t : tasks) {
        if (t.fatal) ++n;
    }
    return n;
}

// ---------------------------------------------------------------- parse

ParseResult parse(const std::string &utf8Text) {
    ParseResult result;
    std::string text = utf8Text;
    // 去掉 UTF-8 BOM
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    const std::vector<std::string> lines = splitLines(text);
    result.lineCount = static_cast<int>(lines.size());

    ImportTask cur;
    int ignoredBeforeTitle = 0;
    bool hasCur = false;       // 已经遇到了「任务名」

    auto flush = [&]() {
        if (!hasCur && cur.title.empty() && cur.details.empty() && cur.steps.empty() &&
            cur.tags.empty()) {
            return;  // 空块，别生成垃圾条目
        }
        if (cur.title.empty()) {
            cur.fatal = true;
            cur.fatalReason = "缺少「任务名」，这一条无法导入";
        }
        result.tasks.push_back(cur);
        cur = ImportTask();
        hasCur = false;
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int lineNo = static_cast<int>(i) + 1;
        const std::string rawLine = trim(lines[i]);
        if (rawLine.empty()) continue;
        // 注释：整行以 # 或 // 开头（标签写在值里不受影响）
        if (rawLine[0] == '#' || (rawLine.size() >= 2 && rawLine[0] == '/' && rawLine[1] == '/')) {
            continue;
        }

        const QString line = QString::fromStdString(rawLine);
        // 找第一个冒号（全角或半角）
        int colon = -1;
        for (int k = 0; k < line.size(); ++k) {
            const QChar c = line.at(k);
            if (c == ':' || c == QChar(0xFF1A)) {
                colon = k;
                break;
            }
        }
        if (colon < 0) {
            result.warnings.push_back(
                QString("第 %1 行无法识别（缺少「键：值」）：%2")
                    .arg(lineNo)
                    .arg(line)
                    .toStdString());
            continue;
        }

        const QString keyPart = line.left(colon);
        const QString valuePart = line.mid(colon + 1).trimmed();
        const std::string key = matchKey(keyPart);

        if (key.empty()) {
            // 未知字段：并入备注（决策 4），但要留痕
            const QString note = QString("%1：%2").arg(keyPart.trimmed()).arg(valuePart);
            if (!cur.details.empty()) cur.details += "\n";
            cur.details += note.toStdString();
            addWarning(cur, QString("未知字段「%1」已并入备注").arg(keyPart.trimmed()));
            continue;
        }

        // 出现任务名 → 上一个任务收尾
        if (key == "任务名") {
            if (hasCur || !cur.title.empty()) flush();
            cur.title = valuePart.toStdString();
            cur.startLine = lineNo;
            hasCur = true;
            if (cur.title.empty()) {
                addWarning(cur, "「任务名」后面是空的");
            }
            continue;
        }

        if (!hasCur && cur.title.empty()) {
            ++ignoredBeforeTitle;
            result.warnings.push_back(
                QString("第 %1 行「%2」出现在「任务名」之前，已忽略")
                    .arg(lineNo)
                    .arg(keyPart.trimmed())
                    .toStdString());
            continue;
        }
        if (cur.startLine == 0) cur.startLine = lineNo;

        if (key == "备注") {
            if (!cur.details.empty()) cur.details += "\n";
            cur.details += valuePart.toStdString();
        } else if (key == "步骤") {
            const std::string step = valuePart.toStdString();
            if (step.empty()) {
                addWarning(cur, QString("第 %1 行步骤内容为空，已跳过").arg(lineNo));
            } else {
                cur.steps.push_back(step);
            }
        } else if (key == "截止时间") {
            std::string parsed;
            bool hasTime = false;
            if (parseDateValue(valuePart.toStdString(), parsed, hasTime)) {
                cur.deadline = parsed;
            } else {
                addWarning(cur,
                           QString("第 %1 行截止时间无法解析：「%2」，已忽略")
                               .arg(lineNo)
                               .arg(valuePart)
                               .toStdString());
            }
        } else if (key == "提醒") {
            std::string parsed;
            bool hasTime = false;
            if (parseDateValue(valuePart.toStdString(), parsed, hasTime)) {
                if (!hasTime) {
                    // 只写日期 → 当天 09:00
                    parsed += " 09:00";
                    addWarning(cur, QString("提醒只写了日期，按 %1 09:00 处理")
                                        .arg(QString::fromStdString(parsed).left(10))
                                        .toStdString());
                }
                cur.reminder = parsed;
            } else {
                addWarning(cur,
                           QString("第 %1 行提醒时间无法解析：「%2」，已忽略")
                               .arg(lineNo)
                               .arg(valuePart)
                               .toStdString());
            }
        } else if (key == "紧急度") {
            int prio = 1;
            if (parsePriority(valuePart.toStdString(), prio)) {
                cur.priority = prio;
            } else {
                addWarning(cur,
                           QString("第 %1 行紧急度无法识别：「%2」，按「中」处理")
                               .arg(lineNo)
                               .arg(valuePart)
                               .toStdString());
            }
        } else if (key == "分类") {
            int cat = 0;
            if (parseCategory(valuePart.toStdString(), cat)) {
                cur.categoryId = cat;
            } else {
                addWarning(cur,
                           QString("第 %1 行分类无法识别：「%2」，按「其他」处理")
                               .arg(lineNo)
                               .arg(valuePart)
                               .toStdString());
            }
        } else if (key == "标签") {
            const std::vector<std::string> parsed = splitTags(valuePart.toStdString());
            for (const std::string &t : parsed) cur.tags.push_back(t);
        } else if (key == "列表") {
            cur.listName = valuePart.toStdString();
        }
    }
    flush();

    // 整段文本一条任务都没解析出来，但确实有字段被忽略过 —— 说明用户漏写了「任务名」。
    // 这种情况必须显示成一条「无法导入」的记录，而不是只留一句「字段出现在任务名之前」。
    if (result.tasks.empty() && ignoredBeforeTitle > 0) {
        ImportTask orphan;
        orphan.fatal = true;
        orphan.fatalReason = "整段文本里没有出现「任务名」，无法导入";
        result.tasks.push_back(orphan);
    }

    // 提醒的边界处理
    const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
    for (ImportTask &t : result.tasks) {
        if (t.reminder.empty()) continue;

        // 已经过去的提醒：标记出来，导入时不写入（决策 5）
        if (QString::fromStdString(t.reminder) < now) {
            t.reminderPassed = true;
            addWarning(t, "提醒时间已经过去，导入时不会写入这个提醒");
        }
        // 提醒晚于截止时间：允许，但要提示
        if (!t.deadline.empty()) {
            std::string cmpDeadline = t.deadline;
            if (cmpDeadline.size() == 10) cmpDeadline += " 23:59";  // 只有日期按当天结束算
            if (t.reminder > cmpDeadline) {
                addWarning(t, "提醒时间晚于截止时间");
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------- readTextFile

bool readTextFile(const std::string &path, std::string &outUtf8, std::string &error) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("打不开文件：%1").arg(file.errorString()).toStdString();
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) {
        error = "文件是空的";
        return false;
    }

    // 1) 有 BOM 就是 UTF-8
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        outUtf8 = std::string(data.constData() + 3, static_cast<std::size_t>(data.size() - 3));
        return true;
    }

    // 2) 严格试 UTF-8：能解通就当 UTF-8
    {
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString decoded = decoder.decode(data);
        if (!decoder.hasError()) {
            outUtf8 = decoded.toStdString();
            return true;
        }
    }

    // 3) 退回 GB18030（Windows 记事本的默认编码）
#ifdef Q_OS_WIN
    {
        const int need = MultiByteToWideChar(54936 /*GB18030*/, 0, data.constData(), data.size(),
                                             nullptr, 0);
        if (need > 0) {
            std::wstring wide(static_cast<std::size_t>(need), L'\0');
            MultiByteToWideChar(54936, 0, data.constData(), data.size(), wide.data(), need);
            const QString decoded = QString::fromWCharArray(wide.data(), need);
            // 解出大量替换字符说明不是 GBK
            const int bad = decoded.count(QChar(0xFFFD));
            if (bad * 20 < decoded.size()) {
                outUtf8 = decoded.toStdString();
                return true;
            }
        }
    }
#endif

    error = "不是 UTF-8 也不是 GBK 编码，请另存为 UTF-8 后重试";
    return false;
}

// ---------------------------------------------------------------- applyTo

namespace {

// 新列表用的配色：按顺序取，尽量不与已有列表重复。
const char *const kListPalette[] = {
    "#FF007F", "#00AA88", "#0077FF", "#32C8FF",
    "#8B5CF6", "#F59E0B", "#EF4444", "#14B8A6",
};
constexpr int kListPaletteSize = static_cast<int>(sizeof(kListPalette) / sizeof(kListPalette[0]));

std::string pickListColor(const TaskDocument &doc, int sequence) {
    const auto &lists = doc.lists();
    for (int attempt = 0; attempt < kListPaletteSize * 2; ++attempt) {
        const std::string candidate = kListPalette[(sequence + attempt) % kListPaletteSize];
        bool used = false;
        for (const TaskList &l : lists) {
            if (equalsIgnoreCase(l.color, candidate)) {
                used = true;
                break;
            }
        }
        if (!used) return candidate;
    }
    return kListPalette[sequence % kListPaletteSize];
}

} // namespace

ApplyStats applyTo(TaskDocument &doc, const std::vector<ImportTask> &tasks,
                   const std::vector<bool> &selected) {
    ApplyStats stats;

    // 整批只压一次撤销快照：导入 20 条，一次 Ctrl+Z 全撤。
    bool snapshotTaken = false;

    // 导入的任务排到最前：取当前最小 manualOrder，然后往下减。
    int baseOrder = 0;
    for (const Task &t : doc.tasks()) {
        if (t.manualOrder < baseOrder) baseOrder = t.manualOrder;
    }
    int nextOrder = baseOrder;

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const bool want = i < selected.size() && selected[i];
        const ImportTask &src = tasks[i];
        if (!want || src.fatal || src.title.empty()) {
            ++stats.skipped;
            continue;
        }
        if (!snapshotTaken) {
            doc.beginUndo();
            snapshotTaken = true;
        }

        // 列表：空 → 默认；有名字 → 找同名（大小写与空格无关），没有就建
        int listId = -1;
        if (!src.listName.empty()) {
            for (const TaskList &l : doc.lists()) {
                if (equalsIgnoreCase(l.name, src.listName)) {
                    listId = l.id;
                    break;
                }
            }
            if (listId < 0) {
                listId = doc.addList(src.listName,
                                     pickListColor(doc, static_cast<int>(stats.createdLists.size())));
                if (listId >= 0) stats.createdLists.push_back(src.listName);
            }
        }
        if (listId < 0) listId = doc.defaultListId();

        Task task;
        task.title = src.title;
        task.details = src.details;
        task.priority = src.priority;
        task.deadline = src.deadline;
        task.categoryId = src.categoryId;
        task.status = 0;
        task.listId = listId;
        task.tags = src.tags;
        // 提醒：已过去的就不写（决策 5 的边界）
        if (!src.reminderPassed) task.reminder = src.reminder;

        for (std::size_t k = 0; k < src.steps.size(); ++k) {
            Subtask sub;
            sub.title = src.steps[k];
            sub.done = false;
            task.subtasks.push_back(sub);
        }

        task.manualOrder = nextOrder--;
        if (doc.addTask(task) >= 0) ++stats.added;
    }

    return stats;
}

std::vector<std::string> knownKeys() {
    return {"任务名", "备注", "步骤", "截止时间", "提醒", "紧急度", "分类", "标签", "列表"};
}

} // namespace TaskImporter
