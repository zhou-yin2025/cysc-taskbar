#include "TaskSidebar.h"

#include "../ui/Theme.h"

#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// 侧栏配色全部取自当前主题。换主题后调 refreshTheme() 重新拼一遍。
QString hexOf(const QColor &c) { return ThemeManager::hex(c); }

QString rowStyle(bool active) {
    const Theme &th = ThemeManager::current();
    // 选中行的文字用强调色，未选中的用主题的侧栏文字色
    return QString("QPushButton { text-align: left; padding: 5px 8px; border: none; "
                   "border-radius: 5px; color: %1; font-size: 11px; background: %2; }"
                   "QPushButton:hover { background: %3; }")
        .arg(active ? hexOf(th.accent) : hexOf(th.sidebarText),
             active ? hexOf(th.sidebarActive) : QString("transparent"),
             hexOf(th.sidebarHover));
}

QString badgeStyle(bool active) {
    const Theme &th = ThemeManager::current();
    return QString("QLabel { color: %1; font-size: 10px; background: transparent; }")
        .arg(active ? hexOf(th.accent) : hexOf(th.sidebarMuted));
}

QString dotStyle(const QString& color) {
    return QString("QLabel { color: %1; font-size: 10px; background: transparent; }").arg(color);
}

QLabel *makeHeader(const QString& text, QWidget *parent) {
    QLabel *label = new QLabel(text, parent);
    label->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                                 "padding: 2px 8px; background: transparent; }")
                            .arg(hexOf(ThemeManager::current().sidebarMuted)));
    return label;
}

} // namespace

TaskSidebar::TaskSidebar(QWidget *parent) : QWidget(parent) {
    setObjectName("taskSidebar");
    setStyleSheet(QString("QWidget#taskSidebar { background-color: %1; }")
                      .arg(ThemeManager::hex(ThemeManager::current().sidebarBg)));
    setFixedWidth(152);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ---------------- 智能视图 ----------------
    m_smartContainer = new QWidget(this);
    m_smartLayout = new QVBoxLayout(m_smartContainer);
    m_smartLayout->setContentsMargins(0, 0, 0, 0);
    m_smartLayout->setSpacing(1);

    m_smartLayout->addWidget(makeHeader("智能视图", m_smartContainer));
    addSmartRow(SmartView::MyDay, "我的一天", "#FFC814");
    addSmartRow(SmartView::Important, "重要", "#FF5C8A");
    addSmartRow(SmartView::Planned, "计划内", "#7C5CFF");
    addSmartRow(SmartView::All, "全部", "#32C8FF");
    addSmartRow(SmartView::Completed, "已完成", "#8A8AA8");
    m_smartLayout->addStretch(1);
    root->addWidget(m_smartContainer);

    // ---------------- 列表区标题 + 新建 ----------------
    QWidget *listHeaderRow = new QWidget(this);
    QHBoxLayout *headerLayout = new QHBoxLayout(listHeaderRow);
    headerLayout->setContentsMargins(0, 6, 0, 0);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(makeHeader("列表", listHeaderRow));
    headerLayout->addStretch(1);

    m_addListButton = new QPushButton("＋", listHeaderRow);
    m_addListButton->setToolTip("新建列表");
    m_addListButton->setFixedSize(20, 20);
    m_addListButton->setCursor(Qt::PointingHandCursor);
    m_addListButton->setStyleSheet(
        QString("QPushButton { color: %1; background: transparent; border: none; "
                "font-size: 13px; font-weight: bold; }"
                "QPushButton:hover { color: %2; }").arg(ThemeManager::hex(ThemeManager::current().sidebarText), ThemeManager::hex(ThemeManager::current().accent)));
    connect(m_addListButton, &QPushButton::clicked, this, &TaskSidebar::addListRequested);
    headerLayout->addWidget(m_addListButton);

    // 分组：把若干列表并成一个分组（底层是并查集）。
    m_groupButton = new QPushButton("⊞", listHeaderRow);
    m_groupButton->setObjectName("groupListsButton");
    m_groupButton->setToolTip("把若干列表归为一组");
    m_groupButton->setFixedSize(20, 20);
    m_groupButton->setCursor(Qt::PointingHandCursor);
    m_groupButton->setStyleSheet(
        QString("QPushButton { color: %1; background: transparent; border: none; "
                "font-size: 12px; }"
                "QPushButton:hover { color: %2; }").arg(ThemeManager::hex(ThemeManager::current().sidebarText), ThemeManager::hex(ThemeManager::current().accent)));
    connect(m_groupButton, &QPushButton::clicked, this, &TaskSidebar::groupListsRequested);
    headerLayout->addWidget(m_groupButton);
    root->addWidget(listHeaderRow);

    // ---------------- 列表（可滚动） ----------------
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }"
                          "QScrollArea > QWidget > QWidget { background: transparent; }");

    m_listContainer = new QWidget();
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(1);
    m_listLayout->addStretch(1);
    scroll->setWidget(m_listContainer);
    root->addWidget(scroll, 1);
}

void TaskSidebar::setDocument(TaskDocument *doc) {
    m_doc = doc;
    refresh();
}

// ---------------------------------------------------------------- 智能视图

void TaskSidebar::addSmartRow(SmartView view, const QString& text, const QString& color) {
    QWidget *row = new QWidget(m_smartContainer);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(3);

    m_smartLayout->addWidget(row);

    QLabel *dot = new QLabel("●", row);
    dot->setStyleSheet(dotStyle(color));
    dot->setFixedWidth(12);

    QPushButton *button = new QPushButton(text, row);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setStyleSheet(rowStyle(false));

    QLabel *badge = new QLabel("", row);
    badge->setStyleSheet(badgeStyle(false));
    badge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    badge->setMinimumWidth(16);

    layout->addWidget(dot);
    layout->addWidget(button, 1);
    layout->addWidget(badge);

    SmartRow entry;
    entry.view = view;
    entry.button = button;
    entry.badge = badge;
    m_smartButtons.push_back(entry);

    connect(button, &QPushButton::clicked, this, [this, view]() {
        m_smartViewSelected = true;
        m_view = view;
        m_listId = -1;
        updateHighlight();
        emit smartViewChosen(view);
    });
}

// ------------------------------------------------------------------ 列表区

void TaskSidebar::rebuildListRows() {
    // 清掉旧行（保留末尾的 stretch）
    for (const ListRow& row : m_listRows) {
        m_listLayout->removeWidget(row.container);
        row.container->deleteLater();
    }
    m_listRows.clear();
    for (QWidget* header : m_groupHeaders) {
        m_listLayout->removeWidget(header);
        header->deleteLater();
    }
    m_groupHeaders.clear();

    if (!m_doc) return;

    // 按分组聚类：同组的列表排在一起，组前插入一行组标题。
    // 分组号是「组内最小的列表 id」，所以排序后同组自然相邻。
    std::vector<TaskList> ordered = m_doc->lists();
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const TaskList& a, const TaskList& b) {
                         const int ga = a.groupId < 0 ? 1 << 30 : a.groupId;
                         const int gb = b.groupId < 0 ? 1 << 30 : b.groupId;
                         if (ga != gb) return ga < gb;
                         return a.id < b.id;
                     });

    int currentGroup = -2;      // -2 = 还没遇到任何组
    int groupSeen = 0;
    for (const TaskList& list : ordered) {
        const bool grouped = list.groupId >= 0;
        if (grouped && list.groupId != currentGroup) {
            currentGroup = list.groupId;
            ++groupSeen;
            QLabel* header = new QLabel(QString("分组 %1").arg(groupSeen), m_listContainer);
            header->setStyleSheet(
                QString("QLabel { color: %1; font-size: 9px; font-weight: bold; "
                        "padding: 4px 8px 0 4px; background: transparent; }")
                    .arg(ThemeManager::hex(ThemeManager::current().sidebarMuted)));
            header->setToolTip("同一分组的列表（由并查集维护）");
            m_listLayout->insertWidget(m_listLayout->count() - 1, header);
            m_groupHeaders.push_back(header);
        } else if (!grouped && currentGroup != -1) {
            // 进入「无分组」区段，之后不再插入组标题
            currentGroup = -1;
        }

        QWidget *container = new QWidget(m_listContainer);
        QHBoxLayout *layout = new QHBoxLayout(container);
        // 组内列表缩进一格，视觉上归到组标题下
        layout->setContentsMargins(grouped ? 14 : 0, 0, 4, 0);
        layout->setSpacing(3);

        QLabel *dot = new QLabel("●", container);
        dot->setStyleSheet(dotStyle(QString::fromStdString(list.color)));
        dot->setFixedWidth(12);

        QPushButton *button = new QPushButton(QString::fromStdString(list.name), container);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        button->setStyleSheet(rowStyle(false));
        // 提示里要写明「右键」——否则用户不知道列表能改名/删除。
        // （之前侧栏完全没有编辑入口，测试堆出来的重名列表清不掉。）
        QString tip = "右键：重命名 / 改颜色";
        if (list.groupId >= 0) tip += " / 移出分组";
        if (list.isDefault) tip = "默认列表（不可删除）\n" + tip;
        else tip += " / 删除列表";
        button->setToolTip(tip);
        dot->setToolTip(tip);

        QLabel *badge = new QLabel("", container);
        badge->setStyleSheet(badgeStyle(false));
        badge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        badge->setMinimumWidth(16);

        layout->addWidget(dot);
        layout->addWidget(button, 1);
        layout->addWidget(badge);
        // 插在 stretch 之前
        m_listLayout->insertWidget(m_listLayout->count() - 1, container);

        const int listId = list.id;
        connect(button, &QPushButton::clicked, this, [this, listId]() {
            m_smartViewSelected = false;
            m_listId = listId;
            updateHighlight();
            emit listChosen(listId);
        });

        // 右键菜单：列表的编辑入口。
        // 没有这个菜单，用户建出来的列表就只能一直堆着（既不能改名也不能删），
        // 测试几次之后侧栏就会被一堆重名列表塞满。
        for (QWidget *w : {static_cast<QWidget*>(container), static_cast<QWidget*>(button),
                           static_cast<QWidget*>(dot), static_cast<QWidget*>(badge)}) {
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this,
                    [this, listId](const QPoint& pos) {
                        showListMenu(listId, mapToGlobal(pos));
                    });
        }

        ListRow row;
        row.listId = listId;
        row.container = container;
        row.button = button;
        row.badge = badge;
        m_listRows.push_back(row);
    }
}

void TaskSidebar::showListMenu(int listId, const QPoint& globalPos) {
    if (!m_doc) return;
    const TaskList *list = nullptr;
    for (const TaskList& l : m_doc->lists()) {
        if (l.id == listId) { list = &l; break; }
    }
    if (!list) return;

    const bool isDefault = list->isDefault;
    const bool grouped = list->groupId >= 0;
    const int taskCount = m_doc->countInList(listId);

    QMenu menu(this);
    QAction *renameAct = menu.addAction("重命名列表…");
    QAction *colorAct = menu.addAction("更改颜色…");
    menu.addSeparator();
    QAction *ungroupAct = menu.addAction("移出分组");
    ungroupAct->setEnabled(grouped);
    menu.addSeparator();
    QAction *deleteAct = menu.addAction("删除列表");
    deleteAct->setEnabled(!isDefault);       // 默认列表不可删
    if (isDefault) {
        deleteAct->setToolTip("默认列表不能删除");
        renameAct->setEnabled(true);
    }
    if (taskCount > 0 && !isDefault) {
        deleteAct->setToolTip(QString("该列表里的 %1 个任务会移到默认列表").arg(taskCount));
    }

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == renameAct)        emit renameListRequested(listId);
    else if (chosen == colorAct)    emit changeListColorRequested(listId);
    else if (chosen == ungroupAct)  emit removeListFromGroupRequested(listId);
    else if (chosen == deleteAct)   emit deleteListRequested(listId);
}

void TaskSidebar::refresh() {
    if (!m_doc) return;

    // 智能视图的数量徽标
    const int counts[5] = {
        m_doc->countInMyDay(),
        m_doc->countStarred(),
        m_doc->countPlanned(),
        static_cast<int>(m_doc->tasks().size()),
        m_doc->countCompleted(),
    };
    for (SmartRow& row : m_smartButtons) {
        const int index = static_cast<int>(row.view);
        const int count = (index >= 0 && index < 5) ? counts[index] : 0;
        row.badge->setText(count > 0 ? QString::number(count) : QString());
    }

    // 列表行需要重建（名称/颜色/数量都可能变）
    rebuildListRows();
    for (const ListRow& row : m_listRows) {
        const int count = m_doc->countInList(row.listId);
        row.badge->setText(count > 0 ? QString::number(count) : QString());
    }

    // 当前选中项若已被删除，回退到「我的一天」
    if (!m_smartViewSelected && m_doc->findList(m_listId) == nullptr) {
        m_smartViewSelected = true;
        m_view = SmartView::MyDay;
        m_listId = -1;
    }
    updateHighlight();
}

void TaskSidebar::updateHighlight() {
    for (SmartRow& row : m_smartButtons) {
        const bool active = m_smartViewSelected && row.view == m_view;
        row.button->setStyleSheet(rowStyle(active));
        row.badge->setStyleSheet(badgeStyle(active));
    }
    for (ListRow& row : m_listRows) {
        const bool active = !m_smartViewSelected && row.listId == m_listId;
        row.button->setStyleSheet(rowStyle(active));
        row.badge->setStyleSheet(badgeStyle(active));
    }
}

void TaskSidebar::selectSmartView(SmartView view) {
    m_smartViewSelected = true;
    m_view = view;
    m_listId = -1;
    updateHighlight();
    emit smartViewChosen(view);
}

void TaskSidebar::selectList(int listId) {
    if (!m_doc || m_doc->findList(listId) == nullptr) return;
    m_smartViewSelected = false;
    m_listId = listId;
    updateHighlight();
    emit listChosen(listId);
}
