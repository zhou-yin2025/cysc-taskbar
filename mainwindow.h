#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHash>
#include <QSize>
#include <QSystemTrayIcon>
#include <QPoint>
#include <QSet>
#include <QVector>
#include "src/models/TaskDocument.h"
#include "src/models/TaskQuery.h"
#include "src/core/DocumentStore.h"
#include "src/widgets/TaskSidebar.h"
#include "src/widgets/TaskVisualizer.h"

class QCompleter;
class QTimer;
class QCompleter;

class MainWindowTestAccess;

// 任务列表：唯一的作用是把「拖拽放下」这件事转发给主窗口。
// 直接给 MainWindow 装 dropEvent 是没用的——任务行是列表里的子控件，
// 事件由 QListWidget 自己处理，不会冒泡到窗口。
class TaskListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit TaskListWidget(QWidget *parent = nullptr) : QListWidget(parent) {}

signals:
    // 行已经被移到新位置后发出。
    void rowMoved();

protected:
    void dropEvent(QDropEvent *event) override {
        QListWidget::dropEvent(event);
        emit rowMoved();
    }
};

class MainWindow : public QWidget {
    Q_OBJECT
    // The GUI self-test drives the real widgets and slots; a friend is simpler
    // and safer than widening the public API just for tests.
    friend class MainWindowTestAccess;
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Resolves a writable location for the task database.
    static QString resolveStoragePath();

    // Headless smoke test of the model + query path used by the list view.
    // Returns 0 on success, 1 on failure. Invoked with `--selftest`.
    static int runSelfTest();

    // Builds a real window off-screen and exercises the list interactions
    // (expand/collapse, status cycle, category filter, search). Invoked with
    // `--guitest`, which is the only way to verify widget wiring without
    // synthesising input events.
    static int runGuiTest();

    // Opens the inline detail editor for the first visible task. Used by
    // `--expand-first` so the detail pane can be inspected on screen (and by
    // screenshot tooling) without simulating a mouse click.
    void expandFirstVisibleTask();

    // Opens the staged new-task wizard. Used by `--wizard` for the same reason.
    void openNewTaskWizard();

    // 打开数据结构可视化面板（演示用）。`--visualize` 会在启动时调用它。
    void openVisualizer();
    // 打开可视化面板并切到指定页签（0=哈希 1=Trie 2=线段树 3=并查集）。用于截图。
    void openVisualizerTab(int tabIndex);
    // 把可视化面板四个页签导出成 PNG（用于使用文档插图）。
    // 返回成功保存的张数；0 表示失败。
    int exportVisualizerImages(const QString& prefix);

    // 让程序用 Qt 自己把窗口渲染成图片（不依赖抓屏，也不依赖图片格式插件）。
    // 返回成功保存的张数。用于使用文档插图与无抓屏环境下的视觉检查。
    int renderToImage(const QString& path);

    // 用色块按钮选列表颜色（替代原来的十六进制字符串下拉框）。
    // 公开是为了让 --color-dialog 能直接打开它做人工检查/截图。
    bool pickListColor(QString& colorInOut);

    // 把当前主题铺到界面上（换主题后必须调一次）。
    // 公开是为了让 --theme 启动参数与截图脚本能直接用。
    void applyThemeToWidgets();

protected:
    void paintEvent(QPaintEvent *event) override;
    // 缩窄到 kAutoCompactWidth 以下时自动进入紧凑模式（仅用户拖边时触发）。
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    // 列表视口的鼠标处理：单击展开详情，双击推进状态。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int testVisibleCount() const { return m_taskList->count(); }

private slots:
    void onNewTask();          // 逐栏向导（Enter / Ctrl+N）
    void onNewTaskFull();      // 完整表单（Ctrl+Shift+N）
    void onEditCurrentTask();
    void onSearchChanged(const QString& text);
    void onCategoryFilterClicked(int category);
    void onHideDoneToggled(bool checked);
    void onSortClicked();
    void onUndoRequested();
    void onAddList();
    // 标题栏按钮
    void toggleOnTop();          // 图钉：置顶 / 取消置顶
    void toggleMaximize();       // 最大化 / 还原
    // 「只显示任务」的紧凑模式：隐藏侧栏与工具栏，只留任务列表，
    // 并把窗口吸附到当前屏幕右上角。
    void toggleCompactMode();
    void applyCompactMode();
    // 打开设置窗口，并把设置应用一遍。
    void openSettings();
    void applySettings();
    // 按设置里的档位放大全局基准字号与窗口尺寸。
    void applyUiScale();
    // 把窗口对齐到当前屏幕右上角（带一点边距）。
    void snapToTopRight();
    // 侧栏列表的编辑操作（右键菜单触发）。
    void onRenameList(int listId);
    void onDeleteList(int listId);
    void onChangeListColor(int listId);
    void onRemoveListFromGroup(int listId);
    // 打开「列表分组」对话框，把勾选的列表并成一组（并查集）。
    void onGroupLists();
    // 展开/收起「建议」面板。
    void onToggleSuggestions();
    // 重建「建议」面板内容（逾期 > 今天到期 > 最近添加）。
    void refreshSuggestions();
    // 把某条建议加入「我的一天」。
    void addSuggestionToMyDay(int taskId);
    void onItemClicked(QListWidgetItem *item);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onItemContextMenu(const QPoint &pos);
    // 拖拽结束后由 TaskListWidget::rowMoved 触发：把新顺序写回模型。
    void onRowsMoved();
    void onClickTimerTimeout();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onQuit();
    void onReminderTick();

private:
    // 分类筛选：-1 表示全部。
    enum { CategoryAll = -1 };

    // 列表排序方式；点击按钮循环切换。
    enum SortMode { SortCreatedAsc, SortDeadlineAsc, SortManual, SortModeCount };

    // 无边框窗口要自己判定「鼠标在哪个边/角」，用来拖拽调整窗口大小。
    // SmNone 表示不在边框上。
    enum ResizeEdge {
        SmNone = 0,
        SmLeft = 1, SmRight = 2, SmTop = 4, SmBottom = 8,
        SmTopLeft = SmTop | SmLeft,
        SmTopRight = SmTop | SmRight,
        SmBottomLeft = SmBottom | SmLeft,
        SmBottomRight = SmBottom | SmRight,
    };

    TaskDocument m_doc;
    DocumentStore m_store;
    // 文档（任务 + 列表）是否从库里读出来了；false 只在读库失败时出现。
    bool m_docLoaded = false;

    // 工具栏
    QLineEdit *m_search = nullptr;
    // 搜索补全（候选来自 TaskDocument 里的 Trie 前缀索引）。
    QCompleter *m_searchCompleter = nullptr;
    QPushButton *m_newButton = nullptr;
    // 打开数据结构可视化面板。
    QPushButton *m_visualizeButton = nullptr;
    QCheckBox *m_hideDone = nullptr;

    // 分类筛选行
    QWidget *m_categoryRow = nullptr;
    QPushButton *m_categoryButtons[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    TaskListWidget *m_taskList = nullptr;
    QPushButton *m_sortButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    // 是否已经提示过「窗口收进托盘了」；只提示一次，不然每次都弹很烦。
    bool m_trayHintShown = false;
    QTimer *m_reminderTimer = nullptr;
    QSet<int> m_remindedIds;
    bool m_dragging = false;
    QPoint m_dragPos;
    // 是否置顶（标题栏图钉控制，默认置顶；记住用户的选择）。
    bool m_onTop = true;
    // 是否处于最大化，以及还原时用的几何。
    // 自己记而不用 isMaximized()：无边框 + Qt::Tool 时它不可靠，
    // 会导致「要按两下才还原」。
    bool m_maximized = false;
    QRect m_normalGeometry;
    // 紧凑模式（只显示任务）：进入前记住窗口几何，退出时还原。
    bool m_compact = false;
    QRect m_geometryBeforeCompact;
    int m_expandedBeforeCompact = -1;
    // 紧凑模式下要整块隐藏的控件。
    QWidget *m_newFullButton = nullptr;
    QWidget *m_categoryHost = nullptr;
    // 「只显示任务」的开关。
    QPushButton *m_compactButton = nullptr;
    // 设置按钮（多功能自定义）。
    QPushButton *m_settingsButton = nullptr;
    // 行密度：0 紧凑 / 1 标准 / 2 宽松。
    int m_density = 1;
    // 上一次铺到界面上的主题 key，用来判断"主题真的变了没有"。
    QString m_appliedThemeKey;
    // 刚刚被标记完成、需要播一次"画线动画"的任务 id。
    QSet<int> m_strikeAnimateIds;
    // 正在调整大小的边/角（SmNone 表示没在调整），以及按下时的快照。
    int m_resizeEdge = SmNone;
    QPoint m_resizeStartGlobal;
    QRect m_resizeStartGeometry;
    // 「文档库已打开、可以用了」：打开失败时界面会提示任务无法保存。
    bool m_dbReady = false;

    // 当前视图状态
    int m_categoryFilter = CategoryAll;
    SortMode m_sortMode = SortCreatedAsc;
    bool m_hideDoneChecked = false;
    // 左侧栏决定的视图：要么看某个智能视图，要么看某个列表。
    bool m_useSmartView = true;
    SmartView m_smartView = SmartView::All;
    int m_listFilter = -1;
    TaskSidebar *m_sidebar = nullptr;

    // 「建议」面板（对应 To Do 的建议区）：折叠式，默认收起。
    QPushButton *m_suggestToggle = nullptr;
    QWidget *m_suggestBox = nullptr;
    QVBoxLayout *m_suggestLayout = nullptr;
    bool m_suggestOpen = false;
    int m_suggestCount = 0;

    // 数据结构可视化面板（非模态，随时可刷新）。
    TaskVisualizer *m_visualizer = nullptr;
    int m_expandedId = -1;
    bool m_buildingList = false;
    // 单击/双击判定：第一次按下立即执行单击（跟手）；双击窗口内再按下同一条
    // => 双击，此时把展开状态还原成双击之前的样子，做到「双击只改状态」。
    int m_pendingClickId = -1;
    bool m_clickWindowOpen = false;
    QTimer *m_clickTimer = nullptr;
    int m_expandedBeforeDoubleClick = -1;
    // 延迟展开：按下时先不动，等确认「不是拖拽」再展开。
    //
    // 为什么：原来按下就立刻展开，于是想拖拽的用户会先看到详情弹开、
    // 拖动开始时又缩回去 —— 两下闪烁，很卡眼。
    QTimer *m_expandDeferTimer = nullptr;
    int m_pendingExpandId = -1;
    // 按下时的位置与「是否已经判定为拖拽」，用来区分点击和拖拽。
    QPoint m_pressPos;
    bool m_pressWasDrag = false;
    // 用户正在拖窗口边缘改大小 / 缩窄自动进紧凑的宽度阈值。
    bool m_resizingByUser = false;
    static constexpr int kAutoCompactWidth = 460;
    // 界面缩放：当前百分比、基准字号、基准窗口尺寸（避免累积放大）。
    int m_scalePercent = 150;
    qreal m_baseFontPointSize = 9.0;
    bool m_baseFontPointSizeValid = false;
    QSize m_baseWindowSize;
    // 「进行中」任务边框流动光效：一个共享定时器 + 统一的进度值。
    QTimer *m_glowTimer = nullptr;
    qreal m_glowProgress = 0.0;
    // 事件过滤器被调用的鼠标事件数（界面自检用来确认事件确实送到了列表）。
    int m_mouseEventCount = 0;
    // 正在被拖拽的任务 id：按下时记下，dropEvent 用它确定被拖的是哪一条。
    int m_draggingTaskId = -1;
    // 每个任务行的实际高度（展开后要变高），建表时算一次给 setSizeHint 用。
    QHash<int, QSize> m_rowSizes;
    // 折叠状态下的行高，展开后还原用。
    QHash<int, QSize> m_collapsedHeights;
    // 行号 -> 任务 id（顺序与列表一致，方便按行取任务）。
    QVector<int> m_rowTaskIds;
    // 任务 id -> (列表项, 行控件)。展开/收起就地改，不重建列表，
    // 否则单击会把双击序列打断（第二次按下时 itemAt 已经找不到行）。
    QHash<int, QListWidgetItem*> m_rowItems;
    QHash<int, QWidget*> m_rowWidgets;

    static const int TITLE_BAR_HEIGHT = 24;
    // 边框热区宽度：鼠标离窗口边缘这么近就算要调整大小。
    static const int RESIZE_MARGIN = 6;
    // 标题栏每个窗口按钮的宽度
    static const int WIN_BUTTON_W = 22;
    QRect titleBarRect() const;
    // 标题栏右侧四个按钮，从右往左：关闭 / 最大化 / 最小化 / 置顶。
    QRect closeButtonRect() const;
    QRect maximizeButtonRect() const;
    QRect minimizeButtonRect() const;
    QRect pinButtonRect() const;

    // 鼠标位置落在哪条边/角上（不在边框返回 SmNone）。
    int resizeEdgeAt(const QPoint& pos) const;
    // 该边/角对应的鼠标指针形状。
    Qt::CursorShape cursorForEdge(int edge) const;
    // 按当前正在拖拽的边/角调整窗口几何。
    void applyResize(const QPoint& globalPos);

    void setupToolbar();
    void setupCategoryRow();
    void setupTray();
    // 生成托盘图标（多尺寸，深底 + 青色对勾）。
    QIcon makeTrayIcon() const;
    void setupReminder();

    void refreshList();
    // 推进「进行中」任务的光效动画（一个定时器驱动全部行）。
    void tickGlow();
    // 当前是否有「进行中」的任务（决定主窗口要不要画流动光效）。
    bool hasDoingTask() const;
    // 如果没有「进行中」的任务就停掉定时器；有则确保在跑。
    void ensureGlowRunning();
    // 就地展开/收起详情区：只改行高与可见性，不重建列表。
    void applyExpansion();
    // 双击后把展开状态还原成双击之前的样子（双击只改状态）。
    void restoreExpansionAfterDoubleClick() {
        m_expandedId = m_expandedBeforeDoubleClick;
        applyExpansion();
    }
    void updateCategoryRow();
    void updateSortButton();
    // 落库并刷新列表（两个新建窗口共用）。
    void createTask(const Task& task);
    // 双击：按 待办 → 进行中 → 已完成 → 待办 推进。
    void advanceStatus(int id);
    // 延迟到事件循环下一轮再推进状态。双击必须用这个：advanceStatus 会重建
    // 整个列表，而双击那一刻 Qt 还在派发鼠标事件给列表项，立刻重建会把正在
    // 派发的列表项删掉，导致闪退。
    void advanceStatusDeferred(int id);
    // 就地替换某一行的控件（不 clear 列表）。凡是由「列表项上的交互」触发的
    // 刷新都必须走这条路径，否则会把正在派发事件的列表项删掉 -> 闪退。
    void refreshRow(int taskId);
    // 就地刷新某一行并保持它展开（详情面板自动保存后调用）。
    void refreshRowKeepExpanded(int taskId);
    // 改状态：新模型没有 changeStatus，取出来改字段再 updateTask（自带快照）。
    bool applyStatus(int id, int status);
    QWidget *createRowWidget(const Task& task);
    QWidget *createDetailEditor(const Task& task);
    // 详情面板是**按需创建**的，不是每行都建。
    //
    // 为什么：详情编辑器有 40 多个控件、6 个 lambda 和一堆堆分配。原来每行都
    // 建一个，185 行实测刷新一次要 20 秒（切列表卡一两秒就是这么来的）。
    // 改成建行时只在行控件上挂一个「按需构建」的闭包，真正展开时才构造。
    void ensureDetailPanel(QWidget *row);
    // 建立「建议」面板（放在搜索栏与列表之间）。
    void setupSuggestionPanel(QVBoxLayout *parentLayout);
    void setExpandedTask(int id);
    // 确定性地设置「哪一行展开」：id < 0 表示全部收起。
    // 与 setExpandedTask（切换语义，供单击用）区分开，测试与外部驱动用它，
    // 免得依赖调用前的展开状态。
    void expandTask(int id);
    void applyTaskEdit(int id, const Task& edited);
    void setStatusMessage(const QString& message, bool isError = false);
    // 把当前文档写回 SQLite。所有改动了数据的路径都要调用它，否则界面上的
    // 改动只留在内存里，重启就丢。失败时给出提示并返回 false。
    bool persistDocument();

    TaskFilter buildQuery() const;
    QString priorityColor(int priority) const;
    // 状态标签的整条样式：四种状态各一个明显不同的颜色。
    // 定义在头文件里是为了让它能直接用下面的调色板（也就不必把它泄露到
    // 匿名命名空间里）。
    QString statusChipStyle(int status) const {
        static const char* kBackground[4] = {
            "#3D6BFF",   // 待办   —— 蓝
            "#00E5A0",   // 进行中 —— 亮青绿
            "#8A8A9A",   // 已完成 —— 灰
            "#FFC814",   // 已暂停 —— 琥珀
        };
        static const char* kForeground[4] = {
            "#FFFFFF", "#0A2A20", "#FFFFFF", "#3A2A00",
        };
        const int index = (status >= 0 && status <= 3) ? status : 0;
        return QString("QLabel { color: %1; background-color: %2; border-radius: 3px; "
                       "padding: 1px 5px; font-size: 9px; font-weight: bold; }")
            .arg(kForeground[index])
            .arg(kBackground[index]);
    }
    QString priorityName(int priority) const;
    // 状态对应的强调色，用于任务行左侧那条色带。
    QString statusAccentColor(int status) const {
        static const char* kAccent[4] = {
            "#3D6BFF",   // 待办   —— 蓝
            "#00C88A",   // 进行中 —— 青绿
            "#8A8A9A",   // 已完成 —— 灰
            "#E0A800",   // 已暂停 —— 琥珀
        };
        const int index = (status >= 0 && status <= 3) ? status : 0;
        return QString::fromLatin1(kAccent[index]);
    }
    QString categoryName(int categoryId) const;
    QString categoryColor(int categoryId) const;
};

#endif
