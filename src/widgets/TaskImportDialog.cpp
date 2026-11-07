// ============================================================================
//  TaskImportDialog.cpp
//  职责：导入预览表格 —— 勾选、重名提示、解析警告一眼可见
// ============================================================================

#include "TaskImportDialog.h"

#include "../ui/Theme.h"
#include "src/models/TaskDocument.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

// 状态标记：用文字与颜色区分，而不是给整行铺底色 ——
// 铺底色在深色主题上会变成刺眼的浅色块。
const char *const kMarkFatal = "✕";
const char *const kMarkDuplicate = "=";
const char *const kMarkWarn = "!";

QString priorityText(int priority) {
    switch (priority) {
        case 0: return "低";
        case 1: return "中";
        case 2: return "高";
        case 3: return "紧急";
        default: return "?";
    }
}

QString categoryText(int categoryId) {
    switch (categoryId) {
        case 0: return "其他";
        case 1: return "工作";
        case 2: return "学习";
        case 3: return "健康";
        default: return "?";
    }
}

} // namespace

TaskImportDialog::TaskImportDialog(const TaskImporter::ParseResult &parsed,
                                   const TaskDocument &doc, const QString &sourceLabel,
                                   QWidget *parent)
    : QDialog(parent), m_parsed(parsed) {
    const Theme &theme = ThemeManager::current();

    setWindowTitle("导入任务");
    setModal(true);
    setMinimumSize(860, 520);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(8);

    QLabel *title = new QLabel("导入预览", this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.5);
    title->setFont(titleFont);
    root->addWidget(title);

    // 重名标记：用文档同一套归一化规则判断，避免"两处说法不一致"。
    for (TaskImporter::ImportTask &t : m_parsed.tasks) {
        if (t.fatal || t.title.empty()) continue;
        t.duplicate = doc.hasTaskWithNormalizedTitle(t.title);
    }

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({"导入", "行", "标题", "截止", "紧急", "步骤", "列表",
                                        "标签", "提示"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
    root->addWidget(m_table, 1);

    m_warnings = new QPlainTextEdit(this);
    m_warnings->setReadOnly(true);
    m_warnings->setMaximumHeight(84);
    m_warnings->setVisible(false);
    root->addWidget(m_warnings);

    QHBoxLayout *bottom = new QHBoxLayout();
    m_hint = new QLabel("提示：勾选的条目才会导入；同名任务默认不勾，可手动勾上。", this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QString("QLabel { color: %1; }")
                              .arg(ThemeManager::hex(theme.cardMutedInk)));
    bottom->addWidget(m_hint, 1);

    QPushButton *allOn = new QPushButton("全选", this);
    QPushButton *allOff = new QPushButton("全不选", this);
    bottom->addWidget(allOn);
    bottom->addWidget(allOff);
    root->addLayout(bottom);

    QDialogButtonBox *box = new QDialogButtonBox(this);
    m_importButton = box->addButton("导入", QDialogButtonBox::AcceptRole);
    box->addButton("取消", QDialogButtonBox::RejectRole);
    root->addWidget(box);

    connect(allOn, &QPushButton::clicked, this, [this]() { setAll(true); });
    connect(allOff, &QPushButton::clicked, this, [this]() { setAll(false); });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        refreshSummary();
    });

    buildRows();
    refreshSummary();

    // 全局警告（例如「第 3 行无法识别」「字段出现在任务名之前」）
    if (!m_parsed.warnings.empty()) {
        QStringList lines;
        int shown = 0;
        for (const std::string &w : m_parsed.warnings) {
            if (shown >= 6) {
                lines << QString("… 另有 %1 条类似提示").arg(
                    static_cast<int>(m_parsed.warnings.size()) - shown);
                break;
            }
            lines << QString::fromStdString(w);
            ++shown;
        }
        m_warnings->setPlainText(lines.join("\n"));
        m_warnings->setVisible(true);
    }
    (void)sourceLabel;
}

void TaskImportDialog::buildRows() {
    const Theme &theme = ThemeManager::current();
    const int rows = static_cast<int>(m_parsed.tasks.size());
    m_table->setRowCount(rows);

    for (int r = 0; r < rows; ++r) {
        const TaskImporter::ImportTask &t = m_parsed.tasks[static_cast<std::size_t>(r)];

        // 0) 勾选
        QTableWidgetItem *check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (!t.fatal) {
            check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
            // 默认：能导入且不重名才勾上（重名默认跳过，可手动勾）
            const bool on = !t.duplicate;
            check->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        } else {
            check->setCheckState(Qt::Unchecked);
        }
        m_table->setItem(r, 0, check);

        // 1) 行号
        QTableWidgetItem *line = new QTableWidgetItem(QString::number(t.startLine));
        line->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(r, 1, line);

        // 2) 标题
        QString titleText = QString::fromStdString(t.title);
        if (t.fatal) titleText = "（缺任务名）";
        m_table->setItem(r, 2, new QTableWidgetItem(titleText));

        // 3) 截止
        m_table->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(t.deadline)));

        // 4) 紧急度
        m_table->setItem(r, 4, new QTableWidgetItem(priorityText(t.priority)));

        // 5) 步骤数
        m_table->setItem(r, 5,
                         new QTableWidgetItem(QString::number(static_cast<int>(t.steps.size()))));

        // 6) 列表（空表示默认列表）
        QString listText = QString::fromStdString(t.listName);
        if (listText.isEmpty()) listText = "（默认）";
        m_table->setItem(r, 6, new QTableWidgetItem(listText));

        // 7) 标签
        QStringList tagList;
        for (const std::string &tag : t.tags) tagList << QString::fromStdString(tag);
        m_table->setItem(r, 7, new QTableWidgetItem(tagList.join(" ")));

        // 8) 提示：致命 / 重名 / 警告
        QString note;
        QColor noteColor = theme.cardMutedInk;
        if (t.fatal) {
            note = QString("%1 无法导入：%2").arg(QString::fromUtf8(kMarkFatal),
                                                 QString::fromStdString(t.fatalReason));
            noteColor = theme.danger;
        } else {
            QStringList parts;
            if (t.duplicate) {
                parts << QString("%1 同名任务已存在").arg(QString::fromUtf8(kMarkDuplicate));
                noteColor = theme.warn;
            }
            for (const std::string &w : t.warnings) {
                parts << QString("%1 %2").arg(QString::fromUtf8(kMarkWarn),
                                               QString::fromStdString(w));
                noteColor = theme.warn;
            }
            note = parts.join("；");
        }
        QTableWidgetItem *noteItem = new QTableWidgetItem(note);
        noteItem->setForeground(noteColor);
        m_table->setItem(r, 8, noteItem);

        // 致命行整行置灰，扫一眼就知道这条不进来
        if (t.fatal) {
            for (int c = 0; c < 8; ++c) {
                if (QTableWidgetItem *it = m_table->item(r, c)) {
                    it->setForeground(theme.cardMutedInk);
                }
            }
            m_table->item(r, 0)->setCheckState(Qt::Unchecked);
        }
    }

    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(2, 240);
    m_table->setColumnWidth(8, 260);
}

int TaskImportDialog::selectedCount() const {
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (rowChecked(r)) ++n;
    }
    return n;
}

bool TaskImportDialog::rowChecked(int row) const {
    QTableWidgetItem *check = m_table->item(row, 0);
    if (!check) return false;
    if (!(check->flags() & Qt::ItemIsUserCheckable)) return false;
    return check->checkState() == Qt::Checked;
}

void TaskImportDialog::refreshSummary() {
    const int total = static_cast<int>(m_parsed.tasks.size());
    const int ok = m_parsed.countOk();
    const int warned = m_parsed.countWarned();
    const int fatal = m_parsed.countFatal();
    const int picked = selectedCount();

    QString text = QString("共 %1 条：%2 条可直接导入、%3 条有提示、%4 条无法导入；已勾选 %5 条。")
                       .arg(total)
                       .arg(ok)
                       .arg(warned)
                       .arg(fatal)
                       .arg(picked);
    m_summary->setText(text);

    if (m_importButton) {
        m_importButton->setText(picked > 0 ? QString("导入 %1 条").arg(picked) : "导入");
        m_importButton->setEnabled(picked > 0);
    }
}

void TaskImportDialog::setAll(bool on) {
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *check = m_table->item(r, 0);
        if (!check || !(check->flags() & Qt::ItemIsUserCheckable)) continue;
        check->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    refreshSummary();
}

std::vector<bool> TaskImportDialog::selectedFlags() const {
    std::vector<bool> flags;
    flags.reserve(static_cast<std::size_t>(m_table->rowCount()));
    for (int r = 0; r < m_table->rowCount(); ++r) {
        flags.push_back(rowChecked(r));
    }
    return flags;
}
