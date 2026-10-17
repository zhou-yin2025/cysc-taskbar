#include "TaskGroupDialog.h"

#include "../ui/Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString themeHex(const QColor &c) { return ThemeManager::hex(c); }
} // namespace

// 按当前主题拼这一窗的样式表。
void TaskGroupDialog::applyTheme() {
    const Theme &th = ThemeManager::current();
    setStyleSheet(QString("QDialog { background-color: %1; }"
                          "QDialogButtonBox QPushButton { background-color: %2; color: %3; "
                          "  border: 1px solid %3; padding: 3px 14px; font-size: 11px; }"
                          "QDialogButtonBox QPushButton:hover { background-color: %3; color: %4; }")
                      .arg(themeHex(th.panelBg), themeHex(th.toolbarBg),
                           themeHex(th.accent), themeHex(th.onAccent)));
}
TaskGroupDialog::TaskGroupDialog(TaskDocument* doc, QWidget* parent)
    : QDialog(parent), m_doc(doc) {
    setWindowTitle("列表分组");
    setMinimumWidth(260);
    applyTheme();

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    QLabel* hint = new QLabel(
        "勾选两个或更多列表，确定后它们归入同一分组。", this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }")
                            .arg(themeHex(ThemeManager::current().cardMutedInk)));
    root->addWidget(hint);

    m_layout = new QVBoxLayout();
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(3);

    if (m_doc) {
        for (const TaskList& list : m_doc->lists()) {
            QCheckBox* box = new QCheckBox(QString::fromStdString(list.name), this);
            box->setStyleSheet(QString("QCheckBox { color: %1; font-size: 11px; }")
                                   .arg(themeHex(ThemeManager::current().cardTitleInk)));
            // 已经在分组里的列表也允许勾选：再勾一个就会把它并进同一组。
            if (list.groupId >= 0) {
                box->setToolTip("该列表已属于一个分组");
            }
            m_layout->addWidget(box);

            Option option;
            option.listId = list.id;
            option.name = QString::fromStdString(list.name);
            option.box = box;
            m_boxes.push_back(option);
        }
    }
    root->addLayout(m_layout);
    root->addStretch(1);

    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("归为一组");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

std::vector<int> TaskGroupDialog::selectedListIds() const {
    std::vector<int> out;
    for (const Option& option : m_boxes) {
        if (option.box && option.box->isChecked()) out.push_back(option.listId);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool TaskGroupDialog::setCheckedByListName(const QString& name, bool checked) {
    for (const Option& option : m_boxes) {
        if (option.name == name && option.box) {
            option.box->setChecked(checked);
            return true;
        }
    }
    return false;
}
