#include "TaskDraft.h"

namespace {

// End of the working day, used when the user gives a date without a time.
constexpr int kDefaultHour = 23;
constexpr int kDefaultMinute = 59;

} // namespace

TaskDraft::TaskDraft() {
    restart();
}

void TaskDraft::clearInput() {
    m_inputError.clear();
}

void TaskDraft::setPriority(int priority) {
    if (priority >= 0 && priority <= 3) {
        m_priority = priority;
    }
}

void TaskDraft::setCategory(int category) {
    if (category >= 0 && category <= 3) {
        m_category = category;
    }
}

TaskDraft::Outcome TaskDraft::restart() {
    m_stage = StageTitle;
    m_title.clear();
    m_details.clear();
    m_priority = 1;
    m_deadline = QDateTime();      // 默认无截止时间
    m_category = 0;
    clearInput();
    return StageChanged;
}

void TaskDraft::loadFrom(const Task& task) {
    m_title = QString::fromStdString(task.title);
    m_details = QString::fromStdString(task.details);
    m_priority = task.priority;
    m_category = task.categoryId;
    m_stage = StageTitle;

    const QDateTime parsed = QDateTime::fromString(QString::fromStdString(task.deadline),
                                                   "yyyy-MM-dd HH:mm");
    // 旧数据里可能是空的或只有日期；解析不出来就当作「无截止时间」。
    m_deadline = parsed.isValid() ? parsed : QDateTime();
    clearInput();
}

QDateTime TaskDraft::dateFromInput(const QString& input) const {
    const QDate today = QDate::currentDate();
    const QString trimmed = input.trimmed();

    // 空输入 = 不设截止时间（不是「今天」）。
    if (trimmed.isEmpty()) {
        return QDateTime();
    }

    // Split the optional time off the date part: "10/12 14:30".
    QString datePart = trimmed;
    QTime time(kDefaultHour, kDefaultMinute);
    const QStringList chunks = trimmed.split(' ', Qt::SkipEmptyParts);
    if (chunks.size() >= 2) {
        const QTime parsedTime = QTime::fromString(chunks.last(), "H:mm");
        if (parsedTime.isValid()) {
            time = parsedTime;
            datePart = trimmed.left(trimmed.length() - chunks.last().length()).trimmed();
        } else if (chunks.size() > 2) {
            return QDateTime();      // more than "date time" is not understood
        }
    }

    const QStringList parts = datePart.split('/');
    QDate day;
    if (parts.size() == 1) {
        day = QDate(today.year(), today.month(), parts[0].toInt());
    } else if (parts.size() == 2) {
        day = QDate(today.year(), parts[0].toInt(), parts[1].toInt());
    } else if (parts.size() == 3) {
        const int year = parts[0].toInt();
        if (year < 2000 || year > 2100) return QDateTime();
        day = QDate(year, parts[1].toInt(), parts[2].toInt());
    } else {
        return QDateTime();
    }

    // QDate rejects impossible dates (2/31, 13/1, ...): never store them.
    if (!day.isValid()) return QDateTime();
    return QDateTime(day, time);
}

QString TaskDraft::deadlineText() const {
    return m_deadline.isValid() ? m_deadline.toString("yyyy-MM-dd HH:mm") : QString();
}

TaskDraft::Outcome TaskDraft::handleDigit(int digit) {
    if (digit < 1 || digit > 4) return None;

    if (m_stage == StagePriority) {
        m_priority = digit - 1;      // 1..4 -> 0 low .. 3 urgent
        m_stage = StageDate;
        clearInput();
        return StageChanged;
    }
    if (m_stage == StageCategory) {
        m_category = digit - 1;      // 1..4 -> Other/Work/Study/Health
        return Committed;
    }
    return None;
}

TaskDraft::Outcome TaskDraft::handleTab(const QString& inputText) {
    switch (m_stage) {
        case StageTitle: {
            const QString title = inputText.trimmed();
            if (title.isEmpty()) {
                m_inputError = QStringLiteral("请先输入标题");
                return Rejected;
            }
            m_title = title;
            m_stage = StagePriority;
            clearInput();
            return StageChanged;
        }
        case StagePriority:
            m_stage = StageDate;
            clearInput();
            return StageChanged;

        case StageDate: {
            const QString raw = inputText.trimmed();
            // 空输入表示跳过日期，不算错误。
            if (raw.isEmpty()) {
                m_deadline = QDateTime();
                m_stage = StageCategory;
                clearInput();
                return StageChanged;
            }
            const QDateTime parsed = dateFromInput(raw);
            if (!parsed.isValid()) {
                m_inputError = QStringLiteral("日期「%1」无效，请试 10/12 或 2026/10/12")
                                   .arg(raw);
                return Rejected;
            }
            m_deadline = parsed;
            m_stage = StageCategory;
            clearInput();
            return StageChanged;
        }
        case StageCategory:
        default:
            return restart();
    }
}

TaskDraft::Outcome TaskDraft::handleEnter(const QString& inputText) {
    if (m_stage == StageTitle) {
        const QString title = inputText.trimmed();
        if (title.isEmpty()) {
            m_inputError = QStringLiteral("请先输入标题");
            return Rejected;
        }
        m_title = title;
        return Committed;
    }

    if (m_stage == StageDate) {
        const QString raw = inputText.trimmed();
        if (raw.isEmpty()) {
            m_deadline = QDateTime();       // Enter 且未填日期 = 不设截止时间
            return Committed;
        }
        const QDateTime parsed = dateFromInput(raw);
        if (!parsed.isValid()) {
            m_inputError = QStringLiteral("日期「%1」无效，请试 10/12 或 2026/10/12")
                               .arg(raw);
            return Rejected;
        }
        m_deadline = parsed;
        return Committed;
    }

    if (m_stage == StagePriority || m_stage == StageCategory) {
        return Committed;
    }
    return None;
}

TaskDraft::Outcome TaskDraft::handleEscape() {
    return restart();
}

TaskDraft::Outcome TaskDraft::choosePriority(int priority) {
    if (priority < 0 || priority > 3) return None;
    m_priority = priority;
    m_stage = StageDate;
    clearInput();
    return StageChanged;
}

TaskDraft::Outcome TaskDraft::chooseDateOffset(int offsetDays) {
    m_deadline = QDateTime(QDate::currentDate().addDays(offsetDays),
                           QTime(kDefaultHour, kDefaultMinute));
    m_stage = StageCategory;
    clearInput();
    return StageChanged;
}
TaskDraft::Outcome TaskDraft::chooseCategory(int category) {
    if (category < 0 || category > 3) return None;
    m_category = category;
    return Committed;
}

Task TaskDraft::toTask() const {
    Task t;
    t.title = m_title.toStdString();
    t.details = m_details.toStdString();
    t.priority = m_priority;
    t.deadline = deadlineText().toStdString();
    t.categoryId = m_category;
    t.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    t.status = 0;
    return t;
}
