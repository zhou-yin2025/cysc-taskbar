#ifndef DOCUMENTSTORE_H
#define DOCUMENTSTORE_H

// ============================================================================
//  DocumentStore.h
//  职责：文档级持久化：列表/任务/子任务/标签/顺序/分组 的存取与迁移
//  依赖：models/TaskDocument.h
// ============================================================================

#include <QSqlDatabase>
#include <QString>
#include <vector>

#include "src/models/TaskDocument.h"

// 文档级持久化：把一个 TaskDocument（列表 + 任务 + 子任务 + 标签）写进 SQLite。
//
// 与 DatabaseManager 的职责边界：
//   * DatabaseManager 负责「连接 + 建表 + 单条任务的读写」；
//   * DocumentStore 负责「整篇文档」的存取、标签序列化与子任务表维护。
//
// 标签用 Unit Separator (0x1F) 连接，这样一个标签里出现逗号或竖线也不会串。
class DocumentStore {
public:
    DocumentStore();
    ~DocumentStore();

    DocumentStore(const DocumentStore&) = delete;
    DocumentStore& operator=(const DocumentStore&) = delete;

    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;

    // 整篇写入：先删后插，保证内容与内存一致。
    bool saveDocument(const TaskDocument& doc);
    // 整篇读取；成功时替换 doc 的内容（含索引重建）。
    bool loadDocument(TaskDocument& doc);

    QString lastError() const { return m_lastError; }

    // 标签列表 <-> 单列文本
    static QString encodeTags(const std::vector<std::string>& tags);
    static std::vector<std::string> decodeTags(const QString& text);

private:
    bool createSchema();
    bool saveLists(const TaskDocument& doc);
    bool saveTasks(const TaskDocument& doc);
    bool saveSubtasks(const TaskDocument& doc);
    bool loadLists(TaskDocument& doc);
    bool loadTasks(TaskDocument& doc);
    bool loadSubtasks(TaskDocument& doc);

    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_opened = false;
    QString m_lastError;
};

#endif
