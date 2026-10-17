#include "TaskDialog.h"

#include "../ui/Theme.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char* kPriorityNames[4] = {"低", "中", "高", "紧急"};
const char* kCategoryNames[4] = {"其他", "工作", "学习", "健康"};

// 取色统一走主题（原来这里是各自一份写死的十六进制）。
QString themeHex(const QColor &c) { return ThemeManager::hex(c); }

} // namespace

TaskDialog::TaskDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("新建任务");
    setModal(true);
    setMinimumWidth(420);
    applyTheme();

    buildUi();
    refreshChips();
    m_titleEdit->setFocus();
}

// 按当前主题拼这一窗的样式表。主窗口换主题后新建这个窗口就会自动是新配色；
// 窗口已经开着时由 MainWindow 调 applyTheme() 刷新。
void TaskDialog::applyTheme() {
    const Theme &th = ThemeManager::current();
    setStyleSheet(
        QString("QDialog { background-color: %1; }"
                "QLabel { color: %2; font-size: 11px; }"
                "QLineEdit, QPlainTextEdit, QDateTimeEdit { background-color: %3; color: %4; "
                "  border: 1px solid %2; padding: 4px; font-size: 12px; "
                "  selection-background-color: %2; selection-color: %5; }"
                "QDateTimeEdit::drop-down { border: none; }"
                "QDialogButtonBox QPushButton { background-color: %3; color: %2; "
                "  border: 1px solid %2; padding: 4px 16px; font-size: 11px; }"
                "QDialogButtonBox QPushButton:hover { background-color: %2; color: %5; }")
            .arg(themeHex(th.panelBg),      // 1 窗口底
                 themeHex(th.accent),       // 2 强调色（文字/描边）
                 themeHex(th.toolbarBg),    // 3 输入框底
                 themeHex(th.cardTitleInk), // 4 输入文字
                 themeHex(th.onAccent)));   // 5 反白文字
}

QString TaskDialog::chipStyle(const QString& accent, bool active) const {
    const Theme &th = ThemeManager::current();
    const QString bg = themeHex(th.toolbarBg);
    const QString ink = themeHex(th.onAccent);
    if (active) {
        return QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %1; "
            "padding: 3px 10px; font-size: 11px; font-weight: bold; }").arg(accent, ink);
    }
    return QString(
        "QPushButton { background-color: %1; color: %3; border: 1px solid %3; "
        "padding: 3px 10px; font-size: 11px; }"
        "QPushButton:hover { background-color: %3; color: %2; }").arg(bg, ink, accent);
}

void TaskDialog::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    QFormLayout *form = new QFormLayout();
    form->setSpacing(8);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText("必填，例如：完成季度总结");
    form->addRow("标题：", m_titleEdit);

    // 优先级
    QWidget *priorityRow = new QWidget(this);
    QHBoxLayout *priorityLayout = new QHBoxLayout(priorityRow);
    priorityLayout->setContentsMargins(0, 0, 0, 0);
    priorityLayout->setSpacing(6);
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kPriorityNames[i], priorityRow);
        btn->setToolTip(QString("快捷键 %1").arg(i + 1));
        connect(btn, &QPushButton::clicked, this, [this, i]() { setPriority(i); });
        m_priorityButtons[i] = btn;
        priorityLayout->addWidget(btn);
    }
    priorityLayout->addStretch();
    form->addRow("优先级：", priorityRow);

    // 分类
    QWidget *categoryRow = new QWidget(this);
    QHBoxLayout *categoryLayout = new QHBoxLayout(categoryRow);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(6);
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kCategoryNames[i], categoryRow);
        btn->setToolTip(QString("Ctrl+%1").arg(i + 1));
        connect(btn, &QPushButton::clicked, this, [this, i]() { setCategory(i); });
        m_categoryButtons[i] = btn;
        categoryLayout->addWidget(btn);
    }
    categoryLayout->addStretch();
    form->addRow("分类：", categoryRow);

    // 截止时间：可勾选（不勾选 = 无截止时间），精确到分钟
    QWidget *deadlineRow = new QWidget(this);
    QHBoxLayout *deadlineLayout = new QHBoxLayout(deadlineRow);
    deadlineLayout->setContentsMargins(0, 0, 0, 0);
    deadlineLayout->setSpacing(6);
    m_hasDeadline = new QCheckBox("设置", deadlineRow);
    m_hasDeadline->setToolTip("取消勾选表示这个任务没有截止时间");
    m_hasDeadline->setStyleSheet(QString("QCheckBox { color: %1; font-size: 11px; }")
                                   .arg(themeHex(ThemeManager::current().accent)));
    connect(m_hasDeadline, &QCheckBox::toggled, this, &TaskDialog::onDeadlineToggled);
    deadlineLayout->addWidget(m_hasDeadline);

    m_deadlineEdit = new QDateTimeEdit(deadlineRow);
    m_deadlineEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    m_deadlineEdit->setCalendarPopup(true);
    connect(m_deadlineEdit, &QDateTimeEdit::dateTimeChanged,
            this, &TaskDialog::onDeadlineChanged);
    deadlineLayout->addWidget(m_deadlineEdit, 1);
    form->addRow("截止时间：", deadlineRow);

    // 备注 / 详情
    m_detailsEdit = new QPlainTextEdit(this);
    m_detailsEdit->setPlaceholderText("可选：补充说明、步骤、链接等");
    m_detailsEdit->setFixedHeight(90);
    form->addRow("备注：", m_detailsEdit);

    root->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }")
                                  .arg(themeHex(ThemeManager::current().danger)));
    m_errorLabel->setWordWrap(true);
    root->addWidget(m_errorLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    QPushButton *ok = buttons->addButton("确定", QDialogButtonBox::AcceptRole);
    QPushButton *cancel = buttons->addButton("取消", QDialogButtonBox::RejectRole);
    connect(ok, &QPushButton::clicked, this, &TaskDialog::onAccept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(buttons);
}

void TaskDialog::refreshChips() {
    for (int i = 0; i < 4; ++i) {
        m_priorityButtons[i]->setStyleSheet(
            chipStyle(themeHex(ThemeManager::current().priorityAccent[i]), m_draft.priority() == i));
        m_categoryButtons[i]->setStyleSheet(
            chipStyle(themeHex(ThemeManager::current().categoryAccent[i]), m_draft.category() == i));
    }
    // 这里刻意不碰 m_deadlineEdit：draft 里的时间是窗口打开时的快照，
    // 用户可能已经改过时间控件。以前在这里回写会导致「改完时间再点优先级/
    // 分类，时间被重置」。
}

void TaskDialog::setPriority(int priority) {
    m_draft.setPriority(priority);
    refreshChips();
}

void TaskDialog::setCategory(int category) {
    m_draft.setCategory(category);
    refreshChips();
}

void TaskDialog::setDraft(const Task& task) {
    m_draft.loadFrom(task);
    m_editingId = task.id;
    m_createdAt = QString::fromStdString(task.createdAt);
    setWindowTitle(QString("编辑任务 #%1").arg(task.id));

    m_titleEdit->setText(m_draft.title());
    m_detailsEdit->setPlainText(m_draft.details());

    // 没有截止时间时勾选框不勾、时间控件禁用，但仍显示一个可用值。
    const bool has = m_draft.hasDeadline();
    m_hasDeadline->setChecked(has);
    m_deadlineEdit->setDateTime(has ? m_draft.deadline() : QDateTime::currentDateTime());
    m_deadlineEdit->setEnabled(has);
    if (!has) {
        m_draft.clearDeadline();
    }
    refreshChips();
}

void TaskDialog::onDeadlineChanged(const QDateTime& value) {
    // 用户改时间时同步进 draft，避免其它字段刷新时把时间冲掉。
    if (!m_hasDeadline || m_hasDeadline->isChecked()) {
        m_draft.setDeadline(value);
    }
}

void TaskDialog::onDeadlineToggled(bool enabled) {
    if (m_deadlineEdit) {
        m_deadlineEdit->setEnabled(enabled);
    }
    if (enabled) {
        m_draft.setDeadline(m_deadlineEdit ? m_deadlineEdit->dateTime()
                                           : QDateTime::currentDateTime());
    } else {
        m_draft.clearDeadline();
    }
}

Task TaskDialog::task() const {
    Task t = m_draft.toTask();
    t.id = m_editingId;
    if (!m_createdAt.isEmpty()) {
        t.createdAt = m_createdAt.toStdString();
    }
    return t;
}

void TaskDialog::onAccept() {
    const QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty()) {
        m_errorLabel->setText("请先输入标题");
        m_titleEdit->setFocus();
        return;                     // 留在窗口里，不丢用户已经填的内容
    }

    m_draft.setTitle(title);
    m_draft.setDetails(m_detailsEdit->toPlainText());
    m_draft.setDeadline(m_deadlineEdit->dateTime());
    accept();
}

void TaskDialog::keyPressEvent(QKeyEvent *event) {
    // 1-4 选优先级、Ctrl+1-4 选分类：把 V0.0.2 的快捷键带进窗口。
    const int key = event->key();
    if (key >= Qt::Key_1 && key <= Qt::Key_4) {
        const int index = key - Qt::Key_1;
        if (event->modifiers() & Qt::ControlModifier) {
            setCategory(index);
        } else {
            setPriority(index);
        }
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
