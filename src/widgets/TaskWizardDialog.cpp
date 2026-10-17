#include "TaskWizardDialog.h"

#include "../ui/Theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

const char* kPriorityNames[4] = {"① 低", "② 中", "③ 高", "④ 紧急"};
// 分组默认四项；以后做设置时改成可配置（读一个列表即可），这里先写死。
const char* kCategoryNames[4] = {"① 其他", "② 工作", "③ 学习", "④ 健康"};

const char* kStageTitles[4] = {"第 1 栏：任务名", "第 2 栏：紧急度", "第 3 栏：分组", "第 4 栏：日期"};

QString themeHex(const QColor &c) { return ThemeManager::hex(c); }

} // namespace

TaskWizardDialog::TaskWizardDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("新建任务（逐栏）");
    setModal(true);
    setMinimumWidth(400);
    applyTheme();

    buildUi();
    m_draft.restart();
    setStage(TaskDraft::StageTitle);
    // Enter 直接完成：向导里 Enter 是有意义的「结束定义」。
    // 这里不设默认按钮，避免与标题栏的 Enter 行为冲突。
}

// 按当前主题拼这一窗的样式表。
void TaskWizardDialog::applyTheme() {
    const Theme &th = ThemeManager::current();
    setStyleSheet(
        QString("QDialog { background-color: %1; }"
                "QLabel { color: %2; font-size: 11px; }"
                "QLineEdit { background-color: %3; color: %4; border: 1px solid %2; "
                "  padding: 5px; font-size: 13px; "
                "  selection-background-color: %2; selection-color: %5; }")
            .arg(themeHex(th.panelBg), themeHex(th.accent), themeHex(th.toolbarBg),
                 themeHex(th.cardTitleInk), themeHex(th.onAccent)));
}

QString TaskWizardDialog::choiceStyle(const QString& accent, bool selected) const {
    const Theme &th = ThemeManager::current();
    const QString bg = themeHex(th.toolbarBg);
    const QString ink = themeHex(th.onAccent);
    if (selected) {
        return QString("QPushButton { background-color: %1; color: %2; border: 1px solid %1; "
                       "padding: 5px 14px; font-size: 12px; font-weight: bold; }")
            .arg(accent, ink);
    }
    return QString("QPushButton { background-color: %1; color: %3; border: 1px solid %3; "
                   "padding: 5px 14px; font-size: 12px; }"
                   "QPushButton:hover { background-color: %3; color: %2; }")
        .arg(bg, ink, accent);
}

void TaskWizardDialog::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    m_stepLabel = new QLabel(this);
    m_stepLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: bold; }")
                                .arg(themeHex(ThemeManager::current().statusChipBg[3])));
    root->addWidget(m_stepLabel);

    m_stack = new QStackedWidget(this);

    // ---- 第 1 栏：任务名 ----
    QWidget *titlePage = new QWidget(m_stack);
    QVBoxLayout *titleLayout = new QVBoxLayout(titlePage);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);
    m_titleEdit = new QLineEdit(titlePage);
    m_titleEdit->setPlaceholderText("输入任务名，然后按 Tab 继续");
    m_titleEdit->installEventFilter(this);
    titleLayout->addWidget(m_titleEdit);
    titleLayout->addStretch();
    m_stack->addWidget(titlePage);

    // ---- 第 2 栏：紧急度 ----
    m_priorityPage = new QWidget(m_stack);
    QHBoxLayout *priorityLayout = new QHBoxLayout(m_priorityPage);
    priorityLayout->setContentsMargins(0, 0, 0, 0);
    priorityLayout->setSpacing(8);
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kPriorityNames[i], m_priorityPage);
        btn->setToolTip(QString("按 %1 选择").arg(i + 1));
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_draft.setPriority(i);
            refreshChoices();
            advance();                      // 点选后自动进入下一栏
        });
        m_priorityButtons[i] = btn;
        priorityLayout->addWidget(btn);
    }
    priorityLayout->addStretch();
    m_stack->addWidget(m_priorityPage);

    // ---- 第 3 栏：分组 ----
    m_categoryPage = new QWidget(m_stack);
    QHBoxLayout *categoryLayout = new QHBoxLayout(m_categoryPage);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(8);
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kCategoryNames[i], m_categoryPage);
        btn->setToolTip(QString("按 %1 选择").arg(i + 1));
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_draft.setCategory(i);
            refreshChoices();
            advance();                      // 选完分组进入日期栏
        });
        m_categoryButtons[i] = btn;
        categoryLayout->addWidget(btn);
    }
    categoryLayout->addStretch();
    m_stack->addWidget(m_categoryPage);

    // ---- 第 4 栏：日期（可留空） ----
    QWidget *datePage = new QWidget(m_stack);
    QVBoxLayout *dateLayout = new QVBoxLayout(datePage);
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->setSpacing(6);
    m_dateEdit = new QLineEdit(datePage);
    m_dateEdit->setPlaceholderText("可留空：直接按 Tab / Enter 完成");
    m_dateEdit->installEventFilter(this);
    dateLayout->addWidget(m_dateEdit);
    dateLayout->addStretch();
    m_stack->addWidget(datePage);

    root->addWidget(m_stack, 1);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }")
                                .arg(themeHex(ThemeManager::current().accentDim)));
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }")
                              .arg(themeHex(ThemeManager::current().accent)));
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }")
                                 .arg(themeHex(ThemeManager::current().danger)));
    m_errorLabel->setWordWrap(true);
    root->addWidget(m_errorLabel);
}

void TaskWizardDialog::setStage(TaskDraft::Stage stage) {
    m_draft.setStageDirect(stage);

    // ---- 枚举值 ≠ 页签下标 ----
    //
    // TaskDraft::Stage 的声明顺序是 Title, Priority, Date, Category（历史原因，
    // "Date" 排在 "Category" 前面），也就是 StageDate == 2、StageCategory == 3。
    // 而这里的页签顺序是 任务名 / 紧急度 / 分组 / 日期 —— 分组在第 3 页、日期在第 4 页。
    //
    // 原来直接写 m_stack->setCurrentIndex(stage)，于是走到「分组」那一栏时
    // 显示的是**日期页**（反之亦然）—— 用户看到的现象就是
    // "没看到分栏界面，直接跳到了日期"。
    //
    // 所以这里必须显式映射，绝不能拿枚举当下标用。
    int page = 0;
    switch (stage) {
        case TaskDraft::StageTitle:    page = 0; break;
        case TaskDraft::StagePriority: page = 1; break;
        case TaskDraft::StageCategory: page = 2; break;
        case TaskDraft::StageDate:     page = 3; break;
    }

    switch (stage) {
        case TaskDraft::StageTitle:
            m_stack->setCurrentIndex(page);
            m_titleEdit->setFocus();
            m_titleEdit->selectAll();
            m_hintLabel->setText("Tab 下一栏　Enter 直接完成　Esc 取消");
            break;
        case TaskDraft::StagePriority:
            m_stack->setCurrentIndex(page);
            m_hintLabel->setText("按 1-4 选择紧急度　Tab 跳过　Enter 完成　Backspace 返回");
            break;
        case TaskDraft::StageCategory:
            m_stack->setCurrentIndex(page);
            m_hintLabel->setText("按 1-4 选择分组　Tab 跳过　Enter 完成　Backspace 返回");
            break;
        case TaskDraft::StageDate:
            m_stack->setCurrentIndex(page);
            m_dateEdit->setText(m_draft.hasDeadline() ? m_draft.deadline().toString("M/d") : QString());
            m_dateEdit->setFocus();
            m_dateEdit->selectAll();
            m_hintLabel->setText("输入 2026/10/12、10/12 或 12　留空=无截止时间　Enter 完成　Backspace 返回");
            break;
    }

    m_stepLabel->setText(kStageTitles[page]);
    m_errorLabel->clear();
    refreshChoices();
    refreshSummary();
}

void TaskWizardDialog::refreshChoices() {
    for (int i = 0; i < 4; ++i) {
        m_priorityButtons[i]->setStyleSheet(
            choiceStyle(themeHex(ThemeManager::current().priorityAccent[i]), m_draft.priority() == i));
        m_categoryButtons[i]->setStyleSheet(
            choiceStyle(themeHex(ThemeManager::current().categoryAccent[i]), m_draft.category() == i));
    }
}

void TaskWizardDialog::refreshSummary() {
    const QString title = m_draft.title().isEmpty() ? QString("（未命名）") : m_draft.title();
    const QString deadline = m_draft.hasDeadline() ? m_draft.deadlineText() : QString("无截止时间");
    // 去掉名称里的「① 」前缀，摘要只显示纯文字。
    const QString priorityName = QString(kPriorityNames[m_draft.priority()]).mid(2);
    const QString categoryName = QString(kCategoryNames[m_draft.category()]).mid(2);
    m_summary->setText(QString("　%1　·　%2　·　%3　·　%4")
                           .arg(title)
                           .arg(priorityName)
                           .arg(categoryName)
                           .arg(deadline));
}

bool TaskWizardDialog::stageUsesChoices() const {
    const TaskDraft::Stage stage = m_draft.stage();
    return stage == TaskDraft::StagePriority || stage == TaskDraft::StageCategory;
}

void TaskWizardDialog::advance() {
    switch (m_draft.stage()) {
        case TaskDraft::StageTitle: {
            collectCurrentStage();
            if (m_draft.title().isEmpty()) {
                m_errorLabel->setText("请先输入任务名");
                m_titleEdit->setFocus();
                return;
            }
            setStage(TaskDraft::StagePriority);
            return;
        }
        case TaskDraft::StagePriority:
            setStage(TaskDraft::StageCategory);
            return;
        case TaskDraft::StageCategory:
            setStage(TaskDraft::StageDate);
            return;
        case TaskDraft::StageDate: {
            const QString raw = m_dateEdit->text().trimmed();
            if (raw.isEmpty()) {
                m_draft.clearDeadline();
                commit();
                return;
            }
            const QDateTime parsed = m_draft.dateFromInput(raw);
            if (!parsed.isValid()) {
                m_errorLabel->setText(QString("日期「%1」无效，请试 10/12 或 2026/10/12").arg(raw));
                m_dateEdit->setFocus();
                return;
            }
            m_draft.setDeadline(parsed);
            commit();
            return;
        }
    }
}

void TaskWizardDialog::retreat() {
    switch (m_draft.stage()) {
        case TaskDraft::StageTitle:
            return;                                  // 已经在第一栏
        case TaskDraft::StagePriority:
            setStage(TaskDraft::StageTitle);
            return;
        case TaskDraft::StageCategory:
            setStage(TaskDraft::StagePriority);
            return;
        case TaskDraft::StageDate:
            setStage(TaskDraft::StageCategory);
            return;
    }
}

// 把**当前这一栏**的输入收进 draft。
//
// 抽出来是因为它有两个调用方，而原来只有 commit() 里写了一半：
//   - commit()（按 Enter 完成）
//   - testAdvance()（自检里模拟 Tab）
// 返回 false 表示输入不合法、应当停在当前栏。
bool TaskWizardDialog::collectCurrentStage() {
    switch (m_draft.stage()) {
        case TaskDraft::StageTitle:
            if (m_titleEdit) m_draft.setTitle(m_titleEdit->text().trimmed());
            return true;
        case TaskDraft::StageDate: {
            if (!m_dateEdit) return true;
            const QString raw = m_dateEdit->text().trimmed();
            if (raw.isEmpty()) {
                m_draft.clearDeadline();
                return true;
            }
            const QDateTime parsed = m_draft.dateFromInput(raw);
            if (!parsed.isValid()) {
                m_errorLabel->setText(
                    QString("日期「%1」无效，请试 10/12 或 2026/10/12").arg(raw));
                m_dateEdit->setFocus();
                m_dateEdit->selectAll();
                return false;
            }
            m_draft.setDeadline(parsed);
            return true;
        }
        case TaskDraft::StagePriority:
        case TaskDraft::StageCategory:
            // 选择题栏：没选就用当前默认值
            return true;
    }
    return true;
}

void TaskWizardDialog::commit() {
    // 关键：先把**当前这一栏**的输入收进 draft，再判定能不能完成。
    //
    // 原来这里只同步了标题栏，于是停在日期栏时按 Enter 会直接 accept()，
    // 用户刚敲进去的截止时间根本没被解析 —— 表现为"设好时间按回车，时间没保存"。
    // （只有按 Tab 才走 advance() 里的日期解析，所以这个 bug 只在 Enter 路径上。）
    if (!collectCurrentStage()) return;      // 输入不合法：停在这一栏

    if (!m_draft.isValid()) {
        m_errorLabel->setText("请先输入任务名");
        m_titleEdit->setFocus();
        return;
    }
    accept();
}

Task TaskWizardDialog::task() const {
    return m_draft.toTask();
}

// ---- 界面自检用的钩子 ----

void TaskWizardDialog::testGoToStage(TaskDraft::Stage stage, const QString& dateText) {
    setStage(stage);
    if (stage == TaskDraft::StageDate && m_dateEdit) {
        m_dateEdit->setText(dateText);
    }
    // 拨到标题栏时顺手填个标题。否则「推进到下一栏」会被空标题拦下 ——
    // 那不是流程 bug，只是测试没模拟"用户已经输入过"。
    if (stage == TaskDraft::StageTitle && m_titleEdit
        && m_titleEdit->text().trimmed().isEmpty()) {
        m_titleEdit->setText(QString::fromUtf8("向导测试任务"));
        m_draft.setTitle("向导测试任务");
    }
}

void TaskWizardDialog::testCommit() {
    commit();
}

void TaskWizardDialog::testAdvance() {
    // 先模拟"用户已经把这一栏填好了"，再走真实的推进路径
    collectCurrentStage();
    advance();
}

TaskDraft::Stage TaskWizardDialog::testStage() const {
    return m_draft.stage();
}

int TaskWizardDialog::testPageIndex() const {
    return m_stack ? m_stack->currentIndex() : -1;
}

int TaskWizardDialog::testPageCount() const {
    return m_stack ? m_stack->count() : -1;
}

QString TaskWizardDialog::testStepText() const {
    return m_stepLabel ? m_stepLabel->text() : QString();
}

bool TaskWizardDialog::focusNextPrevChild(bool next) {
    // 向导自己处理 Tab：返回 false 让 Qt 不要把焦点移来移去。
    Q_UNUSED(next);
    return false;
}

bool TaskWizardDialog::eventFilter(QObject *watched, QEvent *event) {
    // 输入框会先吃掉 Tab / Enter / Backspace，这里先一步把它们转成向导动作。
    if (event->type() == QEvent::KeyPress
        && (watched == m_titleEdit || watched == m_dateEdit)) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                advance();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                commit();
                return true;
            default:
                break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void TaskWizardDialog::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            advance();
            event->accept();
            return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            commit();
            event->accept();
            return;

        case Qt::Key_Backspace:
            // 选择题栏目的 Backspace 用来回退，输入栏目交给文本框自己处理。
            if (stageUsesChoices()) {
                retreat();
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    // 选择题栏目：1-4 直接选定并前进。
    if (stageUsesChoices()) {
        const int key = event->key();
        if (key >= Qt::Key_1 && key <= Qt::Key_4) {
            const int index = key - Qt::Key_1;
            if (m_draft.stage() == TaskDraft::StagePriority) {
                m_draft.setPriority(index);
                setStage(TaskDraft::StageCategory);
            } else {
                m_draft.setCategory(index);
                setStage(TaskDraft::StageDate);
            }
            event->accept();
            return;
        }
    }

    QDialog::keyPressEvent(event);
}
