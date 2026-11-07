#ifndef TASKIMPORTDIALOG_H
#define TASKIMPORTDIALOG_H

// ============================================================================
//  TaskImportDialog.h
//  职责：导入前的预览 —— 每条解析结果显示出来，可勾选，问题必须看得见
//  依赖：Qt Widgets、models/TaskImporter.h
// ============================================================================

#include <QDialog>
#include <vector>

#include "src/models/TaskImporter.h"

class TaskDocument;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// 预览窗口。它只负责「让用户确认」，不写数据 —— 写入由调用方拿
// selectedFlags() 去调 TaskImporter::applyTo()，这样撤销、存档、刷新
// 都留在主窗口那条既有路径上。
class TaskImportDialog : public QDialog {
    Q_OBJECT
public:
    // parsed 会被复制一份（里面要填 duplicate 标记）；doc 用来判断重名。
    TaskImportDialog(const TaskImporter::ParseResult &parsed, const TaskDocument &doc,
                     const QString &sourceLabel, QWidget *parent = nullptr);

    // 与 parsed.tasks 一一对应的勾选结果。
    std::vector<bool> selectedFlags() const;

private:
    void buildRows();
    void refreshSummary();
    void setAll(bool on);
    bool rowChecked(int row) const;
    int selectedCount() const;

    TaskImporter::ParseResult m_parsed;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_hint = nullptr;
    QPlainTextEdit *m_warnings = nullptr;
    QPushButton *m_importButton = nullptr;
};

#endif // TASKIMPORTDIALOG_H
