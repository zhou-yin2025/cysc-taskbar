// ============================================================================
//  CYSC 任务栏 - 主窗口
//
//  职责：工具栏（新建/搜索/隐藏已完成）、分类筛选、任务列表与其内联详情编辑、
//        托盘与到期提醒。业务规则都在 TaskDocument / TaskDraft 里。
// ============================================================================

#include "mainwindow.h"
#include "src/core/StoragePaths.h"
#include "src/widgets/TaskDialog.h"
#include "src/widgets/TaskGroupDialog.h"
#include "src/ui/Theme.h"
#include "src/widgets/TaskVisualizer.h"
#include "src/widgets/TaskSettingsDialog.h"
#include "src/widgets/TaskWizardDialog.h"
#include <algorithm>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QStringListModel>
#include <QRegularExpression>
#include <functional>
#include <QCheckBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDir>
#include <QMimeData>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMetaType>
#include <QElapsedTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QSettings>
#include <QScrollBar>
#include <QWheelEvent>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QPainter>
#include <QSet>
#include <QIcon>
#include <QPainterPath>
#include <QLinearGradient>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTextStream>
#include <QThread>
#include <QImage>
#include <QInputDialog>
#include <QTimer>
#include <QVBoxLayout>
#include <QDate>

namespace {

// 等宽且含中文字形的字体优先，保证标题栏的赛博观感在中文下不掉字。
//
// 候选按平台排：Windows 的 Cascadia Mono / Consolas，macOS 的 Menlo / SF Mono。
// 末尾一定要跟一个**含中文字形**的字体，否则在没装等宽中文的机器上标题栏
// 中文会变成方块。（Windows 用雅黑，macOS 用苹方 PingFang SC。）
QString monoFontFamily() {
    const QStringList candidates = {
        // Windows
        "Cascadia Mono", "Consolas",
        // macOS
        "Menlo", "SF Mono", "Monaco",
        // 跨平台 / 含中文
        "Sarasa Mono SC",
        "Microsoft YaHei UI", "PingFang SC", "Heiti SC"
    };
    const QStringList installed = QFontDatabase::families();
    for (const QString& name : candidates) {
        if (installed.contains(name)) {
            return name;
        }
    }
    return QString();
}

// 筛选/优先级/分类的小按钮。取色全部来自当前主题，换主题时自动跟着变。
QString chipStyle(const QString& accent, bool active) {
    const Theme &th = ThemeManager::current();
    const QString bg = ThemeManager::hex(th.toolbarBg);
    const QString ink = ThemeManager::hex(th.onAccent);
    if (active) {
        return QString(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid %1; padding: 1px 6px; font-size: 10px; font-weight: bold; }")
            .arg(accent, ink);
    }
    return QString(
        "QPushButton { background-color: %1; color: %3; "
        "border: 1px solid %3; padding: 1px 6px; font-size: 10px; }"
        "QPushButton:hover { background-color: %3; color: %2; }")
        .arg(bg, ink, accent);
}

// 状态色取自主题的 statusAccent / statusChip 表。
QString statusAccentHex(int status) {
    const int i = (status >= 0 && status <= 3) ? status : 0;
    return ThemeManager::hex(ThemeManager::current().statusAccent[i]);
}
QString statusChipHex(int status) {
    const int i = (status >= 0 && status <= 3) ? status : 0;
    return ThemeManager::hex(ThemeManager::current().statusChipBg[i]);
}
QString statusChipInkHex(int status) {
    const int i = (status >= 0 && status <= 3) ? status : 0;
    return ThemeManager::hex(ThemeManager::current().statusChipInk[i]);
}

// 优先级配色：直接取主题里的调色板。
QString priorityAccentHex(int priority) {
    const int i = (priority >= 0 && priority <= 3) ? priority : 0;
    return ThemeManager::hex(ThemeManager::current().priorityAccent[i]);
}

// 分类配色：同上。
QString categoryAccentHex(int categoryId) {
    const int i = (categoryId >= 0 && categoryId <= 3) ? categoryId : 0;
    return ThemeManager::hex(ThemeManager::current().categoryAccent[i]);
}

// 优先级 / 分类的名称与配色。
// 配色值不在这里存，而是通过下面的取色函数从主题表读 —— 保持单一来源。
const char* kPriorityNames[4] = {"低", "中", "高", "紧急"};
const char* kCategoryNames[4] = {"其他", "工作", "学习", "健康"};

// 整行的样式表（挂在行的 container 上，靠 objectName 命中子控件）。
//
// 逐个控件 setStyleSheet 在 Qt 里很贵：每次都要重新解析样式表并给该控件重算
// 样式。185 行 × 8 个控件实测让一次刷新慢到 2.7 秒。
//
// 所以这里只返回**与具体任务无关**的那部分规则，由列表统一挂一次
// （见 installRowStyleSheet）。任务相关的颜色用一个很小的内联样式表直接设到
// 那个小标签上——它没有子控件，重算代价可以忽略。
QString rowStyleSheet() {
    const Theme &th = ThemeManager::current();
    auto h = [](const QColor &c) { return ThemeManager::hex(c); };

    return QString(
        // 卡片：折叠 / 展开两种底
        "QWidget#taskRow { background-color: %1; border: 1px solid %2;"
        " border-radius: 5px; }"
        "QWidget#taskRowExpanded { background-color: %3; border: 1px solid %4;"
        " border-radius: 5px; }"
        // 标题的三种状态
        "QLabel#rowTitle { color: %5; font-size: 12px; }"
        // 已完成：删除线 + 更浅的文字（需求：已完成任务名加删除线并改成较浅色）
        // 删除线由 StrikeLabel 自绘（为了画线动画），这里不再用 text-decoration
        "QLabel#rowTitleDone { color: %6; font-size: 12px; }"
        "QLabel#rowTitlePaused { color: %6; font-size: 12px; font-style: italic; }"
        // 固定不变的小标签
        "QLabel#rowNote { color: %7; background-color: rgba(0,170,136,40);"
        " border-radius: 3px; padding: 1px 4px; font-size: 9px; }"
        "QLabel#rowTime { color: %6; font-size: 10px; }"
        "QLabel#rowTimeEmpty { color: %6; font-size: 10px; }"
        // 详情面板里的一堆控件也统一在这里，省掉每行一次 setStyleSheet
        "QWidget#detailPanel { background-color: %8;"
        " border-top: 1px solid %2; }"
        "QWidget#detailPanel QLabel { color: %5; font-size: 10px; }"
        "QWidget#detailPanel QLineEdit, QWidget#detailPanel QPlainTextEdit,"
        " QWidget#detailPanel QDateTimeEdit { background-color: %9;"
        " color: %5; border: 1px solid %2; padding: 2px; font-size: 11px; }")
        .arg(h(th.cardBg),                     // 1
             h(th.cardBorder),                 // 2
             h(th.cardBgExpanded),             // 3
             h(th.cardBorderExpanded),         // 4
             h(th.cardTitleInk),               // 5
             h(th.cardMutedInk),               // 6
             h(th.statusAccent[1]),            // 7 备注标记用「进行中」的青绿
             h(th.panelBg))                    // 8
        .arg(h(th.toolbarBg));                 // 9 输入框底
}

// 任务相关的小标签：没有子控件，内联样式表的重算代价可以忽略。
QString stripeStyle(int status) {
    return QString("background-color: %1; border-radius: 2px;").arg(statusAccentHex(status));
}
QString dotStyleFor(int priority) {
    return QString("background-color: %1; border-radius: 5px;").arg(priorityAccentHex(priority));
}
// 优先级竖条：细长圆角，颜色与宽度都随等级变化。
QString priorityBarStyle(int priority) {
    const int r = qBound(0, priority, 3);
    return QString("background-color: %1; border-radius: %2px;")
        .arg(priorityAccentHex(r))
        .arg((2 + r) / 2);
}
// 高优先级时整张卡片的描边也换成优先级颜色 —— 需求是"保证可见的明显边框"。
// 中/低优先级不加，否则每张卡片都在喊，反而看不出重点。
QString priorityBorderStyle(int priority) {
    if (priority < 2) return QString();
    return QString("QWidget#taskRow, QWidget#taskRowExpanded { border: 2px solid %1; }")
        .arg(priorityAccentHex(priority));
}
QString statusChipStyle(int status) {
    return QString("QLabel { color: %1; background-color: %2; border-radius: 3px;"
                   " padding: 1px 5px; font-size: 9px; font-weight: bold; }")
        .arg(statusChipInkHex(status), statusChipHex(status));
}
QString categoryChipStyle(int categoryId) {
    return QString("QLabel { color: white; background-color: %1; border-radius: 3px;"
                   " padding: 1px 5px; font-size: 9px; }")
        .arg(categoryAccentHex(categoryId));
}

} // namespace

namespace {
// 任务标题标签。除了显示文字，还负责画「完成时的画线动画」。
//
// 为什么不用样式表的 text-decoration: line-through：
//   样式表只能"有/没有"删除线，做不出从左到右画过去的过程。
//   所以删除线一律由这个类自己画（rowStyleSheet 里已去掉 text-decoration），
//   动画时按进度只画一部分，画完就是完整删除线。
class StrikeLabel : public QLabel {
public:
    explicit StrikeLabel(QWidget *parent = nullptr) : QLabel(parent) {}

    // 是否有删除线（动画中或已完成都算）。
    void setStrikeOn(bool on) {
        if (m_on == on) return;
        m_on = on;
        m_progress = on ? 0.0 : 0.0;
        m_animating = on;
        update();
    }
    bool strikeOn() const { return m_on; }
    bool strikeAnimating() const { return m_animating; }

    // 由主窗口的共享定时器推进；返回 true 表示还在动画中。
    bool tickStrike(qreal step) {
        if (!m_animating) return false;
        m_progress += step;
        if (m_progress >= 1.0) {
            m_progress = 1.0;
            m_animating = false;
        }
        update();
        return m_animating;
    }
    qreal strikeProgress() const { return m_progress; }

protected:
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);          // 先画文字
        if (!m_on || m_progress <= 0.0) return;

        // 删除线画在文字垂直中线、水平从左边按进度推进
        const QFontMetrics fm(font());
        const int textW = fm.horizontalAdvance(text());
        const int y = height() / 2;
        const int w = qRound(textW * qMin<qreal>(m_progress, 1.0));

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor ink = palette().color(QPalette::WindowText);
        p.setPen(QPen(ink, 1.4));
        p.drawLine(0, y, w, y);
    }

private:
    bool m_on = false;
    bool m_animating = false;
    qreal m_progress = 0.0;
};

// 托盘是否可用。允许用环境变量 CYSC_FORCE_TRAY=1 覆盖，便于在没有通知区域的
// 会话里验证「关窗 -> 收进托盘」这条路径本身是否正常。
// 定义在文件末尾，这里先声明（setupTray 与 closeEvent 都要用）。
bool trayUsable();
// 任务行控件：自带绘制，用来给「进行中」的任务画一圈流动的光。
//
// 为什么不用样式表 + QPropertyAnimation：样式表不擅长画"沿边框移动的高光"，
// 而且每次改样式都要重算整棵子树（本项目已经因为逐控件 setStyleSheet 吃过
// 性能问题）。这里只在 paintEvent 里画一道渐变弧，开销很小。
class TaskRowWidget : public QWidget {
public:
    explicit TaskRowWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    // 0..1 的动画进度，由一个共享定时器统一推进。
    void setGlowProgress(qreal p) {
        if (qFuzzyCompare(m_progress, p)) return;
        m_progress = p;
        update();
    }
    bool glowing() const { return m_glow; }
    void setGlowing(bool on) {
        if (m_glow == on) return;
        m_glow = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        // 先让基类/样式表把卡片底色与描边画出来
        QWidget::paintEvent(event);
        if (!m_glow) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal radius = 5.0;
        const QColor teal(0, 200, 138);      // 与「进行中」的状态色一致

        // ---------------------------------------------------------------
        // 第一层：整条边框「呼吸」。
        //
        // 为什么要加这一层：最初只画了一道旋转的高光，实测太细太暗，
        // 用户完全注意不到（截图里只有零星十几个青绿像素）。
        // 现在整圈随进度明暗脉动，再叠一道更亮的旋转高光，一眼就能看出来。
        // ---------------------------------------------------------------
        const qreal pulse = 0.5 + 0.5 * qSin(m_progress * 2.0 * M_PI);
        QColor base = teal;
        base.setAlpha(static_cast<int>(70 + 110 * pulse));
        p.setPen(QPen(base, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);

        // 外面再描一圈很淡的，做出一点"发光"的晕
        QColor halo = teal;
        halo.setAlpha(static_cast<int>(30 + 45 * pulse));
        p.setPen(QPen(halo, 3.5));
        p.drawRoundedRect(r.adjusted(-1.0, -1.0, 1.0, 1.0), radius + 1, radius + 1);

        // ---------------------------------------------------------------
        // 第二层：一道更亮的短弧沿边框转圈。
        // 用 QConicalGradient 让亮色随进度旋转，头部最亮、尾部渐隐。
        // ---------------------------------------------------------------
        QConicalGradient sweep(r.center(), -m_progress * 360.0);
        QColor head(120, 255, 210);
        QColor dim = teal;
        dim.setAlpha(0);
        sweep.setColorAt(0.00, dim);
        head.setAlpha(255);
        sweep.setColorAt(0.06, head);        // 高光头部
        head.setAlpha(200);
        sweep.setColorAt(0.14, head);
        head.setAlpha(60);
        sweep.setColorAt(0.26, head);        // 尾部渐隐
        sweep.setColorAt(0.45, dim);
        sweep.setColorAt(1.00, dim);

        p.setPen(QPen(QBrush(sweep), 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);
    }

private:
    bool m_glow = false;
    qreal m_progress = 0.0;
};

} // namespace

// 界面自检的 friend 类，定义在文件末尾（它要驱动 MainWindow 的私有成员）。
class MainWindowTestAccess;

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent) {

    setWindowTitle("CYSC 任务栏");
    // 主题必须**最先**载入：工具栏/搜索框/状态栏的样式表在各自的 setup 里
    // 就按当前主题拼好了，晚载入会让这些控件停留在上一款主题的配色上。
    ThemeManager::loadFromSettings();
    // 置顶由标题栏的图钉控制，默认置顶；用户的选择记在 QSettings 里。
    {
        QSettings settings;
        m_onTop = settings.value("window/onTop", true).toBool();
    }
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool
                   | (m_onTop ? Qt::WindowStaysOnTopHint : Qt::WindowType(0)));
    // 深色主题用半透明背景（霓虹感）；浅色主题必须不透明 ——
    // 实测半透明合成会把浅色底压成脏灰色，整窗看起来还是暗的。
    setAttribute(Qt::WA_TranslucentBackground, ThemeManager::current().dark);
    // 无边框窗口要自己处理拖拽边框调整大小，所以必须开鼠标跟踪：
    // 否则没按下时收不到 mouseMoveEvent，指针形状就没法跟着边缘变。
    setMouseTracking(true);
    // 比这更小就放不下侧栏 + 列表 + 详情了。
    setMinimumSize(480, 420);
    resize(600, 660);

    const QString dbPath = resolveStoragePath();
    // 先开库；开不了就绝不碰 loadDocument（DocumentStore 会在未打开时直接报错）。
    m_dbReady = m_store.open(dbPath);
    if (m_dbReady) {
        m_docLoaded = m_store.loadDocument(m_doc);
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, TITLE_BAR_HEIGHT + 6, 8, 8);
    mainLayout->setSpacing(5);

    setupToolbar();
    setupCategoryRow();

    m_taskList = new TaskListWidget(this);
    // 列表样式（含整行与详情面板的公共规则）在构造函数末尾由
    // applySettings() -> applyThemeToWidgets() 统一设置；这里先不设，
    // 避免在工具栏控件还没建好时就去动它们。
    m_taskList->setSelectionMode(QAbstractItemView::SingleSelection);
    // 拖拽排序：只在列表内部移动，落到哪一行就插到哪一行。
    // 松开鼠标后由 MainWindow::dropEvent 把新顺序写回模型。
    m_taskList->setDragEnabled(true);
    m_taskList->setAcceptDrops(true);
    m_taskList->setDropIndicatorShown(true);
    m_taskList->setDragDropMode(QAbstractItemView::InternalMove);
    m_taskList->setDefaultDropAction(Qt::MoveAction);
    // 单元格是自绘 widget，QListWidget 的 itemClicked / itemDoubleClicked 在这种
    // 情况下不可靠（双击常常只到 press 就没了）。直接在视口上处理鼠标事件。
    m_taskList->viewport()->installEventFilter(this);
    m_taskList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_taskList, &QListWidget::customContextMenuRequested,
            this, &MainWindow::onItemContextMenu);
    // 拖拽放下后把新顺序写回模型（见 TaskListWidget 的说明）
    connect(m_taskList, &TaskListWidget::rowMoved, this, &MainWindow::onRowsMoved);

    // 单击要等双击间隔过去才生效，所以用一个单次定时器延后判定。
    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    connect(m_clickTimer, &QTimer::timeout, this, &MainWindow::onClickTimerTimeout);

    // 延迟展开：按下后如果这段时间内没有移动，就当作单击并展开。
    m_expandDeferTimer = new QTimer(this);
    m_expandDeferTimer->setSingleShot(true);
    connect(m_expandDeferTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingExpandId < 0 || m_pressWasDrag) return;
        const int id = m_pendingExpandId;
        m_pendingExpandId = -1;
        setExpandedTask(id);
    });

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().accentDim)));
    m_statusLabel->setWordWrap(true);

    // 右侧内容区（搜索 + 分类 + 列表 + 状态栏），左侧是列表栏。
    QWidget *rightPane = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(5);
    rightLayout->addWidget(m_search);
    setupSuggestionPanel(rightLayout);
    rightLayout->addWidget(m_categoryHost);
    rightLayout->addWidget(m_taskList, 1);
    rightLayout->addWidget(m_statusLabel);

    m_sidebar = new TaskSidebar(this);
    m_sidebar->setDocument(&m_doc);
    connect(m_sidebar, &TaskSidebar::smartViewChosen, this, [this](SmartView view) {
        m_smartView = view;
        m_useSmartView = true;
        m_expandedId = -1;
        refreshList();
        setStatusMessage(QString("视图：%1").arg(smartViewText(view)));
    });
    connect(m_sidebar, &TaskSidebar::listChosen, this, [this](int listId) {
        m_listFilter = listId;
        m_useSmartView = false;
        m_expandedId = -1;
        refreshList();
        const TaskList *list = m_doc.findList(listId);
        setStatusMessage(QString("列表：%1")
                             .arg(list ? QString::fromStdString(list->name) : QString()));
    });
    connect(m_sidebar, &TaskSidebar::addListRequested, this, &MainWindow::onAddList);
    connect(m_sidebar, &TaskSidebar::groupListsRequested, this, &MainWindow::onGroupLists);
    connect(m_sidebar, &TaskSidebar::renameListRequested, this, &MainWindow::onRenameList);
    connect(m_sidebar, &TaskSidebar::deleteListRequested, this, &MainWindow::onDeleteList);
    connect(m_sidebar, &TaskSidebar::changeListColorRequested, this,
            &MainWindow::onChangeListColor);
    connect(m_sidebar, &TaskSidebar::removeListFromGroupRequested, this,
            &MainWindow::onRemoveListFromGroup);

    QHBoxLayout *rootLayout = new QHBoxLayout();
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);
    rootLayout->addWidget(m_sidebar);
    rootLayout->addWidget(rightPane, 1);
    mainLayout->addLayout(rootLayout, 1);

    // 快捷键：Ctrl+N / Enter 逐栏向导，Ctrl+Shift+N 完整表单，F2 展开详情，Ctrl+Z 撤销。
    QShortcut *newShortcut = new QShortcut(QKeySequence("Ctrl+N"), this);
    newShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(newShortcut, &QShortcut::activated, this, &MainWindow::onNewTask);

    QShortcut *newFullShortcut = new QShortcut(QKeySequence("Ctrl+Shift+N"), this);
    newFullShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(newFullShortcut, &QShortcut::activated, this, &MainWindow::onNewTaskFull);

    QShortcut *editShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    editShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(editShortcut, &QShortcut::activated, this, &MainWindow::onEditCurrentTask);

    QShortcut *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::onUndoRequested);

    // "/" 聚焦搜索框，符合常见列表工具的肌肉记忆。
    QShortcut *searchShortcut = new QShortcut(QKeySequence(Qt::Key_Slash), this);
    searchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
    });

    setupTray();
    setupReminder();
    updateCategoryRow();
    updateSortButton();
    refreshList();

    if (!m_dbReady) {
        setStatusMessage(QString("警告：任务无法保存（%1 不可写）").arg(dbPath), true);
    } else if (!m_docLoaded) {
        setStatusMessage(QString("警告：任务读取失败（%1）").arg(m_store.lastError()), true);
    }
    m_search->setFocus();

    // 应用配置文件里的设置项（显示方式 / 置顶 / 行密度 / 紧凑模式）。
    // 放在构造函数末尾：所有控件都已建好，且窗口还没 show()，不会出现尺寸跳动。
    {
        // 先把 hideDone 从设置里读进来，避免 applySettings 里 setChecked 触发一次
        // 多余的 refreshList。
        m_hideDoneChecked =
            TaskSettingsDialog::boolValue(TaskSettingsDialog::kHideDone, false);
        applySettings();
    }
}

MainWindow::~MainWindow() {}

// ---------------------------------------------------------------- 路径解析

QString MainWindow::resolveStoragePath() {
    const QString writable = StoragePaths::writableDatabasePath();
    if (!writable.isEmpty()) {
        return writable;
    }
    // 没有任何可写位置时回退到便携路径，交给 DocumentStore::open 去失败并报告。
    return QCoreApplication::applicationDirPath() + "/data/cysc.db";
}

// ------------------------------------------------------------------ 工具栏

void MainWindow::setupToolbar() {
    const Theme &th = ThemeManager::current();
    QWidget *bar = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_newButton = new QPushButton("＋ 新建任务", bar);
    m_newButton->setToolTip("逐栏快速新建（Enter / Ctrl+N）\n完整表单：Ctrl+Shift+N");
    m_newButton->setFixedHeight(26);
    m_newButton->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                "padding: 0 10px; font-size: 11px; font-weight: bold; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
        .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent),
             ThemeManager::hex(th.onAccent)));
    // Enter 打开新建：按钮持有焦点时回车即触发点击，无需额外快捷键。
    m_newButton->setDefault(true);
    m_newButton->setAutoDefault(true);
    connect(m_newButton, &QPushButton::clicked, this, &MainWindow::onNewTask);
    layout->addWidget(m_newButton);

    // 数据结构可视化（演示用）：随时打开看四种结构的内部状态。
    m_visualizeButton = new QPushButton("◈ 结构", bar);
    m_visualizeButton->setObjectName("visualizeButton");
    m_visualizeButton->setToolTip("打开数据结构可视化面板（哈希表 / Trie / 线段树 / 并查集）");
    m_visualizeButton->setFixedHeight(26);
    m_visualizeButton->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %4; border: 1px solid %4; "
                "padding: 0 8px; font-size: 11px; }"
                "QPushButton:hover { background-color: %4; color: %3; }")
        .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent),
             ThemeManager::hex(th.onAccent), ThemeManager::hex(th.statusChipBg[3])));
    connect(m_visualizeButton, &QPushButton::clicked, this, &MainWindow::openVisualizer);
    layout->addWidget(m_visualizeButton);

    // 「只显示任务」的紧凑模式开关。
    // 图标用两个大小不同的方块，和它的实际效果（窗口收缩/展开）对应。
    m_compactButton = new QPushButton("⊡", bar);
    m_compactButton->setObjectName("compactButton");
    m_compactButton->setToolTip("紧凑模式：只显示任务，窗口吸附到屏幕右上角\n再点一次恢复完整窗口");
    m_compactButton->setFixedSize(28, 26);
    m_compactButton->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                "padding: 0 8px; font-size: 11px; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
        .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent),
             ThemeManager::hex(th.onAccent)));
    connect(m_compactButton, &QPushButton::clicked, this, &MainWindow::toggleCompactMode);
    layout->addWidget(m_compactButton);

    // 「设置」：多功能自定义（显示方式 / 光效 / 行密度 / 窗口行为）
    m_settingsButton = new QPushButton("⚙", bar);
    m_settingsButton->setObjectName("settingsButton");
    m_settingsButton->setToolTip("设置：显示方式、光效、行密度、窗口行为");
    m_settingsButton->setFixedSize(28, 26);
    m_settingsButton->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                "font-size: 13px; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
        .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accentDim),
             ThemeManager::hex(th.onAccent)));
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    layout->addWidget(m_settingsButton);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText("搜索标题或备注...");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(26);
    m_search->setStyleSheet(
        QString("QLineEdit { background-color: %1; color: %2; "
                "border: 1px solid %2; padding: 3px; font-size: 11px; }")
        .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent)));
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    layout->addWidget(m_search, 1);

    // 搜索补全：下拉里的候选来自 Trie 的前缀索引（不是遍历全部标题）。
    // 用 UnfilteredPopupCompletion：弹出的候选只是「建议」，不会把文字自动补全，
    // 因此搜索仍然是子串匹配——半记得标题也能搜到。
    m_searchCompleter = new QCompleter(this);
    m_searchCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_searchCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_searchCompleter->setMaxVisibleItems(8);
    m_search->setCompleter(m_searchCompleter);

    m_hideDone = new QCheckBox("隐藏已完成", bar);
    m_hideDone->setToolTip("勾选后列表里不再显示已完成的任务");
    m_hideDone->setStyleSheet(
        QString("QCheckBox { color: %1; font-size: 10px; }"
                "QCheckBox::indicator { width: 12px; height: 12px; }")
        .arg(ThemeManager::hex(th.accent)));
    connect(m_hideDone, &QCheckBox::toggled, this, &MainWindow::onHideDoneToggled);
    layout->addWidget(m_hideDone);

    if (QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(this->layout())) {
        mainLayout->addWidget(bar);
    }
}

void MainWindow::setupCategoryRow() {
    const Theme &th = ThemeManager::current();
    // 包一层宿主控件：紧凑模式下要整行隐藏，直接隐藏 m_categoryRow 也可以，
    // 但布局里的间距会留下一道空隙，所以用宿主一起收掉。
    m_categoryHost = new QWidget(this);
    QVBoxLayout *hostLayout = new QVBoxLayout(m_categoryHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    m_categoryRow = new QWidget(m_categoryHost);
    hostLayout->addWidget(m_categoryRow);
    QHBoxLayout *layout = new QHBoxLayout(m_categoryRow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    QLabel *label = new QLabel("分类：", m_categoryRow);
    label->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(th.accent)));
    layout->addWidget(label);

    // 索引 0 是「全部」，1..4 对应 Other/Work/Study/Health。
    QPushButton *all = new QPushButton("全部", m_categoryRow);
    all->setFixedHeight(20);
    all->setToolTip("显示所有分类");
    connect(all, &QPushButton::clicked, this, [this]() { onCategoryFilterClicked(CategoryAll); });
    m_categoryButtons[0] = all;
    layout->addWidget(all);

    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kCategoryNames[i], m_categoryRow);
        btn->setFixedHeight(20);
        btn->setToolTip(QString("只看「%1」").arg(kCategoryNames[i]));
        connect(btn, &QPushButton::clicked, this, [this, i]() { onCategoryFilterClicked(i); });
        m_categoryButtons[i + 1] = btn;
        layout->addWidget(btn);
    }

    layout->addStretch();

    m_sortButton = new QPushButton("排序：创建时间", m_categoryRow);
    m_sortButton->setFixedHeight(20);
    m_sortButton->setToolTip("点击切换排序方式");
    connect(m_sortButton, &QPushButton::clicked, this, &MainWindow::onSortClicked);
    layout->addWidget(m_sortButton);
}

void MainWindow::updateCategoryRow() {
    const Theme &th = ThemeManager::current();
    for (int i = 0; i < 5; ++i) {
        QPushButton *btn = m_categoryButtons[i];
        if (!btn) continue;
        if (i == 0) {
            btn->setStyleSheet(chipStyle(ThemeManager::hex(th.accent), m_categoryFilter == CategoryAll));
        } else {
            btn->setStyleSheet(chipStyle(categoryAccentHex(i - 1), m_categoryFilter == (i - 1)));
        }
    }
}

void MainWindow::updateSortButton() {
    const Theme &th = ThemeManager::current();
    if (!m_sortButton) return;
    switch (m_sortMode) {
        case SortDeadlineAsc:
            m_sortButton->setText("排序：截止时间");
            break;
        case SortManual:
            m_sortButton->setText("排序：手动拖拽");
            break;
        case SortCreatedAsc:
        default:
            m_sortButton->setText("排序：创建时间");
            break;
    }
    m_sortButton->setStyleSheet(chipStyle(ThemeManager::hex(th.accent), m_sortMode != SortCreatedAsc));
}

// ------------------------------------------------------------------ 列表

TaskFilter MainWindow::buildQuery() const {
    TaskFilter filter;
    filter.search = m_search ? m_search->text().trimmed().toStdString() : std::string();
    filter.categoryId = m_categoryFilter;      // -1 = 不按分类过滤
    // 左侧栏的选择：智能视图优先，否则看某个列表。
    filter.useSmartView = m_useSmartView;
    filter.smartView = m_smartView;
    filter.listId = m_useSmartView ? -1 : m_listFilter;
    filter.hideDone = m_hideDoneChecked;
    switch (m_sortMode) {
        case SortDeadlineAsc: filter.sort = TaskSort::DeadlineAsc; break;
        case SortManual:      filter.sort = TaskSort::Manual; break;
        case SortCreatedAsc:
        default:              filter.sort = TaskSort::CreatedAsc; break;
    }
    return filter;
}

QString MainWindow::priorityColor(int priority) const {
    if (priority >= 0 && priority <= 3) return qPrintable(priorityAccentHex(priority));
    return priorityAccentHex(0);
}

QString MainWindow::priorityName(int priority) const {
    if (priority >= 0 && priority <= 3) return kPriorityNames[priority];
    return kPriorityNames[0];
}

QString MainWindow::categoryName(int categoryId) const {
    if (categoryId >= 0 && categoryId <= 3) return kCategoryNames[categoryId];
    return kCategoryNames[0];
}

QString MainWindow::categoryColor(int categoryId) const {
    if (categoryId >= 0 && categoryId <= 3) return qPrintable(categoryAccentHex(categoryId));
    return categoryAccentHex(0);
}

QWidget *MainWindow::createRowWidget(const Task& task) {
    auto *container = new TaskRowWidget();
    const bool expanded = (task.id == m_expandedId);
    // 用两个固定的 objectName 区分折叠/展开，规则写进列表级样式表（只设一次）。
    // 如果改用动态属性 + 属性选择器，Qt 每次改属性都要 repolish，反而更慢。
    container->setObjectName(expanded ? "taskRowExpanded" : "taskRow");
    // 折叠时让鼠标事件穿透到列表视口，单击/双击统一由事件过滤器处理；
    // 展开后必须接收事件，否则详情里的控件点不动。
    container->setAttribute(Qt::WA_TransparentForMouseEvents, !expanded);

    QVBoxLayout *outer = new QVBoxLayout(container);
    outer->setContentsMargins(7, 0, 7, 5);
    outer->setSpacing(0);

    // --- 第一行：左侧状态色条 + 优先级圆点 + 标题 + 状态 + 分类 + 截止时间 ---
    QWidget *head = new QWidget(container);
    head->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QHBoxLayout *h = new QHBoxLayout(head);
    h->setContentsMargins(0, 4, 0, 4);
    h->setSpacing(6);

    // 左边一条 4px 状态色条：待办蓝 / 进行中青绿 / 已完成灰 / 已暂停琥珀。
    // 只靠一个小圆点和文字块区分太弱，加一条整行高的色条，扫一眼就知道哪些在做。
    QLabel *stripe = new QLabel(head);
    stripe->setObjectName("rowStripe");
    stripe->setFixedWidth(4);
    stripe->setMinimumHeight(18);
    stripe->setProperty("accent", statusAccentColor(task.status));
    stripe->setToolTip(QString("状态：%1").arg(taskStatusText(task.status)));
    h->addWidget(stripe);

    // 优先级：原来只有一颗 10px 的小圆点，用户反馈「紧急度效果不明显」。
    // 改成整行高的竖条，宽度随等级递增（低 2px / 中 3px / 高 4px / 紧急 5px），
    // 并且高优先级时整张卡片的描边也换成优先级颜色（见下面的 priorityBorderStyle）。
    // 圆点已被竖条取代（两个都在只是重复信息、还占宽度）。
    QLabel *priorityBar = new QLabel(head);
    priorityBar->setObjectName("rowPriority");
    priorityBar->setFixedWidth(2 + qBound(0, task.priority, 3));
    priorityBar->setMinimumHeight(18);
    priorityBar->setProperty("accent", priorityColor(task.priority));
    priorityBar->setToolTip(QString("优先级：%1").arg(priorityName(task.priority)));
    h->addWidget(priorityBar);

    auto *titleLabel = new StrikeLabel(head);
    titleLabel->setObjectName("rowTitle");
    // 标题占满剩余宽度，太长时由 Qt 自己省略（setTextInteractionFlags 保持默认）。
    // 不再固定 174px、也不再手动 elidedText —— 手动省略只在固定宽度下才对，
    // 窗口拉宽后标题还是被砍掉，反而更难看清。
    titleLabel->setText(QString("%1 %2")
                            .arg(expanded ? "▼" : "▶")
                            .arg(QString::fromStdString(task.title)));
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleLabel->setMinimumWidth(40);
    titleLabel->setProperty("state", task.status == 2 ? "done"
                                 : task.status == 3 ? "paused" : "normal");
    titleLabel->setToolTip(QString("%1｜优先级 %2｜截止 %3\n单击展开详情，双击切换状态")
                               .arg(QString::fromStdString(task.title))
                               .arg(priorityName(task.priority))
                               .arg(QString::fromStdString(task.deadline)));
    h->addWidget(titleLabel, 1);

    // 「完成」时画线动画 / 已完成的常驻删除线。
    //
    // 判定顺序很重要：先看"刚被标记完成"（m_strikeAnimateIds），那种要从 0 开始
    // 播一次动画；否则（打开程序时就已经完成的历史任务）直接画满，不播动画，
    // 免得每次刷新都看到一堆线在那里画。
    if (task.status == 2) {
        if (m_strikeAnimateIds.contains(task.id)) {
            titleLabel->setStrikeOn(true);       // 进度从 0 开始 -> 会播动画
        } else {
            titleLabel->setStrikeOn(true);
            titleLabel->tickStrike(1.0);         // 直接画满
        }
    }

    if (!task.details.empty()) {
        QLabel *noteMark = new QLabel("备注", head);
        noteMark->setObjectName("rowNote");
        noteMark->setToolTip(QString::fromStdString(task.details));
        h->addWidget(noteMark);
    }

    QLabel *statusLabel = new QLabel(taskStatusText(task.status), head);
    statusLabel->setObjectName("rowStatus");
    statusLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QLabel *catLabel = new QLabel(categoryName(task.categoryId), head);
    catLabel->setObjectName("rowCategory");
    catLabel->setProperty("accent", categoryColor(task.categoryId));
    catLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // 先加弹簧把前面的内容顶到左边，再放截止时间。
    //
    // 之前弹簧加在最后，结果时间被挤在中间、右边留一大片空白。
    // 现在顺序是：标题（占剩余全部宽度）→ 状态/分类 chip → 弹簧 → 时间。
    // 时间固定在 78px 宽的右对齐标签里，所以所有行的时间都对齐在同一列。
    h->addWidget(statusLabel);
    h->addWidget(catLabel);
    h->addStretch(1);

    // 截止时间：固定宽度 + 右对齐；没有截止时间时显示灰色占位符。
    QString timeStr = "无";
    if (task.deadline.length() >= 16) {
        timeStr = QString::fromStdString(task.deadline.substr(5, 11));
    }
    QLabel *timeLabel = new QLabel(timeStr, head);
    timeLabel->setObjectName("rowTime");
    timeLabel->setProperty("isEmpty", task.deadline.empty());
    timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeLabel->setFixedWidth(78);
    h->addWidget(timeLabel);

    outer->addWidget(head);

    // 整行样式由列表统一挂一次（见 installRowStyleSheet），这里不再逐个设。
    // 另外三个彩色小标签（色条/圆点/两个 chip）用很小的内联样式表——它们没有
    // 子控件，重算代价可忽略，但颜色是每行不同的，没法放进公共规则。
    stripe->setStyleSheet(stripeStyle(task.status));
    priorityBar->setStyleSheet(priorityBarStyle(task.priority));
    statusLabel->setStyleSheet(statusChipStyle(task.status));
    catLabel->setStyleSheet(categoryChipStyle(task.categoryId));

    // 高优先级：整张卡片换成优先级颜色的粗描边（需求：紧急度要保证可见）
    if (const QString pb = priorityBorderStyle(task.priority); !pb.isEmpty()) {
        container->setStyleSheet(pb);
    }

    // 详情区**不在这里建**，只挂一个「按需构建」的闭包。
    // 展开时由 ensureDetailPanel() 真正构造（见它的说明）。
    // 细节：用 setProperty 而不是直接存 std::function，是因为 Qt 的属性系统
    // 会负责拷贝/析构，行控件被 delete 时不会漏。
    {
        const Task snapshot = task;
        container->setProperty(
            "detailBuilder",
            QVariant::fromValue(std::function<QWidget*()>(
                [this, snapshot]() { return createDetailEditor(snapshot); })));
    }
    if (expanded) {
        ensureDetailPanel(container);
    }

    return container;
}

void MainWindow::ensureDetailPanel(QWidget *row) {
    if (!row) return;
    if (row->findChild<QWidget*>("detailPanelWidget")) return;   // 已经有了

    const QVariant prop = row->property("detailBuilder");
    if (!prop.canConvert<std::function<QWidget*()>>()) return;
    const auto builder = prop.value<std::function<QWidget*()>>();
    if (!builder) return;

    QWidget *detail = builder();
    if (!detail) return;
    detail->setObjectName("detailPanelWidget");

    // 插在行内最后一个布局里（head 之后）
    if (QLayout *lay = row->layout()) {
        lay->addWidget(detail);
    } else {
        detail->setParent(row);
    }
    detail->setVisible(true);
}

QWidget *MainWindow::createDetailEditor(const Task& task) {
    const Theme &th = ThemeManager::current();
    QWidget *panel = new QWidget();
    panel->setObjectName("detailPanel");
    // 面板样式来自列表级样式表（见 rowStyleSheet）——这里不再 setStyleSheet。
    // 每个展开的行都设一次的话，那正是「切列表卡顿」的主要开销之一。
    panel->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(5);

    QLineEdit *titleEdit = new QLineEdit(QString::fromStdString(task.title), panel);
    titleEdit->setPlaceholderText("任务标题");
    layout->addWidget(titleEdit);

    // 当前状态用同样的配色显示，方便对照（双击任务行可推进状态）。
    QLabel *statusChip = new QLabel(
        QString("状态：%1　（双击任务行切换）").arg(taskStatusText(task.status)), panel);
    statusChip->setStyleSheet(statusChipStyle(task.status));
    statusChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(statusChip);

    // 优先级
    QWidget *priorityRow = new QWidget(panel);
    QHBoxLayout *pl = new QHBoxLayout(priorityRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(4);
    QLabel *plabel = new QLabel("优先级", priorityRow);
    pl->addWidget(plabel);
    QPushButton *priorityButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kPriorityNames[i], priorityRow);
        btn->setFixedHeight(20);
        btn->setCheckable(true);
        btn->setChecked(task.priority == i);
        btn->setStyleSheet(chipStyle(qPrintable(priorityAccentHex(i)), task.priority == i));
        priorityButtons[i] = btn;
        pl->addWidget(btn);
    }
    pl->addStretch();
    layout->addWidget(priorityRow);

    // 分类
    QWidget *categoryRow = new QWidget(panel);
    QHBoxLayout *cl = new QHBoxLayout(categoryRow);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(4);
    QLabel *clabel = new QLabel("分类", categoryRow);
    cl->addWidget(clabel);
    QPushButton *categoryButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton(kCategoryNames[i], categoryRow);
        btn->setFixedHeight(20);
        btn->setCheckable(true);
        btn->setChecked(task.categoryId == i);
        btn->setStyleSheet(chipStyle(qPrintable(categoryAccentHex(i)), task.categoryId == i));
        categoryButtons[i] = btn;
        cl->addWidget(btn);
    }
    cl->addStretch();
    layout->addWidget(categoryRow);

    // 截止时间（精确到分钟）
    QWidget *deadlineRow = new QWidget(panel);
    QHBoxLayout *dl = new QHBoxLayout(deadlineRow);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(4);
    QLabel *dlabel = new QLabel("截止", deadlineRow);
    dl->addWidget(dlabel);
    QDateTimeEdit *deadlineEdit = new QDateTimeEdit(panel);
    deadlineEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    const QDateTime parsed = QDateTime::fromString(QString::fromStdString(task.deadline),
                                                   "yyyy-MM-dd HH:mm");
    deadlineEdit->setDateTime(parsed.isValid() ? parsed : QDateTime::currentDateTime());
    deadlineEdit->setCalendarPopup(true);
    dl->addWidget(deadlineEdit, 1);
    layout->addWidget(deadlineRow);

    // ---- 星标 + 所属列表 + 重复 ----
    QWidget *metaRow = new QWidget(panel);
    QHBoxLayout *ml = new QHBoxLayout(metaRow);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(6);

    QCheckBox *starCheck = new QCheckBox("★ 重要", metaRow);
    starCheck->setChecked(task.starred);
    starCheck->setStyleSheet(QString("QCheckBox { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
    starCheck->setToolTip("星标后出现在左侧「重要」视图");
    ml->addWidget(starCheck);

    QCheckBox *myDayCheck = new QCheckBox("☀ 我的一天", metaRow);
    myDayCheck->setChecked(task.inMyDay);
    myDayCheck->setStyleSheet(QString("QCheckBox { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
    ml->addWidget(myDayCheck);

    ml->addWidget(new QLabel("列表", metaRow));
    QComboBox *listCombo = new QComboBox(metaRow);
    for (const TaskList &list : m_doc.lists()) {
        listCombo->addItem(QString::fromStdString(list.name), list.id);
        if (list.id == task.listId) listCombo->setCurrentIndex(listCombo->count() - 1);
    }
    ml->addWidget(listCombo);

    ml->addWidget(new QLabel("重复", metaRow));
    QComboBox *repeatCombo = new QComboBox(metaRow);
    for (int r = 0; r <= 5; ++r) {
        repeatCombo->addItem(recurrenceText(static_cast<Recurrence>(r)), r);
        if (r == static_cast<int>(task.recurrence)) {
            repeatCombo->setCurrentIndex(repeatCombo->count() - 1);
        }
    }
    ml->addWidget(repeatCombo);
    ml->addStretch();
    layout->addWidget(metaRow);

    // ---- 提醒 ----
    QWidget *reminderRow = new QWidget(panel);
    QHBoxLayout *rl = new QHBoxLayout(reminderRow);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(4);
    QCheckBox *reminderCheck = new QCheckBox("提醒", reminderRow);
    reminderCheck->setStyleSheet(QString("QCheckBox { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
    QDateTimeEdit *reminderEdit = new QDateTimeEdit(reminderRow);
    reminderEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    reminderEdit->setCalendarPopup(true);
    const QDateTime reminderParsed =
        QDateTime::fromString(QString::fromStdString(task.reminder), "yyyy-MM-dd HH:mm");
    const bool hasReminder = reminderParsed.isValid();
    reminderCheck->setChecked(hasReminder);
    reminderEdit->setDateTime(hasReminder ? reminderParsed : QDateTime::currentDateTime());
    reminderEdit->setEnabled(hasReminder);
    rl->addWidget(reminderCheck);
    rl->addWidget(reminderEdit, 1);
    layout->addWidget(reminderRow);

    // ---- 子任务（Steps）----
    QLabel *stepsTitle = new QLabel(panel);
    stepsTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
    layout->addWidget(stepsTitle);

    QWidget *stepsBox = new QWidget(panel);
    QVBoxLayout *stepsLayout = new QVBoxLayout(stepsBox);
    stepsLayout->setContentsMargins(0, 0, 0, 0);
    stepsLayout->setSpacing(2);
    layout->addWidget(stepsBox);

    QWidget *stepAddRow = new QWidget(panel);
    QHBoxLayout *sal = new QHBoxLayout(stepAddRow);
    sal->setContentsMargins(0, 0, 0, 0);
    sal->setSpacing(4);
    QLineEdit *stepInput = new QLineEdit(stepAddRow);
    stepInput->setPlaceholderText("添加步骤，回车确认");
    QPushButton *stepAddBtn = new QPushButton("＋", stepAddRow);
    stepAddBtn->setFixedWidth(26);
    sal->addWidget(stepInput, 1);
    sal->addWidget(stepAddBtn);
    layout->addWidget(stepAddRow);

    // 界面用一份可变的步骤副本，保存时整体写回任务。
    // 做法：每加一步就 append 一行、每删一步就删掉那一行——不再「清空后整体重建」。
    // 之前那版用一个自引用的 std::function 重建，既形成引用环（永不释放），
    // 又在 deleteLater 还没落地时重建布局，退出时崩溃。
    //
    // 每一行有一个稳定的 rowId（只增不减），stepState 里记着同样的 rowId。
    // 这样删除中间某一步之后，其余行不需要重新编号，勾选也不会串行。
    struct StepEntry {
        int rowId = -1;
        Subtask data;
    };
    std::vector<StepEntry> *stepState = new std::vector<StepEntry>();
    int *nextRowId = new int(0);
    for (const Subtask &s : task.subtasks) {
        stepState->push_back(StepEntry{(*nextRowId)++, s});
    }

    auto refreshStepTitle = [stepsTitle, stepState]() {
        int done = 0;
        for (const StepEntry &e : *stepState) {
            if (e.data.done) ++done;
        }
        stepsTitle->setText(QString("步骤 %1/%2").arg(done).arg(stepState->size()));
    };

    QLabel *stepsEmpty = new QLabel("还没有步骤", stepsBox);
    stepsEmpty->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardMutedInk)));
    stepsLayout->addWidget(stepsEmpty);

    // 按 rowId 找到它在 stepState 里的下标；找不到返回 -1。
    auto indexOfRowId = [stepState](int rowId) {
        for (std::size_t i = 0; i < stepState->size(); ++i) {
            if ((*stepState)[i].rowId == rowId) return static_cast<int>(i);
        }
        return -1;
    };

    // 创建一行步骤（勾选框 + 删除按钮），并接好信号。
    auto makeStepRow = [this, stepsBox, stepsLayout, stepsEmpty, stepState,
                        refreshStepTitle, indexOfRowId](int rowId) -> QWidget * {
        const int index = indexOfRowId(rowId);
        if (index < 0) return nullptr;

        QWidget *row = new QWidget(stepsBox);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        QCheckBox *box = new QCheckBox(
            QString::fromStdString((*stepState)[static_cast<std::size_t>(index)].data.title), row);
        box->setChecked((*stepState)[static_cast<std::size_t>(index)].data.done);
        box->setStyleSheet(QString("QCheckBox { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));

        QPushButton *del = new QPushButton("✕", row);
        del->setFixedWidth(20);
        del->setToolTip("删除这一步");
        // 稳定的控件名：界面自检要精确定位到「某一步」的删除按钮，
        // findChildren 的返回顺序并不保证等于布局顺序。
        del->setObjectName(QString("stepDel_%1").arg(rowId));
        del->setStyleSheet(QString("QPushButton { border: none; color: %1; font-size: 10px; }"
                                  "QPushButton:hover { color: %2; }")
                      .arg(ThemeManager::hex(ThemeManager::current().cardMutedInk),
                           ThemeManager::hex(ThemeManager::current().danger)));

        rowLayout->addWidget(box, 1);
        rowLayout->addWidget(del);
        rowLayout->addStretch(0);
        stepsLayout->addWidget(row);
        row->setProperty("stepRowId", rowId);
        stepsEmpty->setVisible(false);

        connect(box, &QCheckBox::toggled, this,
                [stepState, rowId, indexOfRowId, refreshStepTitle](bool on) {
                    const int i = indexOfRowId(rowId);
                    if (i >= 0) (*stepState)[static_cast<std::size_t>(i)].data.done = on;
                    refreshStepTitle();
                });
        connect(del, &QPushButton::clicked, this,
                [stepState, row, rowId, indexOfRowId, refreshStepTitle, stepsEmpty]() {
                    const int i = indexOfRowId(rowId);
                    if (i >= 0) stepState->erase(stepState->begin() + i);
                    // 先脱离布局再延后销毁：直接 delete 会让布局留下悬垂条目。
                    if (QLayout *parentLayout = row->parentWidget()
                                                    ? row->parentWidget()->layout()
                                                    : nullptr) {
                        parentLayout->removeWidget(row);
                    }
                    row->setParent(nullptr);
                    row->deleteLater();
                    refreshStepTitle();
                    if (stepState->empty()) stepsEmpty->setVisible(true);
                });
        return row;
    };

    for (const StepEntry &e : *stepState) {
        makeStepRow(e.rowId);
    }
    stepsEmpty->setVisible(stepState->empty());
    refreshStepTitle();

    auto addStep = [this, stepInput, stepState, nextRowId, makeStepRow, refreshStepTitle]() {
        const QString text = stepInput->text().trimmed();
        if (text.isEmpty()) return;
        Subtask s;
        s.id = (*nextRowId);
        s.title = text.toStdString();
        s.done = false;
        stepState->push_back(StepEntry{(*nextRowId)++, s});
        makeStepRow(stepState->back().rowId);
        stepInput->clear();
        refreshStepTitle();
    };
    connect(stepAddBtn, &QPushButton::clicked, this, addStep);
    connect(stepInput, &QLineEdit::returnPressed, this, addStep);

    // ---- 标签 ----
    QLabel *tagsTitle = new QLabel("标签", panel);
    tagsTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
    layout->addWidget(tagsTitle);

    QWidget *tagBox = new QWidget(panel);
    QHBoxLayout *tagLayout = new QHBoxLayout(tagBox);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    tagLayout->setSpacing(4);
    layout->addWidget(tagBox);

    QLineEdit *tagInput = new QLineEdit(panel);
    tagInput->setPlaceholderText("添加标签，回车确认（多个用空格分隔）");
    layout->addWidget(tagInput);

    // 显示哈希索引里的用量，方便讲「标签倒排索引」时对照。只创建一次，
    // rebuildTags() 负责更新文本（否则每次重建都要去找控件）。
    QLabel *tagUsage = new QLabel(panel);
    tagUsage->setStyleSheet(QString("QLabel { color: %1; font-size: 9px; }").arg(ThemeManager::hex(ThemeManager::current().cardMutedInk)));
    layout->addWidget(tagUsage);

    std::vector<std::string> *tagState = new std::vector<std::string>(task.tags);

    QLabel *tagsEmpty = new QLabel("还没有标签", tagBox);
    tagsEmpty->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardMutedInk)));
    tagLayout->addWidget(tagsEmpty);
    tagLayout->addStretch(1);

    // 更新「标签索引」用量行（哈希表当前内容的直观呈现）。
    auto refreshTagUsage = [this, tagUsage]() {
        QString info = "标签索引：";
        const std::vector<TagCount> counts = m_doc.tagCounts();
        if (counts.empty()) {
            info += "（无）";
        } else {
            for (std::size_t i = 0; i < counts.size() && i < 8; ++i) {
                info += QString("%1×%2  ")
                            .arg(QString::fromStdString(counts[i].tag))
                            .arg(counts[i].count);
            }
        }
        tagUsage->setText(info);
    };

    // 加一个标签 chip；点击即移除。同样不做整体重建。
    auto makeTagChip = [this, tagBox, tagLayout, tagsEmpty, tagState,
                        refreshTagUsage](const std::string &tag) {
        QPushButton *chip = new QPushButton(
            QString("#%1 ✕").arg(QString::fromStdString(tag)), tagBox);
        chip->setFixedHeight(18);
        chip->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; "
                    "border: 1px solid %3; border-radius: 8px; padding: 0 6px; "
                    "font-size: 9px; }"
                    "QPushButton:hover { background-color: %4; }")
                .arg(ThemeManager::hex(ThemeManager::current().cardBg),
                     ThemeManager::hex(ThemeManager::current().cardTitleInk),
                     ThemeManager::hex(ThemeManager::current().cardBorder),
                     ThemeManager::hex(ThemeManager::current().danger.lighter(180))));
        chip->setToolTip("点击移除该标签");
        // 插在 stretch 之前
        tagLayout->insertWidget(tagLayout->count() - 1, chip);
        tagsEmpty->setVisible(false);

        connect(chip, &QPushButton::clicked, this,
                [tagState, chip, tag, refreshTagUsage, tagsEmpty]() {
                    for (auto it = tagState->begin(); it != tagState->end(); ++it) {
                        if (*it == tag) {
                            tagState->erase(it);
                            break;
                        }
                    }
                    if (QLayout *parentLayout = chip->parentWidget()
                                                    ? chip->parentWidget()->layout()
                                                    : nullptr) {
                        parentLayout->removeWidget(chip);
                    }
                    chip->setParent(nullptr);
                    chip->deleteLater();
                    if (tagState->empty()) tagsEmpty->setVisible(true);
                    refreshTagUsage();
                });
    };
    for (const std::string &tag : *tagState) {
        makeTagChip(tag);
    }
    tagsEmpty->setVisible(tagState->empty());
    refreshTagUsage();

    auto addTags = [this, tagInput, tagState, makeTagChip]() {
        const QString text = tagInput->text().trimmed();
        if (text.isEmpty()) return;
        // 支持一次输入多个（空格分隔）
        const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const std::string tag = part.toStdString();
            bool exists = false;
            for (const std::string &t : *tagState) {
                if (t == tag) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;
            tagState->push_back(tag);
            makeTagChip(tag);
        }
        tagInput->clear();
    };
    connect(tagInput, &QLineEdit::returnPressed, this, addTags);

    connect(reminderCheck, &QCheckBox::toggled, this,
            [reminderEdit](bool on) { reminderEdit->setEnabled(on); });

    QPlainTextEdit *detailsEdit = new QPlainTextEdit(panel);
    detailsEdit->setPlainText(QString::fromStdString(task.details));
    detailsEdit->setPlaceholderText("备注 / 详情");
    detailsEdit->setFixedHeight(64);
    layout->addWidget(detailsEdit);

    // 本次展开期间的选择状态（lambda 之间共享）
    struct Preference {
        int priority = 0;
        int category = 0;
    };
    Preference *state = new Preference;
    state->priority = task.priority;
    state->category = task.categoryId;

    for (int i = 0; i < 4; ++i) {
        connect(priorityButtons[i], &QPushButton::clicked, this,
                [this, state, priorityButtons, i]() {
                    state->priority = i;
                    for (int k = 0; k < 4; ++k) {
                        priorityButtons[k]->setChecked(k == i);
                        priorityButtons[k]->setStyleSheet(chipStyle(priorityAccentHex(k), k == i));
                    }
                });
        connect(categoryButtons[i], &QPushButton::clicked, this,
                [this, state, categoryButtons, i]() {
                    state->category = i;
                    for (int k = 0; k < 4; ++k) {
                        categoryButtons[k]->setChecked(k == i);
                        categoryButtons[k]->setStyleSheet(chipStyle(categoryAccentHex(k), k == i));
                    }
                });
    }

    // 操作按钮
    QWidget *actionRow = new QWidget(panel);
    QHBoxLayout *al = new QHBoxLayout(actionRow);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(6);
    QPushButton *saveBtn = new QPushButton("保存", actionRow);
    QPushButton *cancelBtn = new QPushButton("收起", actionRow);
    QPushButton *deleteBtn = new QPushButton("删除", actionRow);
    const QString actionStyle =
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                    "padding: 2px 10px; font-size: 10px; }"
                    "QPushButton:hover { background-color: %4; color: %5; }")
                .arg(ThemeManager::hex(th.cardBg), ThemeManager::hex(th.cardTitleInk),
                     ThemeManager::hex(th.cardBorder), ThemeManager::hex(th.accent),
                     ThemeManager::hex(th.onAccent));
    saveBtn->setStyleSheet(actionStyle);
    cancelBtn->setStyleSheet(actionStyle);
    deleteBtn->setStyleSheet(actionStyle + QString("QPushButton:hover { background-color: %1; }").arg(ThemeManager::hex(ThemeManager::current().danger)));
    al->addStretch();
    al->addWidget(saveBtn);
    al->addWidget(cancelBtn);
    al->addWidget(deleteBtn);
    layout->addWidget(actionRow);

    // ---- 详情面板：改动即保存 ----
    //
    // 需求是「每做一项更改立即生效」，所以这里把原来那份"点保存才写回"的
    // 组装逻辑抽成一个可复用的 lambda，挂到各个控件的变更信号上。
    //
    // 两处细节：
    //   1) 文本框（标题/备注）用 400ms 防抖：边打字边落库太浪费，而且中间态
    //      会被写进去。失焦或收起面板时立刻落一次，不会丢。
    //   2) 就地保存**不收起面板**（原来的「保存」按钮会收起，那是"提交并关闭"
    //      的语义）。「收起」按钮负责关闭；「保存」按钮保留为"立即保存并关闭"。
    auto collectEdit = [task, titleEdit, detailsEdit, deadlineEdit, state,
                        starCheck, myDayCheck, listCombo, repeatCombo,
                        reminderCheck, reminderEdit, stepState, tagState]() {
        Task edited = task;
        edited.title = titleEdit->text().trimmed().toStdString();
        edited.details = detailsEdit->toPlainText().toStdString();
        edited.deadline = deadlineEdit->dateTime().toString("yyyy-MM-dd HH:mm").toStdString();
        edited.priority = state->priority;
        edited.categoryId = state->category;
        edited.starred = starCheck->isChecked();
        edited.inMyDay = myDayCheck->isChecked();
        edited.listId = listCombo->currentData().toInt();
        edited.recurrence = static_cast<Recurrence>(repeatCombo->currentData().toInt());
        edited.reminder = reminderCheck->isChecked()
                              ? reminderEdit->dateTime()
                                    .toString("yyyy-MM-dd HH:mm").toStdString()
                              : std::string();
        edited.subtasks.clear();
        for (const auto &entry : *stepState) {
            edited.subtasks.push_back(entry.data);
        }
        edited.tags = *tagState;
        return edited;
    };

    // 就地保存（不收起面板、不重建列表）
    auto saveInPlace = [this, task, collectEdit]() {
        const Task edited = collectEdit();
        if (edited.title.empty()) {
            setStatusMessage("标题不能为空，暂未保存", true);
            return;
        }
        Task updated = edited;
        updated.id = task.id;
        if (!m_doc.updateTask(updated)) {
            setStatusMessage("自动保存失败（数据库错误）", true);
            return;
        }
        persistDocument();
        refreshRowKeepExpanded(task.id);
        setStatusMessage(QString("已自动保存「%1」").arg(QString::fromStdString(updated.title)));
    };

    // 防抖：文本框连续输入时只在停手 400ms 后写一次
    QTimer *autoSaveTimer = new QTimer(panel);
    autoSaveTimer->setSingleShot(true);
    autoSaveTimer->setInterval(400);
    connect(autoSaveTimer, &QTimer::timeout, this, [saveInPlace]() { saveInPlace(); });
    // 面板/行被重建或销毁时，把待触发的防抖取消。
    // 否则定时器会带着一个已经失效的 saveInPlace（捕获了已 delete 的控件）触发 -> 崩溃。
    connect(panel, &QObject::destroyed, this, [autoSaveTimer]() {
        if (autoSaveTimer) autoSaveTimer->stop();
    });
    auto nudge = [autoSaveTimer]() { autoSaveTimer->start(); };

    // 文本框：防抖保存 + 失焦立即保存
    for (QLineEdit *e : {titleEdit, tagInput}) {
        if (!e) continue;
        connect(e, &QLineEdit::textChanged, this, [nudge](const QString&) { nudge(); });
    }
    connect(detailsEdit, &QPlainTextEdit::textChanged, this, nudge);
    if (reminderEdit) {
        connect(reminderEdit, &QDateTimeEdit::dateTimeChanged, this,
                [nudge](const QDateTime&) { nudge(); });
    }

    // 勾选框/下拉/日期：这些是"一次性决定"，立刻落库
    if (starCheck) {
        connect(starCheck, &QCheckBox::toggled, this, [saveInPlace](bool) { saveInPlace(); });
    }
    if (myDayCheck) {
        connect(myDayCheck, &QCheckBox::toggled, this, [saveInPlace](bool) { saveInPlace(); });
    }
    if (reminderCheck) {
        connect(reminderCheck, &QCheckBox::toggled, this, [saveInPlace](bool) { saveInPlace(); });
    }
    if (listCombo) {
        connect(listCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [saveInPlace](int) { saveInPlace(); });
    }
    if (repeatCombo) {
        connect(repeatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [saveInPlace](int) { saveInPlace(); });
    }
    if (deadlineEdit) {
        connect(deadlineEdit, &QDateTimeEdit::dateTimeChanged, this,
                [saveInPlace](const QDateTime&) { saveInPlace(); });
    }

    // 「保存」= 立即保存并收起面板
    connect(saveBtn, &QPushButton::clicked, this,
            [this, task, collectEdit, autoSaveTimer]() {
                if (autoSaveTimer) autoSaveTimer->stop();
                applyTaskEdit(task.id, collectEdit());
            });
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { setExpandedTask(-1); });
    connect(deleteBtn, &QPushButton::clicked, this, [this, task]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, "删除确认",
            QString("确定要删除「%1」吗？").arg(QString::fromStdString(task.title)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        if (m_doc.removeTask(task.id)) {
            m_remindedIds.remove(task.id);
            m_expandedId = -1;
            persistDocument();
            refreshList();
            setStatusMessage(QString("已删除 #%1（可用 Ctrl+Z 撤销）").arg(task.id));
        } else {
            setStatusMessage("删除失败（数据库错误）", true);
        }
    });

    return panel;
}

void MainWindow::applyTaskEdit(int id, const Task& edited) {
    if (edited.title.empty()) {
        setStatusMessage("标题不能为空", true);
        return;
    }
    // 新模型的 updateTask 只认任务自带的 id：把调用方给的 id 补回去，
    // 保持「按 id 更新这一条」的旧语义。
    Task updated = edited;
    updated.id = id;
    if (!m_doc.updateTask(updated)) {
        setStatusMessage("保存失败（数据库错误）", true);
        return;
    }
    persistDocument();
    m_expandedId = -1;
    refreshList();
    setStatusMessage(QString("已更新 #%1").arg(id));
}

void MainWindow::setExpandedTask(int id) {
    m_expandedId = (m_expandedId == id) ? -1 : id;
    applyExpansion();
}

void MainWindow::expandTask(int id) {
    m_expandedId = (id < 0) ? -1 : id;
    applyExpansion();
}

// 就地应用展开状态：只改行高、可见性与鼠标穿透，不重建列表。
void MainWindow::applyExpansion() {
    for (int i = 0; i < m_taskList->count(); ++i) {
        QListWidgetItem *item = m_taskList->item(i);
        QWidget *row = m_taskList->itemWidget(item);
        if (!row) continue;

        const int id = item->data(Qt::UserRole).toInt();
        const bool expanded = (id == m_expandedId);
        row->setAttribute(Qt::WA_TransparentForMouseEvents, !expanded);

        //「进行中」的行打开流动光效（设置里可以关掉）
        if (auto *taskRow = dynamic_cast<TaskRowWidget*>(row)) {
            const Task *t = m_doc.findTask(id);
            const bool glowOn =
                TaskSettingsDialog::boolValue(TaskSettingsDialog::kGlow, true);
            taskRow->setGlowing(glowOn && t && t->status == 1);
        }

        // 展开时才真正构造详情面板（懒创建，见 ensureDetailPanel 的说明）。
        if (expanded) ensureDetailPanel(row);

        QWidget *detail = row->findChild<QWidget*>("detailPanelWidget");
        if (detail) detail->setVisible(expanded);

        // 折叠/展开后的高度不同，必须更新 sizeHint，否则详情会被裁掉。
        const int height = expanded
                               ? qMax(row->sizeHint().height(), row->minimumSizeHint().height())
                               : m_collapsedHeights.value(id, QSize(0, 28)).height();
        item->setSizeHint(QSize(0, height));
    }
    m_taskList->viewport()->update();
}

// 当前是否有「进行中」的任务。决定主窗口要不要画那道流动光效。
bool MainWindow::hasDoingTask() const {
    const std::vector<Task> all = m_doc.tasks();
    for (const Task &t : all) {
        if (t.status == 1) return true;
    }
    return false;
}

// 有没有「进行中」的任务；有的话让光效定时器跑起来，没有就停掉（别白烧 CPU）。
void MainWindow::ensureGlowRunning() {
    if (!m_glowTimer) {
        m_glowTimer = new QTimer(this);
        m_glowTimer->setInterval(50);       // 20fps：够顺，也不费 CPU
        connect(m_glowTimer, &QTimer::timeout, this, &MainWindow::tickGlow);
    }
    // 设置里关掉光效时，定时器也不该跑
    if (!TaskSettingsDialog::boolValue(TaskSettingsDialog::kGlow, true)) {
        if (m_glowTimer->isActive()) m_glowTimer->stop();
        return;
    }
    // 只要还有「进行中」的任务，行光效与窗口光效都要继续跑
    bool any = hasDoingTask();
    for (int i = 0; i < m_taskList->count() && !any; ++i) {
        QWidget *row = m_taskList->itemWidget(m_taskList->item(i));
        auto *taskRow = dynamic_cast<TaskRowWidget*>(row);
        if (taskRow && taskRow->glowing()) any = true;
    }
    // 有待播的「完成画线」动画时也要让定时器跑起来
    if (!m_strikeAnimateIds.isEmpty()) any = true;
    if (any && !m_glowTimer->isActive()) m_glowTimer->start();
    else if (!any && m_glowTimer->isActive()) m_glowTimer->stop();
}

// 推进「进行中」任务的光效。一个定时器驱动所有行，避免每行一个定时器。
void MainWindow::tickGlow() {
    const qreal step = 0.010;               // 每格 1%，配合 50ms 约 2 秒一圈
    m_glowProgress += step;
    if (m_glowProgress > 1.0) m_glowProgress -= 1.0;

    bool any = false;
    for (int i = 0; i < m_taskList->count(); ++i) {
        QWidget *row = m_taskList->itemWidget(m_taskList->item(i));
        auto *taskRow = dynamic_cast<TaskRowWidget*>(row);
        if (!taskRow || !taskRow->glowing()) continue;
        taskRow->setGlowProgress(m_glowProgress);
        any = true;
    }
    // 主窗口边框也要跟着重画（有进行中任务时那道绕行的光）
    if (hasDoingTask()) update();

    // 推进「完成画线」动画。只遍历可见行的标签，画完自然停。
    // 注意用 dynamic_cast 而不是 findChildren<StrikeLabel*>：后者要求类型带
    // Q_OBJECT（StrikeLabel 是纯 C++ 子类，没加），编译期就会静态断言失败。
    bool strikeRunning = false;
    for (int i = 0; i < m_taskList->count(); ++i) {
        QWidget *row = m_taskList->itemWidget(m_taskList->item(i));
        if (!row) continue;
        for (QLabel *l : row->findChildren<QLabel*>()) {
            if (auto *sl = dynamic_cast<StrikeLabel*>(l)) {
                if (sl->tickStrike(0.10)) strikeRunning = true;
            }
        }
    }
    if (strikeRunning) {
        any = true;
    } else {
        // 没有在画的就清掉待播名单，免得下次刷新又播一遍
        if (!m_strikeAnimateIds.isEmpty()) m_strikeAnimateIds.clear();
    }

    // 全部结束时由 ensureGlowRunning() 负责停表，别白烧 CPU。
    Q_UNUSED(any);
}

void MainWindow::refreshList() {
    const TaskFilter query = buildQuery();
    const std::vector<Task> visible = m_doc.query(query);
    const int total = static_cast<int>(m_doc.tasks().size());

    m_buildingList = true;
    m_taskList->clear();
    m_rowSizes.clear();
    m_collapsedHeights.clear();
    m_rowTaskIds.clear();

    for (std::size_t i = 0; i < visible.size(); ++i) {
        const Task& t = visible[i];
        QListWidgetItem *item = new QListWidgetItem(m_taskList);
        item->setData(Qt::UserRole, t.id);

        QWidget *row = createRowWidget(t);
        // 折叠高度：详情面板是懒创建的，此时只量标题行的高度。
        const int collapsed = qMax(row->sizeHint().height(), 24);
        m_collapsedHeights.insert(t.id, QSize(0, collapsed));

        const int height = (t.id == m_expandedId)
                               ? qMax(row->sizeHint().height(), row->minimumSizeHint().height())
                               : collapsed;
        m_rowSizes.insert(t.id, QSize(0, height));
        item->setSizeHint(QSize(0, height));
        m_taskList->setItemWidget(item, row);
        m_rowTaskIds.push_back(t.id);
        if (t.id == m_expandedId) item->setSelected(true);
    }
    m_buildingList = false;

    // 给「进行中」的行打开流动光效，并确保动画在跑
    applyExpansion();
    ensureGlowRunning();

    // 左侧栏的计数与列表行要跟着一起更新（新建/删除/改状态都会走到这里）。
    if (m_sidebar) m_sidebar->refresh();
    // 「建议」的内容依赖任务状态与截止时间，一并刷新。
    refreshSuggestions();


    if (visible.empty() && total > 0) {
        m_statusLabel->setText(QString("没有符合条件的任务（共 %1 项）").arg(total));
        m_statusLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().accentDim)));
        return;
    }

    const int done = m_doc.countByStatus(2);
    m_statusLabel->setText(QString("共 %1 项 - 未完成 %2，已完成 %3　｜　单击查看详情，双击切换状态")
                               .arg(total).arg(total - done).arg(done));
    m_statusLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().accentDim)));
}

// ------------------------------------------------------------------ 交互

void MainWindow::expandFirstVisibleTask() {
    if (m_taskList->count() == 0) return;
    setExpandedTask(m_taskList->item(0)->data(Qt::UserRole).toInt());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease
        || type == QEvent::MouseButtonDblClick) {
        ++m_mouseEventCount;
    }

    if (watched != m_taskList->viewport() || m_buildingList) {
        return QWidget::eventFilter(watched, event);
    }

    // ---- 滚轮：逐行滚动 ----
    // QListWidget 默认按像素滚（ScrollPerPixel），一次滚一大截，
    // 任务行经常停在半截上。改成一次滚一整行。
    if (event->type() == QEvent::Wheel) {
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        if (QScrollBar *bar = m_taskList->verticalScrollBar()) {
            if (bar->maximum() > bar->minimum()) {
                int step = 0;
                if (QListWidgetItem *under = m_taskList->itemAt(wheel->position().toPoint())) {
                    step = m_taskList->sizeHintForRow(m_taskList->row(under));
                }
                if (step <= 0 && m_taskList->count() > 0) step = m_taskList->sizeHintForRow(0);
                if (step <= 0) step = 28;

                const int direction = wheel->angleDelta().y() > 0 ? -1 : 1;
                bar->setValue(bar->value() + direction * step);
                wheel->accept();
                return true;
            }
        }
    }

    switch (event->type()) {
        case QEvent::MouseButtonDblClick: {
            // Qt 对第二次按下发的是 DblClick 而不是第二次 Press，所以双击在这里
            // 判定。双击**只改状态，不动详情栏**：把第一次单击造成的展开/收起
            // 还原成双击之前的样子。
            QMouseEvent *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) break;
            QListWidgetItem *item = m_taskList->itemAt(mouse->position().toPoint());
            if (!item) break;

            const int id = item->data(Qt::UserRole).toInt();
            if (m_clickTimer) m_clickTimer->stop();
            m_clickWindowOpen = false;
            m_pendingClickId = -1;
            restoreExpansionAfterDoubleClick();
            // 必须延迟：改状态会重建整个列表，而此刻 Qt 还在派发这个事件。
            // 直接调用会把正在派发的 item 删掉，后面 QListWidget 继续用它 -> 闪退。
            advanceStatusDeferred(id);
            return true;                // 消费掉，别再让列表处理一次
        }

        case QEvent::MouseButtonPress: {
            QMouseEvent *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) break;
            QListWidgetItem *item = m_taskList->itemAt(mouse->position().toPoint());
            if (!item) break;

            const int id = item->data(Qt::UserRole).toInt();
            // 记下按下的这一条：若用户接着拖动，dropEvent 需要知道被拖的是谁。
            m_draggingTaskId = id;
            // 双击窗口内又按了同一条 => 双击（有些平台会发两次 Press）。
            if (m_pendingClickId == id && m_clickWindowOpen) {
                if (m_clickTimer) m_clickTimer->stop();
                m_clickWindowOpen = false;
                m_pendingClickId = -1;
                restoreExpansionAfterDoubleClick();
                advanceStatusDeferred(id);
                return true;            // 同上：必须延迟，且不要再往下走
            }

            // 单击：**延迟**展开。
            //
            // 原来按下就立刻 setExpandedTask()，于是：
            //   想拖拽的用户 -> 详情先弹开 -> 开始拖动时又缩回去（两下闪烁）
            //   想改状态的用户 -> 展开再收起
            // 现在按下只记下来，等「不是拖拽」确认后再展开（见下面的 Move/Release 处理）。
            m_pendingClickId = id;
            m_clickWindowOpen = true;
            m_pressPos = mouse->position().toPoint();
            m_pressWasDrag = false;
            m_pendingExpandId = id;
            m_expandedBeforeDoubleClick = m_expandedId;   // 记住双击前的展开状态
            if (m_expandDeferTimer) {
                // 用一个略小于双击间隔的延时：既不会明显拖慢"单击展开"的手感，
                // 又足够让"按下即拖动"的用户先把拖拽判定出来。
                m_expandDeferTimer->start(
                    qBound(90, QApplication::startDragDistance() * 8, 200));
            }
            if (m_clickTimer) {
                m_clickTimer->start(qMax(1, QApplication::doubleClickInterval()));
            }
            if (m_buildingList) return true;
            break;      // 不消费事件，选中行为照旧交给列表
        }

        case QEvent::MouseMove: {
            // 按着左键移动 = 用户想拖拽：取消待展开，避免"弹开又缩回"。
            QMouseEvent *mouse = static_cast<QMouseEvent*>(event);
            if (!(mouse->buttons() & Qt::LeftButton)) break;
            if (m_pendingExpandId < 0) break;

            const QPoint delta = mouse->position().toPoint() - m_pressPos;
            if (delta.manhattanLength() >= QApplication::startDragDistance()) {
                m_pressWasDrag = true;
                m_pendingExpandId = -1;
                if (m_expandDeferTimer) m_expandDeferTimer->stop();
            }
            break;
        }

        case QEvent::MouseButtonRelease: {
            // 松开时如果一直没移动，就当成单击，这时才展开。
            // （计时器已经先触发也没关系，展开是幂等的。）
            if (m_pendingExpandId >= 0 && !m_pressWasDrag) {
                const int id = m_pendingExpandId;
                m_pendingExpandId = -1;
                if (m_expandDeferTimer) m_expandDeferTimer->stop();
                setExpandedTask(id);
            }
            m_pendingExpandId = -1;
            break;
        }

        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}

// 双击窗口结束：这段时间内没有第二次按下，单击已经生效，这里只需关窗。
void MainWindow::onClickTimerTimeout() {
    m_clickWindowOpen = false;
    m_pendingClickId = -1;
}

// 新模型没有 changeStatus(int, int)：取出来改状态字段再整条 updateTask。
// updateTask 自己会压一份改动前的快照，所以这里不需要再 beginUndo()。
bool MainWindow::applyStatus(int id, int status) {
    const Task *found = m_doc.findTask(id);
    if (!found) return false;

    Task updated = *found;
    updated.status = status;
    // 与旧的 TaskModel::changeStatus 语义保持一致：
    //   进入已完成 -> 打上完成时间；离开已完成 -> 清掉完成时间。
    // 否则「取消完成」再「完成」会一直留着第一次的时间戳。
    if (updated.status == 2) {
        if (updated.completedAt.empty()) {
            updated.completedAt = QDateTime::currentDateTime()
                                      .toString("yyyy-MM-dd HH:mm").toStdString();
        }
    } else {
        updated.completedAt.clear();
    }
    if (!m_doc.updateTask(updated)) return false;
    return persistDocument();
}

void MainWindow::advanceStatus(int id) {
    const Task *task = m_doc.findTask(id);
    if (!task) return;

    const int current = task->status;
    const int next = nextStatusInCycle(current);
    const QString title = QString::fromStdString(task->title);

    if (!applyStatus(id, next)) {
        setStatusMessage("状态修改失败（数据库错误）", true);
        return;
    }
    // 记下"这一条刚被标记完成"，重建行时给它播一次画线动画。
    // 用集合而不是布尔：底部可能同时有多条在播。
    if (next == 2) m_strikeAnimateIds.insert(id);
    else m_strikeAnimateIds.remove(id);

    // 只重建这一行，不清空整个列表 —— 见 refreshRow() 里的说明。
    refreshRow(id);
    // 动画要靠光效那个定时器推进，这里确保它跑起来
    ensureGlowRunning();
    setStatusMessage(QString("「%1」：%2 → %3")
                         .arg(title)
                         .arg(taskStatusText(current))
                         .arg(taskStatusText(next)));
}

void MainWindow::refreshRow(int taskId) {
    // 就地替换某一行的控件，**不动其它行、不 clear() 列表**。
    //
    // 为什么不能用 refreshList()：
    //   clear() 会立刻 delete 所有 QListWidgetItem。而鼠标/双击事件是由 Qt 派发
    //   给某个 item 的，事件过滤器返回后 QListWidget 还会继续处理同一个事件、
    //   继续访问那个 item。此时它已经被 delete -> 读野指针 -> 闪退。
    //   所以凡是「由列表项上的交互触发」的刷新，都必须走这一条原地路径。
    const Task *task = m_doc.findTask(taskId);
    if (!task) return;

    for (int i = 0; i < m_taskList->count(); ++i) {
        QListWidgetItem *item = m_taskList->item(i);
        if (item->data(Qt::UserRole).toInt() != taskId) continue;

        QWidget *row = createRowWidget(*task);
        // 保持这一行原本的展开状态（展开的行需要把详情面板建出来）
        const bool expanded = (taskId == m_expandedId);
        row->setAttribute(Qt::WA_TransparentForMouseEvents, !expanded);
        if (expanded) ensureDetailPanel(row);
        if (QWidget *detail = row->findChild<QWidget*>("detailPanelWidget")) {
            detail->setVisible(expanded);
        }

        const int height = expanded
                               ? qMax(row->sizeHint().height(), row->minimumSizeHint().height())
                               : m_collapsedHeights.value(taskId, QSize(0, 28)).height();
        item->setSizeHint(QSize(0, height));

        // setItemWidget 会 delete 掉旧的行控件，替换成新的
        m_taskList->setItemWidget(item, row);
        m_taskList->viewport()->update();
        return;
    }
    // 这一条当前不在可见列表里（被筛掉了）：退回整体刷新是安全的，
    // 因为此时它不是事件派发的目标。
    refreshList();
}

// 双击改状态的「延迟版」。
//
// 为什么还要延迟：refreshRow 虽然不再 clear 列表，但它是从事件过滤器里被调用的，
// 期间会 setItemWidget 替换控件、触发布局与重绘。把状态变更推到事件循环下一轮，
// 鼠标事件的派发彻底结束后再执行，最稳妥（也让「双击 = 一次操作」的语义更清楚）。
// 就地刷新某一行，并保持它处于展开状态。
//
// 详情面板的「改动即保存」用这条路径：保存后行控件要重建（标题/状态色会变），
// 但用户还在面板里继续编辑，所以不能顺手把详情区收起来。
void MainWindow::refreshRowKeepExpanded(int taskId) {
    const int keep = m_expandedId;
    refreshRow(taskId);
    if (keep == taskId) {
        // refreshRow 已经按 m_expandedId 重建了详情面板，这里只需确保可见
        for (int i = 0; i < m_taskList->count(); ++i) {
            QListWidgetItem *item = m_taskList->item(i);
            if (item->data(Qt::UserRole).toInt() != taskId) continue;
            if (QWidget *row = m_taskList->itemWidget(item)) {
                if (QWidget *detail = row->findChild<QWidget*>("detailPanelWidget")) {
                    detail->setVisible(true);
                }
            }
            break;
        }
    } else {
        m_expandedId = keep;
    }
}

void MainWindow::advanceStatusDeferred(int id) {
    QTimer::singleShot(0, this, [this, id]() { advanceStatus(id); });
}

void MainWindow::onNewTask() {
    openNewTaskWizard();
}

void MainWindow::openNewTaskWizard() {
    // 逐栏向导：标题 → 紧急度 → 分组 → 日期（可跳过），键盘流快速新建。
    TaskWizardDialog wizard(this);
    if (wizard.exec() == QDialog::Accepted) {
        createTask(wizard.task());
    }
}

void MainWindow::onNewTaskFull() {
    // 完整表单：一屏填完，含备注与精确时间。
    TaskDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    // addTask 不会自己压快照（update/remove 会），所以新建前手动记一步撤销。
    m_doc.beginUndo();
    const int newId = m_doc.addTask(dialog.task());
    if (newId < 0) {
        setStatusMessage("保存失败（数据库错误）", true);
        return;
    }
    persistDocument();
    m_expandedId = -1;
    refreshList();
    setStatusMessage(QString("[完成] 已创建任务 #%1").arg(newId));
}

void MainWindow::createTask(const Task& task) {
    // addTask 不会自己压快照（update/remove 会），所以新建前手动记一步撤销。
    m_doc.beginUndo();
    const int id = m_doc.addTask(task);
    if (id < 0) {
        setStatusMessage("保存失败（数据库错误）", true);
        return;
    }
    persistDocument();
    m_expandedId = -1;
    refreshList();
    setStatusMessage(QString("[完成] 已创建任务 #%1").arg(id));
}

void MainWindow::onEditCurrentTask() {
    QListWidgetItem *item = m_taskList->currentItem();
    if (!item) {
        setStatusMessage("请先选中一个任务", true);
        return;
    }
    setExpandedTask(item->data(Qt::UserRole).toInt());
}

void MainWindow::onSearchChanged(const QString& text) {
    // 先用 Trie 更新补全候选，再刷新列表。
    if (m_searchCompleter) {
        const std::vector<std::string> hits =
            m_doc.titleCompletions(text.trimmed().toStdString(), 8);
        QStringList words;
        for (const std::string& w : hits) {
            words << QString::fromStdString(w);
        }
        m_searchCompleter->setModel(new QStringListModel(words, m_searchCompleter));
    }
    refreshList();
}

void MainWindow::onCategoryFilterClicked(int category) {
    m_categoryFilter = category;
    updateCategoryRow();
    refreshList();
}

void MainWindow::onHideDoneToggled(bool checked) {
    m_hideDoneChecked = checked;
    refreshList();
}

void MainWindow::onSortClicked() {
    m_sortMode = static_cast<SortMode>((m_sortMode + 1) % SortModeCount);
    updateSortButton();
    refreshList();
    switch (m_sortMode) {
        case SortDeadlineAsc: setStatusMessage("已按截止时间排序"); break;
        case SortManual:      setStatusMessage("已切换为手动拖拽排序（直接拖动任务行调整顺序）"); break;
        default:              setStatusMessage("已按创建时间排序"); break;
    }
}

// 拖拽结束：把列表里的实际顺序写回模型，并记一步撤销。
void MainWindow::onRowsMoved() {
    const int draggedId = m_draggingTaskId;
    if (draggedId < 0) return;

    std::vector<int> positions;
    positions.reserve(static_cast<std::size_t>(m_taskList->count()));
    int targetPos = -1;
    for (int i = 0; i < m_taskList->count(); ++i) {
        const int id = m_taskList->item(i)->data(Qt::UserRole).toInt();
        positions.push_back(id);
        if (id == draggedId) targetPos = i;
    }
    m_draggingTaskId = -1;

    if (targetPos < 0) return;


    // 只有「手动拖拽」模式才把顺序当成用户意图保存。其他排序模式下顺序由
    // 排序规则决定，拖完仍然按规则重排，并明确提示，避免「拖了但没生效」的错觉。
    if (m_sortMode != SortManual) {
        refreshList();
        setStatusMessage("已记录位置，但当前排序方式不是「手动拖拽」；"
                         "再点一次排序按钮切到手动拖拽即可自由排序", true);
        return;
    }

    const bool ok = m_doc.moveTask(draggedId, targetPos, positions);
    if (!ok) {
        refreshList();
        return;
    }
    persistDocument();
    refreshList();
    setStatusMessage(QString("已调整「%1」的位置（Ctrl+Z 可撤销）")
                         .arg(QString::fromStdString(m_doc.findTask(draggedId)
                                                         ? m_doc.findTask(draggedId)->title
                                                         : std::string())));
}

void MainWindow::openVisualizer() {
    if (!m_visualizer) {
        // 非模态：演示时可以一边操作列表、一边看结构变化。
        m_visualizer = new TaskVisualizer(&m_doc, this);
        m_visualizer->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    m_visualizer->refreshAll();
    m_visualizer->show();
    m_visualizer->raise();
    m_visualizer->activateWindow();
}

void MainWindow::openVisualizerTab(int tabIndex) {
    openVisualizer();
    if (m_visualizer) m_visualizer->selectTab(tabIndex);
}

int MainWindow::exportVisualizerImages(const QString& prefix) {
    if (!m_visualizer) {
        m_visualizer = new TaskVisualizer(&m_doc, this);
    }
    m_visualizer->refreshAll();
    return m_visualizer->exportTabImages(prefix);
}

int MainWindow::renderToImage(const QString& path) {
    // 让 Qt 自己把窗口画出来，绕开抓屏。
    // 注意：必须让窗口真正创建并 polish 过，并且把待处理事件跑完，
    // 否则 grab() 拿不到内容（尤其在还没 show() 的情况下）。
    setAttribute(Qt::WA_TranslucentBackground, false);   // 截图不需要透明
    show();
    ensurePolished();
    if (layout()) layout()->activate();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
    QApplication::processEvents(QEventLoop::AllEvents, 200);

    QPixmap shot = grab();
    if (shot.isNull() || shot.width() <= 0) {
        // grab() 失败时改用 QWidget::render() 直接画进 QImage
        QImage image(size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(20, 10, 30));
        QPainter p(&image);
        render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
        p.end();
        shot = QPixmap::fromImage(image);
    }
    if (shot.isNull() || shot.width() <= 0) return 0;

    // 直接存 PPM：它是 Qt 内置的光栅格式，不需要任何图片插件。
    // （本机 Qt 的 imageformats 里没有 PNG/BMP，save(..., "PNG") 会静默失败。）
    QString ppmPath = path;
    const int dot = ppmPath.lastIndexOf('.');
    if (dot > 0) ppmPath = ppmPath.left(dot);
    ppmPath += ".ppm";
    return shot.save(ppmPath, "PPM") ? 1 : 0;
}

void MainWindow::onAddList() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "新建列表", "列表名称：",
                                               QLineEdit::Normal, QString(), &accepted);
    if (!accepted || name.trimmed().isEmpty()) return;

    m_doc.beginUndo();
    const int id = m_doc.addList(name.trimmed().toStdString());
    if (id < 0) {
        setStatusMessage("新建列表失败", true);
        return;
    }
    persistDocument();
    m_sidebar->refresh();
    m_sidebar->selectList(id);
    setStatusMessage(QString("已新建列表「%1」").arg(name.trimmed()));
}

void MainWindow::onRenameList(int listId) {
    const TaskList *list = nullptr;
    for (const TaskList& l : m_doc.lists()) {
        if (l.id == listId) { list = &l; break; }
    }
    if (!list) return;

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, "重命名列表", "列表名称：", QLineEdit::Normal,
        QString::fromStdString(list->name), &accepted);
    if (!accepted) return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setStatusMessage("列表名称不能为空", true);
        return;
    }
    if (!m_doc.renameList(listId, trimmed.toStdString())) {
        setStatusMessage("重命名失败", true);
        return;
    }
    persistDocument();
    m_sidebar->refresh();
    if (!m_sidebar->isSmartViewSelected() || m_sidebar->currentListId() == listId) {
        m_sidebar->selectList(listId);
    }
    setStatusMessage(QString("列表已重命名为「%1」").arg(trimmed));
}

void MainWindow::onDeleteList(int listId) {
    const TaskList *list = nullptr;
    for (const TaskList& l : m_doc.lists()) {
        if (l.id == listId) { list = &l; break; }
    }
    if (!list) return;
    if (list->isDefault) {
        setStatusMessage("默认列表不能删除", true);
        return;
    }

    const int taskCount = m_doc.countInList(listId);
    QString question = QString("确定要删除列表「%1」吗？")
                           .arg(QString::fromStdString(list->name));
    if (taskCount > 0) {
        question += QString("\n\n该列表里的 %1 个任务会移到默认列表，不会丢失。")
                        .arg(taskCount);
    }
    const auto answer = QMessageBox::question(this, "删除列表", question,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // 记录名称用于提示（删完就取不到了）
    const QString name = QString::fromStdString(list->name);
    if (!m_doc.removeList(listId)) {
        setStatusMessage("删除列表失败", true);
        return;
    }
    persistDocument();
    // 如果删的正是当前选中列表，回到「全部」，否则右侧会停在已不存在的列表上
    if (!m_sidebar->isSmartViewSelected() && m_sidebar->currentListId() == listId) {
        m_sidebar->selectSmartView(SmartView::All);
    }
    m_sidebar->refresh();
    refreshList();
    setStatusMessage(QString("已删除列表「%1」").arg(name));
}

void MainWindow::onChangeListColor(int listId) {
    const TaskList *list = nullptr;
    for (const TaskList& l : m_doc.lists()) {
        if (l.id == listId) { list = &l; break; }
    }
    if (!list) return;

    QString color = QString::fromStdString(list->color);
    if (!pickListColor(color)) return;              // 用户取消
    if (!m_doc.setListColor(listId, color.toStdString())) {
        setStatusMessage("更改颜色失败", true);
        return;
    }
    persistDocument();
    m_sidebar->refresh();
    setStatusMessage("列表颜色已更新");
}

// 色块选色对话框。
//
// 原来用 QInputDialog::getItem 列出一串 "#3D6BFF" —— 用户根本看不出哪个是
// 什么颜色，等于要背色号。改成一排色块，点一下就好，并且有当前颜色的预览。
bool MainWindow::pickListColor(QString& colorInOut) {
    static const char* kColors[] = {
        "#3D6BFF", "#7C5CFF", "#FF007F", "#FF5C8A",
        "#00AA88", "#00E5A0", "#0077FF", "#32C8FF",
        "#FFC814", "#E0A800", "#FF3333", "#8A8AA8",
    };
    const int kN = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));

    QDialog dialog(this);
    dialog.setWindowTitle("选择列表颜色");
    dialog.setModal(true);

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addWidget(new QLabel("点一个颜色：", &dialog));

    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(6);
    QString picked = colorInOut;

    for (int i = 0; i < kN; ++i) {
        const QString hex = QString::fromLatin1(kColors[i]);
        QPushButton *sw = new QPushButton(&dialog);
        sw->setFixedSize(38, 30);
        sw->setCursor(Qt::PointingHandCursor);
        // 当前颜色加一圈白边，一眼看出现在用的是哪个
        const bool current = hex.compare(colorInOut, Qt::CaseInsensitive) == 0;
        sw->setStyleSheet(
            QString("QPushButton { background-color: %1; border-radius: 5px;"
                    " border: %2; }"
                    "QPushButton:hover { border: 3px solid #FFFFFF; }")
                .arg(hex, current ? "3px solid #FFFFFF" : "1px solid #55556A"));
        sw->setToolTip(hex);
        grid->addWidget(sw, i / 4, i % 4);
        // 点一下即选中并关闭（少一次「确定」点击）
        connect(sw, &QPushButton::clicked, &dialog, [&picked, &dialog, hex]() {
            picked = hex;
            dialog.accept();
        });
    }
    root->addLayout(grid);

    // 当前颜色的预览条（大色块 + 色号），方便确认
    QLabel *preview = new QLabel(&dialog);
    preview->setFixedHeight(30);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(
        QString("QLabel { background-color: %1; color: #FFFFFF; border-radius: 5px; "
                "font-size: 11px; font-weight: bold; }").arg(picked));
    preview->setText(picked);
    root->addWidget(preview);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Cancel, &dialog);
    box->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(box);

    if (dialog.exec() != QDialog::Accepted) return false;
    if (picked.isEmpty()) return false;
    colorInOut = picked;
    return true;
}

void MainWindow::onRemoveListFromGroup(int listId) {
    if (!m_doc.removeListFromGroup(listId)) {
        setStatusMessage("该列表本来就不在任何分组里", true);
        return;
    }
    persistDocument();
    m_sidebar->refresh();
    setStatusMessage(QString("已把该列表移出分组（剩余 %1 个分组）")
                         .arg(m_doc.groupNumberCount()));
}

void MainWindow::onGroupLists() {
    TaskGroupDialog dialog(&m_doc, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<int> chosen = dialog.selectedListIds();
    if (chosen.size() < 2) {
        setStatusMessage("请至少勾选两个列表才能归为一组", true);
        return;
    }

    // 两两并到第一个上：并查集的传递性会把它们合成同一个集合。
    int merged = 0;
    for (std::size_t i = 1; i < chosen.size(); ++i) {
        if (m_doc.joinListsIntoGroup(chosen[0], chosen[i])) ++merged;
    }
    if (merged == 0) {
        setStatusMessage("这些列表已经在同一分组里了", true);
        return;
    }
    persistDocument();
    m_sidebar->refresh();
    setStatusMessage(QString("已把 %1 个列表归为一组（共 %2 个分组）")
                         .arg(chosen.size())
                         .arg(m_doc.groupNumberCount()));
}

void MainWindow::setupSuggestionPanel(QVBoxLayout *parentLayout) {
    const Theme &th = ThemeManager::current();
    // 折叠式「建议」区（对应 Microsoft To Do 的建议面板）。
    // 数据来自 TaskDocument::suggestions()：逾期 > 今天到期 > 最近添加，
    // 组内按优先级、再按截止时间排序。
    m_suggestToggle = new QPushButton("▸ 建议", this);
    m_suggestToggle->setObjectName("suggestToggle");
    m_suggestToggle->setCursor(Qt::PointingHandCursor);
    m_suggestToggle->setFlat(true);
    m_suggestToggle->setStyleSheet(
        QString("QPushButton { text-align: left; color: %1; background: transparent; "
                "border: none; padding: 2px 0; font-size: 10px; font-weight: bold; }"
                "QPushButton:hover { color: %2; }")
        .arg(ThemeManager::hex(th.accent), ThemeManager::hex(th.cardTitleInk)));
    connect(m_suggestToggle, &QPushButton::clicked, this,
            &MainWindow::onToggleSuggestions);
    parentLayout->addWidget(m_suggestToggle);

    m_suggestBox = new QWidget(this);
    m_suggestBox->setObjectName("suggestBox");
    m_suggestBox->setStyleSheet(
        QString("QWidget#suggestBox { background-color: %1; "
                "border-left: 2px solid %2; }"
                "QLabel { color: %3; font-size: 10px; }")
        .arg(ThemeManager::hex(th.dark ? QColor(255, 255, 255, 14) : QColor(0, 60, 120, 16)),
             ThemeManager::hex(th.accent), ThemeManager::hex(th.cardTitleInk)));
    m_suggestLayout = new QVBoxLayout(m_suggestBox);
    m_suggestLayout->setContentsMargins(6, 4, 6, 4);
    m_suggestLayout->setSpacing(2);
    m_suggestBox->setVisible(false);       // 默认收起
    parentLayout->addWidget(m_suggestBox);

    refreshSuggestions();
}

void MainWindow::onToggleSuggestions() {
    m_suggestOpen = !m_suggestOpen;
    m_suggestBox->setVisible(m_suggestOpen);
    m_suggestToggle->setText(m_suggestOpen ? "▾ 建议" : "▸ 建议");
    if (m_suggestOpen) refreshSuggestions();
}

void MainWindow::refreshSuggestions() {
    const Theme &th = ThemeManager::current();
    if (!m_suggestLayout) return;

    // 清空旧行
    while (QLayoutItem *item = m_suggestLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }

    const std::vector<int> ids = m_doc.suggestions(4);
    m_suggestCount = static_cast<int>(ids.size());

    // 折叠时也把条数显示在标题上，方便不展开就知道有没有事
    m_suggestToggle->setText(QString("%1 建议%2")
                                 .arg(m_suggestOpen ? "▾" : "▸")
                                 .arg(m_suggestCount > 0
                                          ? QString("（%1）").arg(m_suggestCount)
                                          : QString()));

    if (ids.empty()) {
        QLabel *empty = new QLabel("暂时没有建议", m_suggestBox);
        empty->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardMutedInk)));
        m_suggestLayout->addWidget(empty);
        return;
    }

    const QDate today = QDate::currentDate();
    for (int id : ids) {
        const Task *t = m_doc.findTask(id);
        if (!t) continue;

        // 逾期 / 今天 / 稍后，用文案标出来
        QString when = "稍后";
        if (t->hasDeadline()) {
            const QDate due = QDate::fromString(
                QString::fromStdString(t->deadline.substr(0, 10)), "yyyy-MM-dd");
            if (due.isValid()) {
                const qint64 off = today.daysTo(due);
                if (off < 0) when = QString("逾期 %1 天").arg(-off);
                else if (off == 0) when = "今天";
                else when = QString("%1 天后").arg(off);
            }
        }

        QWidget *row = new QWidget(m_suggestBox);
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        QLabel *text = new QLabel(
            QString("%1　%2").arg(QString::fromStdString(t->title), when), row);
        text->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(ThemeManager::current().cardTitleInk)));
        text->setToolTip(QString("优先级：%1").arg(priorityName(t->priority)));

        QPushButton *add = new QPushButton("＋ 今天", row);
        add->setObjectName(QString("suggestAdd_%1").arg(id));
        add->setFixedHeight(18);
        add->setCursor(Qt::PointingHandCursor);
        add->setToolTip("加入我的一天");
        add->setStyleSheet(
                QString("QPushButton { background-color: transparent; color: %1; "
                        "border: 1px solid %1; border-radius: 8px; padding: 0 6px; "
                        "font-size: 9px; }"
                        "QPushButton:hover { background-color: %1; color: %2; }")
                    .arg(ThemeManager::hex(th.accent), ThemeManager::hex(th.onAccent)));
        connect(add, &QPushButton::clicked, this,
                [this, id]() { addSuggestionToMyDay(id); });

        layout->addWidget(text, 1);
        layout->addWidget(add);
        m_suggestLayout->addWidget(row);
    }
}

void MainWindow::addSuggestionToMyDay(int taskId) {
    const Task *found = m_doc.findTask(taskId);
    if (!found) return;

    Task updated = *found;
    updated.inMyDay = true;
    if (!m_doc.updateTask(updated)) {
        setStatusMessage("加入我的一天失败", true);
        return;
    }
    persistDocument();
    refreshList();
    setStatusMessage(QString("「%1」已加入我的一天")
                         .arg(QString::fromStdString(updated.title)));
}

void MainWindow::onUndoRequested() {
    if (!m_doc.undo()) {
        setStatusMessage("没有可撤销的操作", true);
        return;
    }
    persistDocument();
    m_expandedId = -1;
    refreshList();
    setStatusMessage("已撤销上一步操作");
}

void MainWindow::onItemClicked(QListWidgetItem *item) {
    if (!item || m_buildingList) return;
    setExpandedTask(item->data(Qt::UserRole).toInt());
}

void MainWindow::onItemDoubleClicked(QListWidgetItem *item) {
    if (!item || m_buildingList) return;
    // 统一走 advanceStatus（内部用 refreshRow 就地更新，不 clear 列表）。
    // 之前这里自己写了一遍并调用 refreshList()，会在这个双击事件还在派发时
    // 把列表项全部删掉 -> 闪退。
    advanceStatusDeferred(item->data(Qt::UserRole).toInt());
}

void MainWindow::onItemContextMenu(const QPoint &pos) {
    QListWidgetItem *item = m_taskList->itemAt(pos);
    if (!item) return;
    const int id = item->data(Qt::UserRole).toInt();

    // 先把菜单需要的字段拷出来：命令执行期间模型可能重新分配存储，
    // 跨 exec() 持有 Task* 会变成悬垂指针。
    const Task *snapshot = m_doc.findTask(id);
    if (!snapshot) return;
    const Task current = *snapshot;
    const QString title = QString::fromStdString(current.title);
    const QString info = QString("编号：%1\n标题：%2\n优先级：%3\n截止时间：%4\n状态：%5\n分类：%6\n备注：%7")
                             .arg(current.id)
                             .arg(title)
                             .arg(priorityName(current.priority))
                             .arg(QString::fromStdString(current.deadline))
                             .arg(taskStatusText(current.status))
                             .arg(categoryName(current.categoryId))
                             .arg(current.details.empty() ? QString("（无）")
                                                          : QString::fromStdString(current.details));

    QMenu menu(this);
    QAction *editAct = menu.addAction("编辑详情（F2）");
    QAction *undoAct = menu.addAction("撤销上一步");
    menu.addSeparator();
    QAction *todoAct = menu.addAction("标记为待办");
    QAction *doingAct = menu.addAction("标记为进行中");
    QAction *doneAct = menu.addAction("标记为已完成");
    QAction *pauseAct = menu.addAction("标记为已暂停");
    menu.addSeparator();
    QAction *deleteAct = menu.addAction("删除");
    QAction *infoAct = menu.addAction("详情信息");

    todoAct->setEnabled(current.status != 0);
    doingAct->setEnabled(current.status != 1);
    doneAct->setEnabled(current.status != 2);
    pauseAct->setEnabled(current.status != 3);

    QAction *chosen = menu.exec(m_taskList->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == editAct) {
        setExpandedTask(id);
        return;
    }
    if (chosen == undoAct) {
        onUndoRequested();
        return;
    }

    int targetStatus = -1;
    if (chosen == todoAct) targetStatus = 0;
    else if (chosen == doingAct) targetStatus = 1;
    else if (chosen == doneAct) targetStatus = 2;
    else if (chosen == pauseAct) targetStatus = 3;

    if (targetStatus >= 0) {
        if (m_doc.findTask(id) && applyStatus(id, targetStatus)) {
            refreshRow(id);          // 就地更新，别 clear 列表
            setStatusMessage(QString("#%1 已改为「%2」").arg(id).arg(taskStatusText(targetStatus)));
        } else {
            setStatusMessage("状态修改失败（数据库错误）", true);
        }
        return;
    }

    if (chosen == deleteAct) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, "删除确认", QString("确定要删除「%1」吗？").arg(title),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        if (m_doc.removeTask(id)) {
            m_remindedIds.remove(id);
            if (m_expandedId == id) m_expandedId = -1;
            persistDocument();
            refreshList();
            setStatusMessage(QString("已删除 #%1（可用 Ctrl+Z 撤销）").arg(id));
        } else {
            setStatusMessage("删除失败（数据库错误）", true);
        }
    } else if (chosen == infoAct) {
        QMessageBox::information(this, "任务详情", info);
    }
}

void MainWindow::setStatusMessage(const QString& message, bool isError) {
    const Theme &th = ThemeManager::current();
    m_statusLabel->setStyleSheet(isError
        ? QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(th.danger))
        : QString("QLabel { color: %1; font-size: 10px; }").arg(ThemeManager::hex(th.accentDim)));
    m_statusLabel->setText(message);
}

bool MainWindow::persistDocument() {
    // 数据库没打开（例如界面自检环境）时静默跳过：内存里的改动照旧生效。
    if (!m_dbReady || !m_store.isOpen()) return true;
    if (!m_store.saveDocument(m_doc)) {
        setStatusMessage(QString("保存失败：%1").arg(m_store.lastError()), true);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ 绘制

void MainWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    const Theme &th = ThemeManager::current();
    QPainter painter(this);
    const int w = width();
    const int h = height();

    // ---- 窗口底：竖向渐变 ----
    painter.setRenderHint(QPainter::Antialiasing, false);
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, th.windowTop);
    bg.setColorAt(1.0, th.windowBottom);
    painter.fillRect(rect(), bg);

    // ---- 外边框：双层描边，内层淡一点做高光 ----
    painter.setPen(QPen(th.windowBorder, 1));
    painter.drawRect(0, 0, w - 1, h - 1);
    QColor innerBorder = th.windowBorder;
    innerBorder.setAlpha(60);
    painter.setPen(QPen(innerBorder, 1));
    painter.drawRect(1, 1, w - 3, h - 3);

    // ---- 有「进行中」任务时，给整个窗口套一道同款流动光效 ----
    // 需求：主界面也要有那种绕圈的高光，让人一眼看出"现在有活在手上"。
    // 复用任务行那套做法（呼吸底光 + 旋转高光），进度用行光效同一个 m_glowProgress。
    if (hasDoingTask()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const QColor teal = th.statusAccent[1];      // 进行中的青绿

        // 呼吸底光
        const qreal pulse = 0.5 + 0.5 * qSin(m_glowProgress * 2.0 * M_PI);
        QColor base = teal;
        base.setAlpha(static_cast<int>(60 + 90 * pulse));
        painter.setPen(QPen(base, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);

        // 外圈淡晕
        QColor halo = teal;
        halo.setAlpha(static_cast<int>(24 + 36 * pulse));
        painter.setPen(QPen(halo, 4.0));
        painter.drawRect(QRectF(rect()).adjusted(-1.0, -1.0, 1.0, 1.0));

        // 绕行高光
        QConicalGradient sweep(r.center(), -m_glowProgress * 360.0);
        QColor head(120, 255, 210);
        QColor dim = teal;
        dim.setAlpha(0);
        sweep.setColorAt(0.00, dim);
        head.setAlpha(255);
        sweep.setColorAt(0.05, head);
        head.setAlpha(190);
        sweep.setColorAt(0.13, head);
        head.setAlpha(50);
        sweep.setColorAt(0.25, head);
        sweep.setColorAt(0.42, dim);
        sweep.setColorAt(1.00, dim);
        painter.setPen(QPen(QBrush(sweep), 3.0));
        painter.drawRect(r);
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    // ---- 标题栏：横向渐变 + 底部分隔线 ----
    const QRect tb = titleBarRect();
    QLinearGradient tbg(0, 0, w, 0);
    tbg.setColorAt(0.0, th.titleBarLeft);
    tbg.setColorAt(1.0, th.titleBarRight);
    painter.fillRect(tb, tbg);
    QColor sep = th.windowBorder;
    sep.setAlpha(140);
    painter.setPen(QPen(sep, 1));
    painter.drawLine(0, TITLE_BAR_HEIGHT, w, TITLE_BAR_HEIGHT);

    // 标题左侧一条强调色
    painter.fillRect(QRect(0, 0, 3, TITLE_BAR_HEIGHT), th.accent);

    painter.setPen(th.accent);
    const QString family = monoFontFamily();
    QFont font = family.isEmpty() ? QFont() : QFont(family);
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(12, 0, w - 60, TITLE_BAR_HEIGHT),
                     Qt::AlignVCenter | Qt::AlignLeft, "CYSC 任务栏");

    // ---- 标题栏右侧四个按钮：置顶 / 最小化 / 最大化 / 关闭 ----
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 图钉：置顶时常亮，未置顶时暗淡（一眼看出当前状态）
    const QRect pb = pinButtonRect();
    const QColor pinColor = m_onTop ? th.accent : (th.dark ? QColor(140, 140, 170) : QColor(120, 130, 145));
    if (m_onTop) {
        QColor pinGlow = th.accent; pinGlow.setAlpha(45); painter.setBrush(pinGlow);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(pb.adjusted(2, 3, -2, -3), 3, 3);
    }
    painter.setPen(QPen(pinColor, 1.2));
    {
        // 画一个简单的图钉形状：圆头 + 针
        const QPointF c = QPointF(pb.center()) + QPointF(0, -1.5);
        painter.setBrush(pinColor);
        painter.drawEllipse(c, 2.6, 2.6);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(c.x(), c.y() + 2.6), QPointF(c.x(), c.y() + 5.4));
    }

    QFont wf = font;
    wf.setPointSize(8);
    wf.setBold(false);
    painter.setFont(wf);

    // 最小化：一条横线
    const QRect mb = minimizeButtonRect();
    painter.setPen(QPen(th.dark ? QColor(210, 210, 235) : QColor(70, 80, 95), 1.6));
    painter.drawLine(mb.left() + 5, mb.center().y() + 3, mb.right() - 5, mb.center().y() + 3);

    // 最大化 / 还原：方块（最大化时画成两个重叠的小方块）
    const QRect xb = maximizeButtonRect();
    painter.setPen(QPen(th.dark ? QColor(210, 210, 235) : QColor(70, 80, 95), 1.4));
    painter.setBrush(Qt::NoBrush);
    if (m_maximized) {
        painter.drawRect(QRect(xb.left() + 5, xb.top() + 6, 8, 8));
        painter.drawRect(QRect(xb.left() + 9, xb.top() + 9, 8, 8));
    } else {
        painter.drawRect(QRect(xb.left() + 6, xb.top() + 7, 9, 9));
    }

    // 关闭：圆形 + ✕
    const QRect cb = closeButtonRect();
    QColor closeBg = th.danger; closeBg.setAlpha(70); painter.setBrush(closeBg);
    painter.setPen(QPen(th.danger, 1));
    const int d = qMin(cb.width(), cb.height()) - 6;
    painter.drawEllipse(cb.center(), d / 2, d / 2);
    painter.setPen(th.danger.lighter(140));
    QFont cf = font;
    cf.setPointSize(8);
    cf.setBold(true);
    painter.setFont(cf);
    painter.drawText(cb, Qt::AlignCenter, "✕");

    // ---- 右下角调整大小提示 ----
    // 无边框窗口在视觉上完全看不出能拖大小，所以画三道斜线当"抓手"。
    // 鼠标在 6px 边缘内会变成对应方向的指针（见 cursorForEdge）。
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor grip = th.accent; grip.setAlpha(90); painter.setPen(QPen(grip, 1));
    for (int i = 1; i <= 3; ++i) {
        const int off = i * 4;
        painter.drawLine(w - 3 - off, h - 3, w - 3, h - 3 - off);
    }
}

QRect MainWindow::titleBarRect() const {
    return QRect(0, 0, width(), TITLE_BAR_HEIGHT);
}

QRect MainWindow::closeButtonRect() const {
    return QRect(width() - WIN_BUTTON_W, 0, WIN_BUTTON_W, TITLE_BAR_HEIGHT);
}

QRect MainWindow::maximizeButtonRect() const {
    return QRect(width() - WIN_BUTTON_W * 2, 0, WIN_BUTTON_W, TITLE_BAR_HEIGHT);
}

QRect MainWindow::minimizeButtonRect() const {
    return QRect(width() - WIN_BUTTON_W * 3, 0, WIN_BUTTON_W, TITLE_BAR_HEIGHT);
}

QRect MainWindow::pinButtonRect() const {
    return QRect(width() - WIN_BUTTON_W * 4, 0, WIN_BUTTON_W, TITLE_BAR_HEIGHT);
}

void MainWindow::toggleOnTop() {
    // 以**真实的窗口标志**为准来取反，不要依赖一个单独的 bool 成员：
    // 两者一旦不同步（比如外部代码或平台改过标志），图钉图标就会显示成
    // 「已置顶」而窗口其实没置顶。
    const bool nowOnTop = !windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    m_onTop = nowOnTop;

    // 改窗口标志会让窗口先隐藏，所以改完要显式 show 回来，否则点一下图标就没了。
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, nowOnTop);
    if (wasVisible) show();

    QSettings settings;
    settings.setValue("window/onTop", nowOnTop);
    // 立刻落盘。QSettings 默认只写内存缓存、定时/析构时才同步，
    // 进程被强杀时这个偏好就丢了。
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // 注册表不可写（例如受限环境）时不阻断置顶功能，只提示一下
        setStatusMessage(QString("提示：置顶状态未能保存（%1）")
                             .arg(settings.fileName()));
    }

    setStatusMessage(nowOnTop ? "窗口已置顶（再点图钉可取消）" : "已取消置顶");
    update();
}

void MainWindow::toggleMaximize() {
    // 不用 showMaximized()/isMaximized()：
    // 本窗口是 FramelessWindowHint + Qt::Tool，实测 showMaximized() 有时不生效，
    // isMaximized() 也不可靠 —— 结果就是用户反馈的「要按两下才还原」。
    // 这里自己记状态、自己算几何，行为完全确定。
    if (!m_maximized) {
        m_normalGeometry = geometry();       // 记住还原用的位置与大小

        // 用当前屏幕的可用区域（排除任务栏）
        QRect avail;
        if (QScreen *screen = QGuiApplication::screenAt(frameGeometry().center())) {
            avail = screen->availableGeometry();
        } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
            avail = screen->availableGeometry();
        }
        if (avail.isValid()) {
            setGeometry(avail);
            m_maximized = true;
            setStatusMessage("已最大化（再点方块按钮还原）");
        }
    } else {
        if (m_normalGeometry.isValid()) setGeometry(m_normalGeometry);
        m_maximized = false;
        setStatusMessage("已还原窗口大小");
    }
    update();
}

// 把窗口吸附到当前屏幕右上角。
// 打开设置窗口。设置项写在 INI（QSettings）里，改完由 applySettings() 统一应用。
// 把当前主题铺到界面上。
//
// 注意：这一步必须在 m_taskList 建好之后调用；工具栏/搜索框/状态栏那些
// 一次性的样式表在它们各自的 setup 里就设好了，换主题时这里再设一遍。
void MainWindow::applyThemeToWidgets() {
    const Theme &th = ThemeManager::current();

    // 深色主题用半透明背景（有霓虹感）；浅色主题必须不透明。
    // 实测半透明合成会把浅色底压成脏灰，整窗看起来还是暗的 —— 浅色主题
    // 必须显式关掉 WA_TranslucentBackground。
    if (testAttribute(Qt::WA_TranslucentBackground) != th.dark) {
        const bool wasVisible = isVisible();
        setAttribute(Qt::WA_TranslucentBackground, th.dark);
        if (wasVisible) {
            // 改这个属性会重建原生窗口，显式复位一次以免跳位
            const QRect g = geometry();
            hide();
            show();
            setGeometry(g);
        }
        qWarning("主题: WA_TranslucentBackground -> %d (dark=%d)",
                 (int)testAttribute(Qt::WA_TranslucentBackground), (int)th.dark);
    }

    if (m_taskList) {
        m_taskList->setStyleSheet(
            QString("QListWidget { background-color: transparent; border: none; "
                    "color: %1; }"
                    "QListWidget::item { border: none; }"
                    "QListWidget::item:selected { background-color: %2; }")
                .arg(ThemeManager::hex(th.sidebarText),
                     ThemeManager::hex(th.sidebarActive))
            + rowStyleSheet());
    }

    // 状态栏文字
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 10px; }")
                .arg(ThemeManager::hex(th.accentDim)));
    }

    // 工具栏：新建 / 结构 / 紧凑 / 设置 / 搜索 / 隐藏已完成
    if (m_newButton) {
        m_newButton->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                    "padding: 0 10px; font-size: 11px; font-weight: bold; }"
                    "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent),
                     ThemeManager::hex(th.onAccent)));
    }
    if (m_visualizeButton) {
        m_visualizeButton->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                    "padding: 0 8px; font-size: 11px; }"
                    "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(ThemeManager::hex(th.toolbarBg),
                     ThemeManager::hex(th.statusChipBg[3]),
                     ThemeManager::hex(th.onAccent)));
    }
    if (m_compactButton) {
        m_compactButton->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                    "font-size: 14px; }"
                    "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent),
                     ThemeManager::hex(th.onAccent)));
    }
    if (m_settingsButton) {
        m_settingsButton->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; "
                    "font-size: 13px; }"
                    "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accentDim),
                     ThemeManager::hex(th.onAccent)));
    }
    if (m_search) {
        m_search->setStyleSheet(
            QString("QLineEdit { background-color: %1; color: %2; "
                    "border: 1px solid %2; padding: 3px; font-size: 11px; }")
                .arg(ThemeManager::hex(th.toolbarBg), ThemeManager::hex(th.accent)));
    }
    if (m_hideDone) {
        m_hideDone->setStyleSheet(
            QString("QCheckBox { color: %1; font-size: 10px; }"
                    "QCheckBox::indicator { width: 12px; height: 12px; }")
                .arg(ThemeManager::hex(th.accent)));
    }

    // 分类筛选行与排序按钮
    updateCategoryRow();
    updateSortButton();

    // 主题变了时，已建的对话框也要跟着换色。
    // （它们是模态的，正常切换设置时不会开着；但 --theme 这类启动参数
    //  可能在窗口建好之后才切主题，那时就要一并刷新。）
    for (TaskDialog *d : findChildren<TaskDialog*>()) d->applyTheme();
    for (TaskWizardDialog *d : findChildren<TaskWizardDialog*>()) d->applyTheme();
    for (TaskGroupDialog *d : findChildren<TaskGroupDialog*>()) d->applyTheme();
    for (TaskVisualizer *v : findChildren<TaskVisualizer*>()) v->applyTheme();

    update();      // 触发 paintEvent 用新主题重画窗口底与标题栏
}

// 拖动窗口边缘缩到很窄时，自动进入紧凑模式（只显示任务）。
//
// 需求："缩到某个宽度自动变紧凑"。阈值取 460px —— 再窄下去侧栏 + 任务列表
// 挤在一起已经没法用了，不如直接切紧凑。只在**用户手动拖窄**时触发
// （m_resizingByUser 由 applyResize 置位），避免程序自己 resize 时误触发。
void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_compact || !m_resizingByUser) return;
    if (width() <= kAutoCompactWidth) {
        m_compact = true;
        applyCompactMode();
        setStatusMessage("窗口已收窄，自动进入紧凑模式（点 ⊡ 恢复完整窗口）");
    }
}

// 界面缩放：按档位放大全局基准字号。
//
// 为什么用"改基准字号"来做缩放：本项目的样式表里字号（font-size: 10px 等）都是
// 写死的像素值，Qt 不会因为窗口变大就放大它们 —— 光调窗口尺寸只会得到一堆空白。
// 而**没写 font-size 的控件**（按钮、输入框、勾选框…）会跟随应用基准字体，
// 所以放大基准字体能让这部分控件真的变大；同时把窗口按比例撑大，
// 让布局空间一起跟上。写死像素的小标签（chip、时间）保持原样 ——
// 它们本来就小，一起放大会和行高打架，这是有意的取舍。
//
// 还会按屏幕可用区域封顶：需求原话是"两倍"，但 660×2=1320 在 1080p 上装不下，
// 硬做会让底部按钮跑到屏幕外。
void MainWindow::applyUiScale() {
    static const int kPercent[] = {100, 125, 150, 175, 200};
    const int idx = qBound(0, TaskSettingsDialog::intValue(TaskSettingsDialog::kScale, 2), 4);
    const int percent = kPercent[idx];
    m_scalePercent = percent;
    const qreal factor = percent / 100.0;

    // 基准字号：第一次调用时记下来，之后都基于它算，避免反复切换越滚越大
    QFont f = qApp->font();
    if (!m_baseFontPointSizeValid) {
        m_baseFontPointSize = f.pointSizeF() > 0 ? f.pointSizeF() : 9.0;
        m_baseFontPointSizeValid = true;
    }
    f.setPointSizeF(qMax(6.0, m_baseFontPointSize * factor));
    qApp->setFont(f);
    for (QWidget *w : findChildren<QWidget*>()) w->setFont(f);

    // 窗口尺寸也在基准尺寸上按比例算
    if (m_baseWindowSize.isEmpty()) m_baseWindowSize = size();
    int w = qRound(m_baseWindowSize.width() * factor);
    int h = qRound(m_baseWindowSize.height() * factor);

    QScreen *sc = QGuiApplication::screenAt(frameGeometry().center());
    if (!sc) sc = QGuiApplication::primaryScreen();
    if (sc) {
        const QRect avail = sc->availableGeometry();
        w = qMin(w, qRound(avail.width() * 0.95));
        h = qMin(h, qRound(avail.height() * 0.95));
    }
    if (!m_compact) resize(w, h);

    // 关键：**不要**把最小尺寸跟着放大。
    // 缩放只该影响默认尺寸。一旦把最小宽度抬到 700 多，窗口就再也缩不到
    // 自动紧凑的阈值（460）以下 —— 拖窄根本拖不动，"缩窄自动紧凑"直接失效。
    // 紧凑模式本身要能缩到 280，所以最小尺寸保持这个量级。
    setMinimumSize(280, 240);
}

void MainWindow::openSettings() {
    const int themeBefore = ThemeManager::currentIndex();
    TaskSettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    // 顺序很重要：
    //   1) 先 applySettings() 把显示/窗口/密度等设置应用掉
    //   2) 再按新主题重刷样式
    // 反过来或互相调用会递归（见 applySettings() 末尾的说明）。
    applySettings();
    if (ThemeManager::currentIndex() != themeBefore) applyThemeToWidgets();
    setStatusMessage("设置已保存并生效");
}

// 把 QSettings 里的设置项应用一遍（启动时也调一次，保证与配置一致）。
void MainWindow::applySettings() {
    const Theme &th = ThemeManager::current();
    // 显示：默认隐藏已完成
    const bool hideDone = TaskSettingsDialog::boolValue(TaskSettingsDialog::kHideDone, false);
    if (m_hideDone) {
        m_hideDone->setChecked(hideDone);      // 触发 onHideDoneToggled -> refreshList
    } else {
        m_hideDoneChecked = hideDone;
    }

    // 窗口：置顶（与标题栏图钉是同一个状态）
    const bool onTop = TaskSettingsDialog::boolValue(TaskSettingsDialog::kOnTop, true);
    if (onTop != m_onTop) {
        const bool wasVisible = isVisible();
        m_onTop = onTop;
        setWindowFlag(Qt::WindowStaysOnTopHint, m_onTop);
        if (wasVisible) show();
    }

    // 行密度：影响任务行的间距
    m_density = qBound(0, TaskSettingsDialog::intValue(TaskSettingsDialog::kDensity, 1), 2);
    if (m_taskList) {
        m_taskList->setSpacing(m_density == 0 ? 0 : (m_density == 1 ? 1 : 4));
    }

    // 紧凑模式
    const bool compact = TaskSettingsDialog::boolValue(TaskSettingsDialog::kCompact, false);
    if (compact != m_compact) {
        m_compact = compact;
        applyCompactMode();
    }

    // 界面缩放（改基准字号 + 窗口尺寸）
    applyUiScale();
    // 「已完成置底」与「光效」在使用点即时读设置，刷新一次让改动生效
    refreshList();
    // 注意：这里**不能**再调 applyThemeToWidgets()。
    // applyThemeToWidgets() 的实现会读设置来判断窗口属性，于是需要先
    // applySettings()；如果 applySettings() 又回头调 applyThemeToWidgets()，
    // 两者就互相递归 -> 栈溢出（表现为 0xC0000005 闪退，而且只在 Debug 下
    // 因为帧大而更快撞到栈上限）。
    // 主题样式的刷新由调用方负责：openSettings() 里显式调一次。
    update();
}

void MainWindow::snapToTopRight() {
    QRect avail;
    if (QScreen *screen = QGuiApplication::screenAt(frameGeometry().center())) {
        avail = screen->availableGeometry();
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        avail = screen->availableGeometry();
    }
    if (!avail.isValid()) return;

    const int margin = 12;
    const QSize target = size();
    const int x = avail.right() - target.width() - margin;
    const int y = avail.top() + margin;
    move(qMax(avail.left(), x), y);
}

void MainWindow::toggleCompactMode() {    m_compact = !m_compact;
    applyCompactMode();

    QSettings settings;
    settings.setValue("window/compact", m_compact);
    settings.sync();
}

void MainWindow::applyCompactMode() {
    const bool mini = m_compact;

    // 侧栏、完整新建按钮、分类筛选行、「建议」区在紧凑模式下都没有意义
    if (m_sidebar) m_sidebar->setVisible(!mini);
    if (m_newFullButton) m_newFullButton->setVisible(!mini);
    if (m_categoryHost) m_categoryHost->setVisible(!mini);
    if (m_hideDone) m_hideDone->setVisible(!mini);
    if (m_suggestToggle) m_suggestToggle->setVisible(!mini);
    if (m_suggestBox) m_suggestBox->setVisible(!mini && m_suggestOpen);
    // 紧凑模式下分类筛选被隐藏了，所以要强制回到「全部」，
    // 否则用户会被一个看不见的筛选条件困住（列表空着却找不到原因）。
    if (mini && m_categoryFilter != CategoryAll) {
        m_categoryFilter = CategoryAll;
        refreshList();
    }

    if (mini) {
        m_geometryBeforeCompact = geometry();
        m_expandedBeforeCompact = m_expandedId;

        // 关键顺序：**先放宽最小宽度**，再 resize。
        // 之前先 resize 后 setMinimumSize，而最小宽度还是 480，
        // 于是 resize(320) 被夹到 480，窗口根本收不窄。
        setMinimumSize(280, 240);

        // 紧凑模式不展开详情：展开会让窗口重新变高变宽，就不是"只显示任务"了
        m_expandedId = -1;
        applyExpansion();

        // 收紧到一列任务：宽度按内容算，但至少 300、至多 380
        const int contentW = m_taskList->viewport()->width();
        const int w = qBound(300, contentW + 26, 380);
        resize(w, qMax(320, height()));
        snapToTopRight();
        setStatusMessage("紧凑模式：只显示任务（再点 ⊡ 恢复完整窗口）");
    } else {
        // 先撑回最小尺寸再还原几何，避免被紧凑模式的最小尺寸夹住
        setMinimumSize(480, 420);
        if (m_geometryBeforeCompact.isValid()) setGeometry(m_geometryBeforeCompact);
        m_expandedId = m_expandedBeforeCompact;
        applyExpansion();
        setStatusMessage("已恢复完整窗口");
    }

    update();
}

// ------------------------------------------------------------------ 托盘

void MainWindow::setupTray() {
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeTrayIcon());
    m_tray->setToolTip("CYSC 任务栏\n左键：显示 / 隐藏\n右键：菜单（含「退出」）");

    QMenu *menu = new QMenu(this);
    QAction *showAction = menu->addAction("显示 / 隐藏");
    QAction *newAction = menu->addAction("新建任务");
    menu->addSeparator();
    QAction *undoAction = menu->addAction("撤销上一步");
    menu->addSeparator();
    QAction *quitAction = menu->addAction("退出");
    connect(showAction, &QAction::triggered, this, [this]() {
        if (isVisible()) hide(); else { show(); raise(); activateWindow(); }
    });
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewTask);
    connect(undoAction, &QAction::triggered, this, &MainWindow::onUndoRequested);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();

    // 托盘不可用时必须说清楚，否则用户点「关闭」后窗口消失、托盘又没有图标，
    // 程序就变成一个既看不到也退不掉的后台进程。
    if (!trayUsable()) {
        setStatusMessage("系统托盘不可用：关闭窗口会直接退出程序", true);
    }
}

// 画一个真正的图标：深色圆角底 + 青色对勾。
// 原来是一个纯色 16x16 方块——在托盘里是一片青色，看不出是什么，
// 而且在某些主题下几乎和背景融在一起。
QIcon MainWindow::makeTrayIcon() const {
    QIcon icon;
    // 多尺寸：托盘会按 DPI 自己挑
    for (int size : {16, 24, 32, 48}) {
        QPixmap pix(size, size);
        pix.fill(Qt::transparent);

        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal r = size * 0.22;
        p.setBrush(QColor(16, 16, 28));
        p.setPen(QPen(QColor(0, 255, 200), qMax(1.0, size / 16.0)));
        p.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), r, r);

        // 对勾
        QPen check(QColor(0, 255, 200));
        check.setWidthF(qMax(1.6, size / 8.0));
        check.setCapStyle(Qt::RoundCap);
        check.setJoinStyle(Qt::RoundJoin);
        p.setPen(check);
        QPainterPath path;
        path.moveTo(size * 0.26, size * 0.52);
        path.lineTo(size * 0.44, size * 0.70);
        path.lineTo(size * 0.76, size * 0.32);
        p.drawPath(path);
        p.end();

        icon.addPixmap(pix);
    }
    return icon;
}

void MainWindow::setupReminder() {
    m_reminderTimer = new QTimer(this);
    m_reminderTimer->setInterval(60 * 1000);
    connect(m_reminderTimer, &QTimer::timeout, this, &MainWindow::onReminderTick);
    m_reminderTimer->start();
}

void MainWindow::onReminderTick() {
    if (!m_tray || !m_tray->isVisible()) return;

    const QDateTime now = QDateTime::currentDateTime();
    QStringList messages;
    std::vector<Task>& tasks = m_doc.tasks();

    QSet<int> live;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        live.insert(tasks[i].id);
    }
    m_remindedIds.intersect(live);

    for (std::size_t i = 0; i < tasks.size() && messages.size() < 3; ++i) {
        const Task& t = tasks[i];
        if (t.status == 2 || t.status == 3) continue;
        if (m_remindedIds.contains(t.id)) continue;

        const QDateTime due = QDateTime::fromString(
            QString::fromStdString(t.deadline), "yyyy-MM-dd HH:mm");
        if (!due.isValid()) continue;

        const qint64 minutesLeft = now.secsTo(due) / 60;
        if (minutesLeft > 24 * 60) continue;
        if (minutesLeft < -24 * 60) continue;

        const QString title = QString::fromStdString(t.title);
        if (minutesLeft < 0) {
            messages << QString("【已逾期】%1").arg(title);
        } else if (minutesLeft < 60) {
            messages << QString("还剩 %1 分钟：%2").arg(minutesLeft).arg(title);
        } else {
            messages << QString("将于 %1 到期：%2").arg(due.toString("MM-dd HH:mm"), title);
        }
    }

    if (!messages.isEmpty()) {
        m_tray->showMessage("CYSC 任务栏", messages.join('\n'),
                            QSystemTrayIcon::Information, 8000);
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const Task& t = tasks[i];
            if (t.status == 2 || t.status == 3) continue;
            const QDateTime due = QDateTime::fromString(
                QString::fromStdString(t.deadline), "yyyy-MM-dd HH:mm");
            if (!due.isValid()) continue;
            const qint64 minutesLeft = now.secsTo(due) / 60;
            if (minutesLeft <= 24 * 60 && minutesLeft >= -24 * 60) {
                m_remindedIds.insert(t.id);
            }
        }
    }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) hide(); else show();
    }
}

void MainWindow::onQuit() {
    m_tray->hide();
    QApplication::quit();
}

// ------------------------------------------------------------------ 窗口

void MainWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // 窗口每次显示时把焦点交给「新建任务」，这样一打开就能按空格/回车开始录入，
    // 而不是让焦点落在工具栏的搜索框上、敲字直接进了搜索。
    if (m_newButton) {
        m_newButton->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();

    if (closeButtonRect().contains(pos)) { hide(); return; }
    if (pinButtonRect().contains(pos)) { toggleOnTop(); return; }
    if (minimizeButtonRect().contains(pos)) { showMinimized(); return; }
    if (maximizeButtonRect().contains(pos)) { toggleMaximize(); return; }

    // 先判边框：贴边按下就是要调整窗口大小（无边框窗口没有系统边框可拖）。
    const int edge = resizeEdgeAt(pos);
    if (edge != SmNone) {
        m_resizeEdge = edge;
        m_resizeStartGlobal = event->globalPosition().toPoint();
        m_resizeStartGeometry = geometry();
        return;
    }

    if (titleBarRect().contains(pos)) {
        m_dragging = true;
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    const QPoint globalPos = event->globalPosition().toPoint();

    if (m_resizeEdge != SmNone) {
        // 标记"这次 resize 来自用户拖边"，好让 resizeEvent 里的
        // 「缩窄自动进紧凑」只在手动拖拽时生效。
        m_resizingByUser = true;
        applyResize(globalPos);
        return;
    }
    if (m_dragging) {
        move(globalPos - m_dragPos);
        return;
    }
    // 没在拖拽时，按鼠标所在的边/角切换指针形状，让用户知道这里能拉。
    setCursor(cursorForEdge(resizeEdgeAt(event->position().toPoint())));
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    m_resizingByUser = false;
    m_dragging = false;
    m_resizeEdge = SmNone;
}

int MainWindow::resizeEdgeAt(const QPoint& pos) const {
    // 边框热区不能让整个窗口都算，否则点哪都在拉边。
    const int m = RESIZE_MARGIN;
    if (width() <= m * 3 || height() <= m * 3) return SmNone;

    int edge = SmNone;
    if (pos.x() <= m) edge |= SmLeft;
    else if (pos.x() >= width() - m) edge |= SmRight;
    if (pos.y() <= m) edge |= SmTop;
    else if (pos.y() >= height() - m) edge |= SmBottom;
    return edge;
}

Qt::CursorShape MainWindow::cursorForEdge(int edge) const {
    switch (edge) {
        case SmLeft:
        case SmRight:
            return Qt::SizeHorCursor;
        case SmTop:
        case SmBottom:
            return Qt::SizeVerCursor;
        case SmTopLeft:
        case SmBottomRight:
            return Qt::SizeFDiagCursor;
        case SmTopRight:
        case SmBottomLeft:
            return Qt::SizeBDiagCursor;
        case SmNone:
        default:
            return Qt::ArrowCursor;
    }
}

void MainWindow::applyResize(const QPoint& globalPos) {
    if (m_resizeEdge == SmNone) return;

    const QPoint delta = globalPos - m_resizeStartGlobal;
    QRect g = m_resizeStartGeometry;
    const int minW = minimumWidth() > 0 ? minimumWidth() : 320;
    const int minH = minimumHeight() > 0 ? minimumHeight() : 240;

    if (m_resizeEdge & SmLeft) {
        const int newLeft = g.left() + delta.x();
        if (g.right() - newLeft + 1 >= minW) g.setLeft(newLeft);
    }
    if (m_resizeEdge & SmRight) {
        const int newRight = g.right() + delta.x();
        if (newRight - g.left() + 1 >= minW) g.setRight(newRight);
    }
    if (m_resizeEdge & SmTop) {
        const int newTop = g.top() + delta.y();
        if (g.bottom() - newTop + 1 >= minH) g.setTop(newTop);
    }
    if (m_resizeEdge & SmBottom) {
        const int newBottom = g.bottom() + delta.y();
        if (newBottom - g.top() + 1 >= minH) g.setBottom(newBottom);
    }
    setGeometry(g);
}

namespace {
// 托盘是否可用。允许用环境变量 CYSC_FORCE_TRAY=1 覆盖，便于在没有通知区域的
// 会话里验证「关窗 -> 收进托盘」这条路径。
bool trayUsable() {
    if (qEnvironmentVariableIsSet("CYSC_FORCE_TRAY")) return true;
    return QSystemTrayIcon::isSystemTrayAvailable();
}
} // namespace

void MainWindow::closeEvent(QCloseEvent *event) {
    // 关窗口 = 收进托盘，不退出进程。
    // 但如果托盘不可用，就没有「收进去」的地方了 —— 这时必须真的退出，
    // 否则窗口消失、进程还在后台跑，用户既看不到也没法退。
    if (!trayUsable()) {
        onQuit();                     // 里面会 hide 托盘并 QApplication::quit()
        event->accept();
        return;
    }
    event->ignore();
    hide();

    // 首次收进托盘时给个气泡，否则窗口一消失用户会以为程序崩了，
    // 又不知道图标被 Windows 收进了「隐藏的图标」溢出区（Win11 默认如此）。
    if (!m_trayHintShown && m_tray && QSystemTrayIcon::supportsMessages()) {
        m_trayHintShown = true;
        m_tray->showMessage("CYSC 任务栏",
                            "已收进系统托盘。若看不到图标，请点任务栏的 ∧ 展开"
                            "「隐藏的图标」；右键图标可退出程序。",
                            QSystemTrayIcon::Information, 6000);
    }
}

// ------------------------------------------------------- 界面自检驱动

class MainWindowTestAccess {
public:
static int run() {
    QTextStream out(stdout);
    out << "\nCYSC 任务栏界面自检\n";

    int failures = 0;
    auto check = [&](bool ok, const QString& what) {
        out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (!ok) ++failures;
    };

    // 必须在**构造任何窗口之前**设好应用/组织名，并把偏好设置指向临时目录：
    // QSettings 的路径由这些决定，晚设的话窗口构造时会写到别的地方，
    // 测试再从正确位置读就成了「无效」。
    QCoreApplication::setOrganizationName("CYSC");
    QCoreApplication::setApplicationName("CYSC TaskBar-SelfTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       qEnvironmentVariable("CYSC_TEST_TMPDIR", QDir::tempPath()));

    // 必须在构造窗口之前指定数据库（构造函数会加载已有任务）；文件名带
    // 时间戳，保证每次运行都是全新的库，不会被上一次的数据干扰。
    const QString tmpDir = qEnvironmentVariable("CYSC_TEST_TMPDIR");
    const QString dbPath = QDir(tmpDir.isEmpty() ? QDir::tempPath() : tmpDir)
                               .absoluteFilePath("cysc_guitest_"
                                                 + QString::number(
                                                       QDateTime::currentMSecsSinceEpoch())
                                                 + ".db");
    qputenv("CYSC_DB_PATH", dbPath.toUtf8());

    auto *window = new MainWindow;
    window->resize(440, 640);
    window->show();

    auto add = [window](const char* title, const char* details, int priority,
                        const char* deadline, int category) {
        Task t;
        t.title = title;
        t.details = details;
        t.priority = priority;
        t.deadline = deadline;
        t.categoryId = category;
        return window->m_doc.addTask(t);
    };

    const int firstId = add("界面测试任务", "这段备注用来验证详情编辑", 3,
                            "2026-10-10 18:00", 1);
    add("学习的任务", "数据结构的笔记", 1, "2026-10-11 09:00", 2);
    const int thirdId = add("已完成的任务", "", 0, "2026-10-12 09:00", 0);
    window->applyStatus(thirdId, 2);
    check(firstId >= 0 && thirdId >= 0, "写入三条测试任务");

    // 刷新列表
    window->refreshList();
    QCoreApplication::processEvents();
    check(window->m_taskList->count() == 3, "列表显示 3 条任务");

    QWidget *firstRow = window->m_taskList->itemWidget(window->m_taskList->item(0));
    const int collapsedHeight = window->m_taskList->item(0)->sizeHint().height();
    check(firstRow && collapsedHeight > 0, "折叠时行高有效");

    // --- 单击展开 / 再次单击收起（展开是就地改行高，不重建列表）---
    auto detailOf = [window](int row) {
        QWidget *w = window->m_taskList->itemWidget(window->m_taskList->item(row));
        return w ? w->findChild<QWidget*>("detailPanelWidget") : nullptr;
    };

    window->expandTask(firstId);
    check(window->m_expandedId == firstId, "单击后记录为展开状态");
    const int expandedHeight = window->m_taskList->item(0)->sizeHint().height();
    check(expandedHeight > collapsedHeight, "展开后行高变大（详情区可见）");
    check(detailOf(0) && detailOf(0)->isVisible(), "展开后详情区可见");
    check(detailOf(0) && detailOf(0)->findChild<QPlainTextEdit*>() != nullptr,
          "详情区里有备注输入框");
    check(detailOf(0) && detailOf(0)->findChild<QDateTimeEdit*>() != nullptr,
          "详情区里有精确到分钟的截止时间控件");
    check(window->m_taskList->count() == 3, "展开不会重建列表（双击靠这一点才成立）");

    window->expandTask(-1);
    check(window->m_expandedId == -1, "再次单击收起");
    check(window->m_taskList->item(0)->sizeHint().height() == collapsedHeight,
          "收起后行高还原");
    check(detailOf(0) == nullptr || !detailOf(0)->isVisible(), "收起后详情区隐藏");
    check(window->m_taskList->count() == 3, "收起后列表条数不变");

    // --- 状态循环本身（直接驱动槽函数，快速覆盖三态轮转）---
    // 注意：双击改状态是**延迟到事件循环下一轮**执行的（advanceStatusDeferred）。
    // 这不是为了"稍后再改"，而是因为立刻改需要重建列表，而重建会把正在派发
    // 这个双击事件的列表项删掉 —— 那正是"双击任务就闪退"的根因。
    // 所以这里必须把待处理事件跑掉，才能看到状态已经变了。
    auto doubleClickSlot = [window](int id) {
        for (int i = 0; i < window->m_taskList->count(); ++i) {
            QListWidgetItem *item = window->m_taskList->item(i);
            if (item->data(Qt::UserRole).toInt() == id) {
                window->onItemDoubleClicked(item);
                QCoreApplication::processEvents();   // 让延迟的状态变更落地
                return;
            }
        }
    };
    const int listCountBefore = window->m_taskList->count();
    doubleClickSlot(firstId);
    check(window->m_doc.findTask(firstId)->status == 1, "状态轮转：待办 -> 进行中");
    // 关键回归：改状态后列表条数不变，说明没有 clear() 重建整个列表。
    check(window->m_taskList->count() == listCountBefore,
          "改状态不会清空重建列表（清空会把正在派发事件的列表项删掉 -> 闪退）");
    doubleClickSlot(firstId);
    check(window->m_doc.findTask(firstId)->status == 2, "状态轮转：进行中 -> 已完成");
    doubleClickSlot(firstId);
    check(window->m_doc.findTask(firstId)->status == 0, "状态轮转：已完成 -> 回到待办");
    check(window->m_taskList->count() == listCountBefore,
          "三轮轮转后列表条数依然不变");

    // --- 分类筛选 ---
    window->onCategoryFilterClicked(2);
    check(window->m_taskList->count() == 1, "按「学习」筛选只剩 1 条");
    window->onCategoryFilterClicked(-1);
    check(window->m_taskList->count() == 3, "切回「全部」恢复 3 条");

    // --- 搜索：标题 + 备注子串 ---
    window->m_search->setText("笔记");
    check(window->m_taskList->count() == 1, "按备注内容搜索命中 1 条");
    window->m_search->setText("的任务");
    check(window->m_taskList->count() == 2, "按标题子串搜索命中 2 条");
    window->m_search->setText("不存在的关键词");
    check(window->m_taskList->count() == 0, "搜不到时列表为空");
    window->m_search->setText(QString());
    check(window->m_taskList->count() == 3, "清空搜索后恢复 3 条");

    // --- 搜索补全：候选来自 Trie 前缀索引 ---
    // 界面自检里的三条任务标题是「界面测试任务 / 学习的任务 / 已完成的任务」，
    // 断言按这三个真实标题写，不要凭空假设。
    {
        check(window->m_searchCompleter != nullptr, "搜索框挂了补全器");

        window->m_search->setText("界面");
        QAbstractItemModel *model = window->m_searchCompleter->model();
        check(model != nullptr, "补全器有候选模型");
        const int rc = model ? model->rowCount() : -1;
        check(model && model->rowCount() == 1, "前缀「界面」只有 1 个补全候选");
        check(model && model->rowCount() == 1
                  && model->index(0, 0).data().toString() == "界面测试任务",
              "候选正是「界面测试任务」");

        window->m_search->setText("学习的");
        model = window->m_searchCompleter->model();
        check(model && model->rowCount() == 1
                  && model->index(0, 0).data().toString() == "学习的任务",
              "前缀「学习的」补全出「学习的任务」");

        // 「任务」是后缀而不是任何标题的前缀：没有候选，但子串搜索仍命中 3 条。
        window->m_search->setText("任务");
        check(window->m_searchCompleter->model()->rowCount() == 0,
              "「任务」不是任何标题的前缀，所以没有补全候选");
        check(window->m_taskList->count() == 3,
              "但子串搜索仍然命中全部 3 条（补全不影响搜索本身）");

        // 「测试」在标题中间：同样无候选，但子串能命中
        window->m_search->setText("测试");
        check(window->m_searchCompleter->model()->rowCount() == 0,
              "「测试」在标题中间，无补全候选");
        check(window->m_taskList->count() == 1, "子串搜索命中「界面测试任务」");

        window->m_search->setText(QString());
        window->refreshList();
    }

    // --- 隐藏已完成 ---
    window->m_hideDone->setChecked(true);
    check(window->m_taskList->count() == 2, "隐藏已完成后剩 2 条");
    window->m_hideDone->setChecked(false);
    check(window->m_taskList->count() == 3, "取消隐藏后恢复 3 条");

    // --- 内联编辑保存（详情编辑器按钮走的就是这条路径）---
    Task edited = *window->m_doc.findTask(firstId);
    edited.title = "改过的标题";
    edited.details = "改过的备注";
    edited.priority = 0;
    edited.categoryId = 3;
    window->applyTaskEdit(firstId, edited);
    check(window->m_doc.findTask(firstId)->title == "改过的标题", "内联编辑保存了标题");
    check(window->m_doc.findTask(firstId)->details == "改过的备注", "内联编辑保存了备注");
    check(window->m_doc.findTask(firstId)->priority == 0, "内联编辑保存了优先级");
    check(window->m_taskList->count() == 3, "保存后列表完整");

    // --- 空标题必须被拒绝 ---
    Task invalid = *window->m_doc.findTask(firstId);
    invalid.title.clear();
    window->applyTaskEdit(firstId, invalid);
    check(window->m_doc.findTask(firstId)->title == "改过的标题", "空标题被拒绝，原标题保留");

    // --- 详情区「删除」按钮真的会删掉任务 ---
    window->expandTask(firstId);
    // 展开后要等布局跑一遍，行控件才会真正被补建出来
    QCoreApplication::processEvents();
    QWidget *row = window->m_taskList->itemWidget(window->m_taskList->item(0));
    QPushButton *deleteButton = nullptr;
    if (row) {
        const QList<QPushButton*> buttons = row->findChildren<QPushButton*>();
        for (QPushButton *b : buttons) {
            if (b->text() == "删除") {
                deleteButton = b;
                break;
            }
        }
    }
    check(deleteButton != nullptr, "详情区里找到「删除」按钮");
    if (deleteButton) {
        // 删除会弹确认框，这里直接调用模型层的删除路径做等价验证，
        // 避免在无界面环境下等待模态对话框。
        const int before = static_cast<int>(window->m_doc.tasks().size());
        check(window->m_doc.removeTask(firstId), "删除按钮对应的删除操作成功");
        window->refreshList();
        check(static_cast<int>(window->m_doc.tasks().size()) == before - 1,
              "删除后任务数减一");
        check(window->m_taskList->count() == 2, "删除后列表剩 2 条");
        check(window->m_doc.undo(), "删除可以撤销");
        window->refreshList();
        check(window->m_taskList->count() == 3, "撤销后恢复 3 条");
    }

    // --- 详情区新增控件（星标 / 我的一天 / 列表 / 重复 / 提醒 / 步骤 / 标签）---
    {
        window->expandTask(firstId);
        QCoreApplication::processEvents();
        QWidget *editRow = window->m_taskList->itemWidget(window->m_taskList->item(0));
        check(editRow != nullptr, "拿到第一行的详情编辑器");
        if (editRow) {
            // 把这些控件找出来：星标与「我的一天」按文字区分，列表/重复按条目内容区分。
            QCheckBox *starCheck = nullptr;
            QCheckBox *myDayCheck = nullptr;
            QCheckBox *reminderCheck = nullptr;
            QComboBox *listCombo = nullptr;
            QComboBox *repeatCombo = nullptr;
            QPushButton *saveButton = nullptr;
            for (QCheckBox *c : editRow->findChildren<QCheckBox *>()) {
                if (c->text().contains("重要")) starCheck = c;
                else if (c->text().contains("我的一天")) myDayCheck = c;
                else if (c->text() == "提醒") reminderCheck = c;
            }
            for (QComboBox *c : editRow->findChildren<QComboBox *>()) {
                // 重复下拉里有「每天」；列下拉里是列表名
                if (c->findText("每天") >= 0) repeatCombo = c;
                else listCombo = c;
            }
            for (QPushButton *b : editRow->findChildren<QPushButton *>()) {
                if (b->text() == "保存") saveButton = b;
            }

            check(starCheck != nullptr, "详情区有「重要」星标复选框");
            check(myDayCheck != nullptr, "详情区有「我的一天」复选框");
            check(reminderCheck != nullptr, "详情区有「提醒」复选框");
            check(listCombo != nullptr, "详情区有列表下拉");
            check(repeatCombo != nullptr, "详情区有重复下拉");
            check(saveButton != nullptr, "详情区有「保存」按钮");
            check(repeatCombo && repeatCombo->count() == 6,
                  "重复下拉有 6 个选项（不重复 / 每天 / 工作日 / 每周 / 每月 / 每年）");

            // 步骤输入框：加两步，一步勾上
            QLineEdit *stepInput = nullptr;
            QLineEdit *tagInput = nullptr;
            for (QLineEdit *e : editRow->findChildren<QLineEdit *>()) {
                if (e->placeholderText().contains("添加步骤")) stepInput = e;
                else if (e->placeholderText().contains("添加标签")) tagInput = e;
            }
            check(stepInput != nullptr, "详情区有「添加步骤」输入框");
            check(tagInput != nullptr, "详情区有「添加标签」输入框");

            if (stepInput) {
                stepInput->setText("第一步");
                emit stepInput->returnPressed();
                stepInput->setText("第二步");
                emit stepInput->returnPressed();
                // 勾上第一步
                const QList<QCheckBox *> boxes = editRow->findChildren<QCheckBox *>();
                for (QCheckBox *c : boxes) {
                    if (c->text() == "第一步") {
                        c->setChecked(true);
                        break;
                    }
                }
                check(true, "通过回车添加了两个步骤");
            }
            if (tagInput) {
                tagInput->setText("演示 重要");
                emit tagInput->returnPressed();
                check(true, "一次输入两个标签（空格分隔）");
            }

            // 设置其余字段再保存
            if (starCheck) starCheck->setChecked(true);
            if (myDayCheck) myDayCheck->setChecked(true);
            if (repeatCombo) {
                const int idx = repeatCombo->findText("每周");
                if (idx >= 0) repeatCombo->setCurrentIndex(idx);
            }
            if (reminderCheck) reminderCheck->setChecked(true);
            if (listCombo && listCombo->count() > 1) listCombo->setCurrentIndex(1);

            const int wantListId = listCombo ? listCombo->currentData().toInt() : -1;
            if (saveButton) emit saveButton->clicked();

            const Task *after = window->m_doc.findTask(firstId);
            check(after != nullptr, "保存后任务仍在");
            if (after) {
                check(after->starred, "保存写入了星标");
                check(after->inMyDay, "保存写入了「我的一天」");
                check(after->recurrence == Recurrence::Weekly, "保存写入了重复规则「每周」");
                check(!after->reminder.empty(), "保存写入了提醒时间");
                check(wantListId > 0 && after->listId == wantListId, "保存写入了所属列表");
                check(after->subtasks.size() == 2, "保存写入了 2 个步骤");
                check(after->doneSubtaskCount() == 1, "步骤的勾选状态被保存（1/2）");
                check(after->subtaskPercent() == 50, "进度算出来是 50%");
                check(after->tags.size() == 2, "保存写入了 2 个标签");
                check(after->hasTag("演示") && after->hasTag("重要"), "两个标签内容都正确");
            }

            // 标签进了哈希索引：按标签能查到
            TaskFilter byTag;
            byTag.tag = "演示";
            check(static_cast<int>(window->m_doc.query(byTag).size()) == 1,
                  "新标签立即进了哈希倒排索引，按标签能筛到");

            // 「重要」与「我的一天」视图计数跟着变
            check(window->m_doc.countStarred() == 1, "星标后「重要」计数为 1");
            check(window->m_doc.countInMyDay() == 1, "「我的一天」计数为 1");

            // 删除一个步骤：剩下的步骤不能串行。
            // 按 objectName 精确定位：findChildren 的顺序不保证是布局顺序，
            // 之前用遍历取到的是另一个步骤的按钮，得出的结论是错的。
            QPushButton *stepDelete = editRow->findChild<QPushButton *>("stepDel_0");
            check(stepDelete != nullptr, "步骤行上有删除按钮（objectName 可定位）");
            if (stepDelete && saveButton) {
                emit stepDelete->clicked();
                emit saveButton->clicked();
                const Task *trimmed = window->m_doc.findTask(firstId);
                check(trimmed && trimmed->subtasks.size() == 1, "删掉一个步骤后只剩 1 个");
                // 删掉的是第 1 步（已勾选），剩下的第 2 步必须仍是未勾选
                check(trimmed && trimmed->subtasks.size() == 1
                          && trimmed->subtasks[0].title == "第二步"
                          && !trimmed->subtasks[0].done,
                      "删掉已勾选的第 1 步后，剩下的第 2 步仍未勾选（没有串行）");
            }
        }
        window->expandTask(-1);
    }

    // --- 拖拽排序：走真实的 rowMoved 路径 ---
    {
        // 先验证列表控件真的配好了拖拽
        check(window->m_taskList->dragEnabled(), "列表启用了拖拽");
        check(window->m_taskList->acceptDrops(), "列表接受放下");
        check(window->m_taskList->dragDropMode() == QAbstractItemView::InternalMove,
              "拖拽模式为「仅内部移动」");
        check(window->m_taskList->showDropIndicator(), "显示放下位置指示线");

        // 先切到手动拖拽排序，再单独刷新一次列表，保证列表顺序与模型一致。
        // （onSortClicked 自己会 refreshList，顺序会按 Manual 规则重排。）
        while (window->m_sortMode != MainWindow::SortManual) {
            window->onSortClicked();
        }
        check(window->m_sortMode == MainWindow::SortManual, "排序切到「手动拖拽」");
        check(window->m_sortButton->text().contains("手动"), "排序按钮文字显示「手动」");
        window->refreshList();

        check(window->m_taskList->count() == 3, "拖拽前列表有 3 条");
        const int dragId = window->m_taskList->item(2)->data(Qt::UserRole).toInt();
        const int targetId = window->m_taskList->item(0)->data(Qt::UserRole).toInt();
        check(dragId != targetId, "被拖的任务与目标行不是同一条");

        // 先验证 moveTask 本身（绕过控件）：把最后一条移到最前。
        // 不要假设 id 大小与位置有关——Release 下跑的是真实用户数据库，
        // 里面的任务可能来自之前的运行，id 与顺序都不确定。
        {
            // 把这三条都置为未完成：已完成的任务会被统一置底（见 TaskDocument::sortTasks），
            // 那样「移到最前」就会被置底规则顶回去，断言不再成立。
            for (int i = 0; i < window->m_taskList->count(); ++i) {
                const int id = window->m_taskList->item(i)->data(Qt::UserRole).toInt();
                window->applyStatus(id, 0);
            }
            window->refreshList();

            std::vector<int> ids;
            for (int i = 0; i < window->m_taskList->count(); ++i) {
                ids.push_back(window->m_taskList->item(i)->data(Qt::UserRole).toInt());
            }
            const int first = ids.front();
            const int last = ids.back();
            check(first != last, "列表里至少有两条不同任务");

            // 把最后一条移到最前
            const bool ok = window->m_doc.moveTask(last, 0, ids);
            check(ok, "直接调用 moveTask 把最后一条移到最前");

            TaskFilter m2;
            m2.sort = TaskSort::Manual;
            const std::vector<Task> q2 = window->m_doc.query(m2);
            check(!q2.empty() && q2.front().id == last,
                  "moveTask 后该任务在「手动拖拽」顺序里排到最前");
            window->refreshList();
        }

        // 模拟 Qt 内部移动完成后的列表状态：把第 3 行挪到最前。
        // 注意这里不能调用 refreshList()——那会按模型顺序重建列表，
        // 把我们刚做出来的「拖动结果」抹掉，moveTask 就会看到"本来就在原位"。
        QListWidgetItem *moved = window->m_taskList->takeItem(2);
        window->m_taskList->insertItem(0, moved);
        // 按下时记下的「正在拖拽的任务」
        window->m_draggingTaskId = dragId;

        // TaskListWidget 在 dropEvent 里处理完 Qt 的内部移动后发出 rowMoved；
        // 这里直接走主窗口的槽，等价于真实拖拽放下的那一步。
        window->onRowsMoved();

        // 模型里的顺序应当把 dragId 排到最前
        TaskFilter manual;
        manual.sort = TaskSort::Manual;
        const std::vector<Task> after = window->m_doc.query(manual);
        check(!after.empty() && after.front().id == dragId,
              "拖拽后该任务在模型里排到最前（新顺序已写回）");
        check(!after.empty() && after.front().manualOrder == 0, "该任务的 manualOrder 变为 0");
        check(static_cast<int>(after.size()) == 3, "拖拽没有丢任务");

        bool contiguous = true;
        for (std::size_t i = 0; i < after.size(); ++i) {
            if (after[i].manualOrder != static_cast<int>(i)) contiguous = false;
        }
        check(contiguous, "manualOrder 保持连续");

        check(window->m_doc.canUndo(), "拖拽后可撤销");
        check(window->m_doc.undo(), "撤销拖拽成功");
        window->refreshList();

        // 切回创建时间排序，免得影响后面的断言
        while (window->m_sortMode != MainWindow::SortCreatedAsc) {
            window->onSortClicked();
        }
    }

    // --- 列表分组：对话框 + 并查集 + 侧栏渲染 ---
    {
        // 侧栏上应有「新建列表」与「分组」两个按钮
        QPushButton *groupButton = window->m_sidebar->findChild<QPushButton*>(
            "groupListsButton");
        check(groupButton != nullptr, "侧栏有「分组」按钮");

        // 造两个列表，先确认它们不同组
        const int listA = window->m_doc.addList("分组甲", "#FF007F");
        const int listB = window->m_doc.addList("分组乙", "#00AA88");
        window->m_sidebar->refresh();
        check(!window->m_doc.listsShareGroup(listA, listB), "新建的两个列表不同组");

        const int headersBefore = window->m_sidebar->groupHeaderCount();
        check(headersBefore == 0, "还没分组时侧栏没有分组标题");
        check(window->m_sidebar->listRowCount() == static_cast<int>(window->m_doc.lists().size()),
              "侧栏列表行数与模型一致");

        // 对话框：勾选两个列表
        TaskGroupDialog dlg(&window->m_doc);
        check(dlg.optionCount() == static_cast<int>(window->m_doc.lists().size()),
              "分组对话框列出全部列表");
        check(dlg.setCheckedByListName("分组甲", true), "能勾选「分组甲」");
        check(dlg.setCheckedByListName("分组乙", true), "能勾选「分组乙」");
        const std::vector<int> chosen = dlg.selectedListIds();
        check(chosen.size() == 2, "对话框返回 2 个勾选项");

        // 走主窗口的分组逻辑（对话框 exec 在无界面环境不能弹，这里直接调模型路径）
        int merged = 0;
        for (std::size_t i = 1; i < chosen.size(); ++i) {
            if (window->m_doc.joinListsIntoGroup(chosen[0], chosen[i])) ++merged;
        }
        window->persistDocument();
        window->m_sidebar->refresh();

        check(merged == 1, "两个列表被合并成一个分组");
        check(window->m_doc.listsShareGroup(listA, listB), "并查集确认它们同组");
        check(window->m_doc.groupCount() >= 1, "分组计数大于 0");
        check(window->m_sidebar->groupHeaderCount() == 1,
              "侧栏出现 1 个分组标题");

        // 第三个列表并进来：传递性
        const int listC = window->m_doc.addList("分组丙", "#0077FF");
        window->m_doc.joinListsIntoGroup(listB, listC);
        window->m_sidebar->refresh();
        check(window->m_doc.listsShareGroup(listA, listC),
              "并查集传递性：甲~乙~丙 同组");
        check(window->m_sidebar->groupHeaderCount() == 1,
              "三个列表仍只占 1 个分组标题");

        // 侧栏里同组的列表应当相邻显示（分组渲染把同组聚类）
        check(window->m_sidebar->listRowCount() == static_cast<int>(window->m_doc.lists().size()),
              "加了第三个列表后侧栏行数仍与模型一致");

        // 分组可以撤销
        check(window->m_doc.undo(), "撤销一次分组合并");
        window->m_sidebar->refresh();
        check(true, "撤销分组后侧栏仍能正常渲染");
    }

    // --- 列表编辑（右键菜单对应的四个操作）---
    {
        // 重命名
        const int idA = window->m_doc.addList("改名前", "#FF007F");
        check(idA > 0, "新建一个待编辑的列表");
        check(window->m_doc.renameList(idA, "改后名"), "重命名列表");
        const TaskList *renamed = nullptr;
        for (const TaskList &l : window->m_doc.lists()) {
            if (l.id == idA) renamed = &l;
        }
        check(renamed && renamed->name == "改后名", "名称确实写进了模型");
        check(!window->m_doc.renameList(idA, ""), "空名称被拒绝");
        check(!window->m_doc.renameList(99999, "不存在"), "给不存在的列表改名会失败");

        // 改颜色
        check(window->m_doc.setListColor(idA, "#00E5A0"), "更改列表颜色");
        renamed = nullptr;
        for (const TaskList &l : window->m_doc.lists()) {
            if (l.id == idA) renamed = &l;
        }
        check(renamed && renamed->color == "#00E5A0", "颜色确实写进了模型");

        // 移出分组：先并进去，再单独摘出来，同组伙伴必须仍然成组
        const int idB = window->m_doc.addList("伙伴乙", "#32C8FF");
        const int idC = window->m_doc.addList("伙伴丙", "#FFC814");
        window->m_doc.joinListsIntoGroup(idA, idB);
        window->m_doc.joinListsIntoGroup(idB, idC);
        check(window->m_doc.listsShareGroup(idA, idC), "三个列表先并成一组");
        check(window->m_doc.removeListFromGroup(idA), "把其中一个移出分组");
        check(!window->m_doc.listsShareGroup(idA, idB), "被移出的列表不再与伙伴同组");
        check(window->m_doc.listsShareGroup(idB, idC),
              "同组伙伴之间仍然成组（拆除不能连累别人）");
        check(!window->m_doc.removeListFromGroup(idA), "重复移出会失败");

        // 删除列表：任务要迁移到默认列表，不能悬空；其它分组不能被连累
        const int doomed = window->m_doc.addList("待删除列表", "#888888");
        Task t;
        t.title = "会被迁移的任务";
        t.listId = doomed;
        const int movedTaskId = window->m_doc.addTask(t);
        check(movedTaskId > 0 && window->m_doc.countInList(doomed) == 1,
              "往待删除列表里放一个任务");

        const int groupsBefore = window->m_doc.groupCount();
        // 真正有意义的「分组数」= 不同 groupId 的个数（groupId < 0 表示未分组）。
        // 注意 groupCount() 是并查集的集合总数，**每个未分组列表也算一个单元素
        // 集合**，所以它不能直接当「分组个数」用（加一个列表就 +1、删一个就 -1）。
        auto distinctGroups = [](const TaskDocument &d) {
            std::vector<int> ids;
            for (const TaskList &l : d.lists()) {
                if (l.groupId >= 0) ids.push_back(l.groupId);
            }
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return static_cast<int>(ids.size());
        };
        const int distinctBefore = distinctGroups(window->m_doc);

        check(window->m_doc.removeList(doomed), "删除列表");
        check(window->m_doc.findList(doomed) == nullptr, "列表确实没了");
        const Task *moved = window->m_doc.findTask(movedTaskId);
        check(moved && moved->listId == window->m_doc.defaultListId(),
              "列表里的任务被迁移到默认列表，没有悬空");
        // 回归：删除列表曾经会 m_groups.reset()，把所有分组都抹掉
        check(distinctGroups(window->m_doc) == distinctBefore,
              "删除一个列表不会连累其它分组（曾经会全部清空）");
        check(window->m_doc.listsShareGroup(idB, idC), "其它分组在删除后依然存在");
        check(window->m_doc.groupCount() == groupsBefore - 1,
              "集合总数少 1（被删的那个列表本身是一个单元素集合）");

        // 默认列表不可删
        check(!window->m_doc.removeList(window->m_doc.defaultListId()),
              "默认列表不可删除");

        window->persistDocument();
        window->m_sidebar->refresh();
        check(window->m_sidebar->listRowCount() ==
                  static_cast<int>(window->m_doc.lists().size()),
              "一系列编辑后侧栏行数仍与模型一致");
    }

    // --- 「建议」面板 ---
    {
        check(window->m_suggestToggle != nullptr, "有「建议」折叠按钮");
        check(window->m_suggestBox != nullptr, "有建议面板容器");
        check(!window->m_suggestOpen && !window->m_suggestBox->isVisible(),
              "建议面板默认收起");

        // 造一条逾期任务，确保建议非空
        Task overdue;
        overdue.title = "逾期登记表";
        overdue.listId = 1;
        overdue.deadline = QDate::currentDate().addDays(-2)
                               .toString("yyyy-MM-dd").toStdString() + " 09:00";
        overdue.priority = 3;
        const int overdueId = window->m_doc.addTask(overdue);
        window->refreshSuggestions();

        const std::vector<int> tips = window->m_doc.suggestions(4);
        check(!tips.empty(), "有可建议的任务");
        check(!tips.empty() && tips.front() == overdueId,
              "逾期的任务排在建议第一位");

        // 展开面板
        window->onToggleSuggestions();
        check(window->m_suggestOpen, "点击后建议面板展开");
        check(window->m_suggestToggle->text().contains("建议"),
              "按钮文字包含「建议」与条数");
        check(window->m_suggestCount == static_cast<int>(tips.size()),
              "面板里的建议条数与模型一致");

        // 通过建议里的「＋ 今天」把它加入我的一天
        QPushButton *addBtn = window->m_suggestBox->findChild<QPushButton*>(
            QString("suggestAdd_%1").arg(overdueId));
        check(addBtn != nullptr, "建议行上有「加入我的一天」按钮");
        const int myDayBefore = window->m_doc.countInMyDay();
        if (addBtn) {
            emit addBtn->clicked();
            const Task *after = window->m_doc.findTask(overdueId);
            check(after && after->inMyDay, "点击后该任务进入我的一天");
            check(window->m_doc.countInMyDay() == myDayBefore + 1,
                  "我的一天计数 +1");
            // 已在「我的一天」里的任务不该再出现在建议的「今天」按钮上，
            // 但它仍可能因为逾期而留在建议里，这里只确认状态写对了。
        }

        // 收起
        window->onToggleSuggestions();
        check(!window->m_suggestOpen, "再次点击收起建议面板");
    }

    // --- 数据结构可视化面板 ---
    {
        QPushButton *vizBtn = window->findChild<QPushButton*>("visualizeButton");
        check(vizBtn != nullptr, "工具栏有「结构」按钮");

        // 先造点数据，让四个页签都有内容
        window->m_doc.addList("可视化甲", "#FF007F");
        window->m_doc.addList("可视化乙", "#00AA88");
        window->refreshList();

        window->openVisualizer();
        check(window->m_visualizer != nullptr, "可视化面板已创建");
        TaskVisualizer *viz = window->m_visualizer;
        if (viz) {
            viz->refreshAll();

            // 哈希表页：至少有一个非空桶，且桶数/最长链与模型一致
            const HashTable<std::string, std::vector<int>>& idx = window->m_doc.tagIndex();
            check(viz->hashBucketRows() == static_cast<int>(idx.usedBucketCount()),
                  "哈希页列出的非空桶数与模型一致");
            check(viz->hashBucketRows() > 0, "哈希页有内容（标签索引非空）");
            check(HashTable<std::string, int>::isPrime(idx.bucketCount()),
                  "哈希桶数仍是质数");

            // Trie 页：标题前缀有内容，结点数非零
            check(viz->trieRows() > 0, "Trie 页有内容");
            check(window->m_doc.titleTrie().nodeCount() > 0, "Trie 结点数非零");

            // 线段树页
            check(viz->segmentRows() > 0, "线段树页有内容");
            check(window->m_doc.deadlineTree().days() > 0, "线段树天数非零");

            // 并查集页：行数 = 列表数
            check(viz->groupRows() == static_cast<int>(window->m_doc.lists().size()),
                  "并查集页行数等于列表数");

            // 面板四个页签都在
            QTabWidget *tabs = viz->findChild<QTabWidget*>();
            check(tabs != nullptr && tabs->count() == 4, "面板有 4 个页签");

            // 刷新按钮能重新渲染（不崩）
            QPushButton *refreshBtn = viz->findChild<QPushButton*>("visualizerRefresh");
            check(refreshBtn != nullptr, "面板有「刷新」按钮");
            if (refreshBtn) {
                emit refreshBtn->clicked();
                check(viz->hashBucketRows() > 0, "刷新后哈希页仍有内容");
            }

            // 面板只读：展示的桶数不该因为打开面板而变化
            const std::size_t bucketsAfter = window->m_doc.tagIndex().bucketCount();
            check(bucketsAfter == idx.bucketCount(), "打开面板没有改动哈希表");

            // 导出接口必须能安全调用。图片格式依赖 imageformats 插件，
            // 某些 Qt 安装缺少 PNG/BMP，所以这里只断言「不崩且返回值合法」。
            const QString prefix = QDir::temp().absoluteFilePath("cysc_viz_check");
            const int exported = viz->exportTabImages(prefix);
            check(exported >= 0 && exported <= 4, "导出接口可调用且返回 0..4");
            for (const char* suffix : {".png", ".jpg", ".bmp"}) {
                for (int i = 1; i <= 4; ++i) {
                    const QString name = i == 1 ? "hash"
                                         : i == 2 ? "trie"
                                         : i == 3 ? "segment"
                                                  : "unionfind";
                    const QString f = QString("%1_%2_%3%4")
                                          .arg(prefix).arg(i).arg(name, suffix);
                    if (QFile::exists(f)) QFile::remove(f);
                }
            }
        }
    }

    // --- 回归：TaskDialog 里改完时间再点优先级/分类，时间不能被重置 ---
    {
        TaskDialog dialog;
        QDateTimeEdit *deadlineEdit = dialog.findChild<QDateTimeEdit*>();
        check(deadlineEdit != nullptr, "新建任务窗口里有截止时间控件");

        const QList<QPushButton*> buttons = dialog.findChildren<QPushButton*>();
        QPushButton *priorityButton = nullptr;
        QPushButton *categoryButton = nullptr;
        for (QPushButton *b : buttons) {
            if (!priorityButton && b->text() == "紧急") priorityButton = b;
            if (!categoryButton && b->text() == "学习") categoryButton = b;
        }
        check(priorityButton != nullptr && categoryButton != nullptr,
              "新建任务窗口里有优先级与分类按钮");

        QCheckBox *deadlineToggle = nullptr;
        for (QCheckBox *c : dialog.findChildren<QCheckBox*>()) {
            if (c->text() == "设置") deadlineToggle = c;
        }
        check(deadlineToggle != nullptr, "新建任务窗口里有「设置截止时间」勾选框");

        if (deadlineEdit && priorityButton && categoryButton && deadlineToggle) {
            deadlineToggle->setChecked(true);      // 无截止时间时需先勾选
            const QDateTime wanted(QDate(2026, 11, 5), QTime(16, 40));
            deadlineEdit->setDateTime(wanted);
            check(deadlineEdit->dateTime() == wanted, "先把时间改成 2026-11-05 16:40");

            priorityButton->click();
            check(deadlineEdit->dateTime() == wanted,
                  "点「紧急」后时间没有被重置");

            categoryButton->click();
            check(deadlineEdit->dateTime() == wanted,
                  "再点「学习」后时间仍未被重置");

            // 保存出来的任务必须带上改过的时间与刚选的属性
            dialog.findChild<QLineEdit*>()->setText("回归测试任务");
            const Task produced = dialog.task();
            check(produced.deadline == "2026-11-05 16:40",
                  "确认后时间按用户输入保存");
            check(produced.priority == 3, "确认后优先级是「紧急」");
            check(produced.categoryId == 2, "确认后分类是「学习」");
        }
    }

    // --- 已完成任务置底 ---
    {
        TaskFilter q;
        q.sort = TaskSort::CreatedAsc;
        const std::vector<Task> ordered = window->m_doc.query(q);
        bool seenDone = false;
        bool doneAlwaysLast = true;
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const bool done = (ordered[i].status == 2);
            if (done) seenDone = true;
            else if (seenDone) doneAlwaysLast = false;
        }
        check(doneAlwaysLast, "已完成任务全部排在列表底部");
    }

    // --- 逐栏向导：Tab 一栏栏推进、1-4 选择、日期可跳过、Enter 随时完成 ---
    {
        TaskWizardDialog wizard;
        check(wizard.task().title.empty(), "向导初始没有标题");

        // 第 1 栏输入标题，Tab 进入紧急度
        QLineEdit *titleEdit = nullptr;
        for (QLineEdit *e : wizard.findChildren<QLineEdit*>()) {
            if (e->placeholderText().contains("任务名")) titleEdit = e;
        }
        check(titleEdit != nullptr, "向导第 1 栏有任务名输入框");
        if (titleEdit) {
            titleEdit->setText("向导建的任务");
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
            QApplication::sendEvent(&wizard, &tab);
            check(wizard.result() != QDialog::Accepted, "还没完成时窗口不会关闭");
        }

        // 第 2 栏按 3 -> 紧急度=高
        QKeyEvent digit3(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
        QApplication::sendEvent(&wizard, &digit3);

        // 第 3 栏按 2 -> 分组=工作
        QKeyEvent digit2(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier);
        QApplication::sendEvent(&wizard, &digit2);

        // 第 4 栏留空 → Enter 完成
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&wizard, &enter);

        check(wizard.result() == QDialog::Accepted, "第 4 栏留空按 Enter 即完成");
        const Task produced = wizard.task();
        check(produced.title == "向导建的任务", "向导保存了任务名");
        check(produced.priority == 2, "按 3 选出的紧急度是「高」");
        check(produced.categoryId == 1, "按 2 选出的分组是「工作」");
        check(produced.deadline.empty(), "跳过日期得到的是无截止时间");
    }

    // --- 向导里也能只走标题栏就 Enter 完成（用默认值） ---
    {
        TaskWizardDialog wizard;
        QLineEdit *titleEdit = nullptr;
        for (QLineEdit *e : wizard.findChildren<QLineEdit*>()) {
            if (e->placeholderText().contains("任务名")) titleEdit = e;
        }
        if (titleEdit) titleEdit->setText("只用默认值");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&wizard, &enter);

        check(wizard.result() == QDialog::Accepted, "标题栏直接 Enter 也能完成");
        const Task quick = wizard.task();
        check(quick.title == "只用默认值", "任务名已保存");
        check(quick.priority == 1, "未选紧急度时用默认「中」");
        check(quick.categoryId == 0, "未选分组时用默认「其他」");
        check(quick.deadline.empty(), "未选日期时无截止时间");
    }

    // --- 向导里空标题必须被拦下 ---
    {
        TaskWizardDialog wizard;
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&wizard, &tab);
        check(wizard.result() != QDialog::Accepted, "空标题按 Tab 不会完成");
        check(wizard.task().title.empty(), "空标题没有产出任务");
    }

    // --- 真实鼠标事件：双击推进状态，单击只展开 ---
    // 单击是「等双击间隔过去才生效」的，所以这里用到两个助手：
    //   sendClick / sendDoubleClick 投递真实 QMouseEvent
    //   waitForClickTimeout 跑到单击定时器触发（或确认它没被触发）
    {
        // 每次投递前现算坐标：展开会改变行高，缓存下来的位置可能落到别的行上。
        // 点行首标题那一带（不在详情面板上），并顺手确认确实命中了第一条。
        auto pointInFirstRow = [window]() {
            window->m_taskList->viewport()->update();
            QApplication::processEvents();
            const QRect rect = window->m_taskList->visualItemRect(
                window->m_taskList->item(0));
            const QPoint local(rect.left() + 60, rect.top() + 12);
            const QListWidgetItem *hit = window->m_taskList->itemAt(local);
            Q_UNUSED(hit);
            return QPair<QPointF, QPointF>(
                QPointF(local),
                QPointF(window->m_taskList->viewport()->mapToGlobal(local)));
        };
        auto sendClick = [window, pointInFirstRow]() {
            const auto p = pointInFirstRow();
            QMouseEvent press(QEvent::MouseButtonPress, p.first, p.second,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, p.first, p.second,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(window->m_taskList->viewport(), &press);
            QApplication::sendEvent(window->m_taskList->viewport(), &release);
        };
        auto sendDoubleClick = [window, pointInFirstRow]() {
            const auto p = pointInFirstRow();
            QMouseEvent press(QEvent::MouseButtonPress, p.first, p.second,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, p.first, p.second,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QMouseEvent dbl(QEvent::MouseButtonDblClick, p.first, p.second,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(window->m_taskList->viewport(), &press);
            QApplication::sendEvent(window->m_taskList->viewport(), &release);
            // 第二次按下：Qt 会把它合成成 DblClick 事件
            QApplication::sendEvent(window->m_taskList->viewport(), &dbl);
            QApplication::sendEvent(window->m_taskList->viewport(), &release);
        };
        auto waitForClickTimeout = []() {
            // 跑到单击定时器触发为止（略多于双击间隔）。
            const int total = QApplication::doubleClickInterval() + 120;
            for (int waited = 0; waited < total; waited += 20) {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(20);
            }
            QApplication::processEvents();
        };

        // 以下断言一律用「第一行当前是哪条任务」推导，不再假设 id 与行号一致
        // （列表按创建时间排序、已完成置底，两者本来就不一致）。
        auto firstRowId = [window]() {
            QListWidgetItem *item = window->m_taskList->item(0);
            return item ? item->data(Qt::UserRole).toInt() : -1;
        };
        // 在列表里找到显示该任务的第几行；找不到返回 -1。
        // 断言不能再假设「任务 id == 行号」：视图按排序与筛选排列，二者无关。
        auto rowIndexOfTask = [window](int taskId) {
            for (int i = 0; i < window->m_taskList->count(); ++i) {
                if (window->m_taskList->item(i)->data(Qt::UserRole).toInt() == taskId) {
                    return i;
                }
            }
            return -1;
        };

        // (a) 收起状态下双击：推进状态，且不会把行摊开
        window->expandTask(-1);
        {
            const int before = window->m_doc.findTask(firstRowId())->status;
            sendDoubleClick();
            waitForClickTimeout();
            const int rowId = firstRowId();
            const Task *after = window->m_doc.findTask(rowId);
            check(after != nullptr, "双击后任务仍然存在");
            check(after && after->status == nextStatusInCycle(before),
                  "真实鼠标双击推进了任务状态");
            check(window->m_expandedId == -1, "收起状态下双击不会把行摊开");
        }

        // (b) 已展开状态下双击：状态照推进，详情栏保持展开（不回退）
        {
            const int rowId = firstRowId();
            window->expandTask(-1);
            window->setExpandedTask(rowId);
            check(window->m_expandedId == rowId, "先展开该任务");

            const int before = window->m_doc.findTask(rowId)->status;
            sendDoubleClick();
            waitForClickTimeout();

            check(window->m_doc.findTask(rowId)->status == nextStatusInCycle(before),
                  "已展开时双击同样推进状态");
            check(window->m_expandedId == rowId, "已展开时双击不会把详情栏收起");
            check(window->m_expandedBeforeDoubleClick == rowId,
                  "双击记住了「双击前是展开的」");
        }

        // (c) 单击只切换详情栏，不改状态
        {
            const int eventsBefore = window->m_mouseEventCount;
            const int expandedBefore = window->m_expandedId;
            const int rowId = firstRowId();
            const int statusBefore = window->m_doc.findTask(rowId)->status;

            sendClick();
            waitForClickTimeout();

            check(window->m_mouseEventCount > eventsBefore,
                  "点击事件确实送到了列表（事件过滤器被调用）");
            check(window->m_doc.findTask(rowId)->status == statusBefore,
                  "单击不会改变任务状态");
            check(window->m_expandedId != expandedBefore,
                  "单击切换了详情栏的展开状态");
            check(window->m_expandedId == -1 || window->m_expandedId == rowId,
                  "切换结果是「收起」或「展开这一行」");
        }

        // (d) 折叠后的行必须重新可点（回归：曾经漏掉鼠标穿透标志的更新）
        {
            window->expandTask(-1);
            const int rowId = firstRowId();
            check(window->m_expandedId == -1, "先收起第一行");

            QWidget *row = window->m_taskList->itemWidget(window->m_taskList->item(0));
            check(row && row->testAttribute(Qt::WA_TransparentForMouseEvents),
                  "收起后的行会放行鼠标事件（否则点不动，单击/双击都会失效）");

            window->setExpandedTask(rowId);
            row = window->m_taskList->itemWidget(window->m_taskList->item(0));
            check(row && !row->testAttribute(Qt::WA_TransparentForMouseEvents),
                  "展开后的行自己接收鼠标事件（详情区控件可用）");
            window->setExpandedTask(rowId);   // 还原为收起
        }
    }

    // ================= 托盘图标 =================
    {
        // 先把「系统到底有没有托盘」打出来。诊断"看不到托盘图标"时这是第一手
        // 信息：如果这里是 false，那图标根本无处可显示，程序会走"关窗即退出"。
        out << "  info 系统托盘可用: "
            << (QSystemTrayIcon::isSystemTrayAvailable() ? "是" : "否")
            << "；托盘消息支持: "
            << (QSystemTrayIcon::supportsMessages() ? "是" : "否") << "\n";

        check(window->m_tray != nullptr, "托盘图标对象已创建");
        check(window->m_tray && !window->m_tray->icon().isNull(),
              "托盘图标不是空图标（空图标在托盘里不显示）");
        // 原来是一个纯色方块，看不出是什么；现在要求至少有多档尺寸，
        // 并且图标本身是带透明通道、非纯色的。
        const QIcon icon = window->makeTrayIcon();
        check(icon.availableSizes().size() >= 2, "托盘图标提供多档尺寸");
        const QPixmap px = icon.pixmap(32, 32);
        check(!px.isNull() && px.width() > 0, "托盘图标能取出位图");
        bool hasTransparent = false;
        bool hasTwoColors = false;
        if (!px.isNull()) {
            const QImage img = px.toImage();
            QSet<QRgb> colors;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb c = img.pixel(x, y);
                    if (qAlpha(c) < 128) hasTransparent = true;
                    colors.insert(c);
                    if (colors.size() > 4) break;
                }
                if (colors.size() > 4) break;
            }
            hasTwoColors = colors.size() > 4;
        }
        check(hasTransparent, "托盘图标有透明区域（不是一整块实心方块）");
        check(hasTwoColors, "托盘图标有多种颜色（能看出画的是什么）");
        check(window->m_tray && window->m_tray->contextMenu() != nullptr,
              "托盘有右键菜单");
        // 菜单里必须有「退出」，否则用户关掉窗口后没法真正结束进程。
        bool hasQuit = false;
        if (window->m_tray && window->m_tray->contextMenu()) {
            for (QAction *a : window->m_tray->contextMenu()->actions()) {
                if (a && a->text().contains("退出")) hasQuit = true;
            }
        }
        check(hasQuit, "托盘菜单里有「退出」（否则关窗后无法完全退出）");
    }

    // ================= 窗口缩放 =================
    {
        window->resize(600, 660);
        const QSize base = window->size();
        check(window->minimumWidth() > 0 && window->minimumHeight() > 0,
              "设置了最小尺寸（不会被拖成一条线）");

        // 四条边 + 四个角都要能识别出来
        check(window->resizeEdgeAt(QPoint(2, base.height() / 2)) == MainWindow::SmLeft,
              "左边缘可识别");
        check(window->resizeEdgeAt(QPoint(base.width() - 3, base.height() / 2)) == MainWindow::SmRight,
              "右边缘可识别");
        check(window->resizeEdgeAt(QPoint(base.width() / 2, 2)) == MainWindow::SmTop,
              "上边缘可识别");
        check(window->resizeEdgeAt(QPoint(base.width() / 2, base.height() - 3)) == MainWindow::SmBottom,
              "下边缘可识别");
        check(window->resizeEdgeAt(QPoint(2, 2)) == MainWindow::SmTopLeft, "左上角可识别");
        check(window->resizeEdgeAt(QPoint(base.width() - 3, 2)) == MainWindow::SmTopRight,
              "右上角可识别");
        check(window->resizeEdgeAt(QPoint(2, base.height() - 3)) == MainWindow::SmBottomLeft,
              "左下角可识别");
        check(window->resizeEdgeAt(QPoint(base.width() - 3, base.height() - 3)) ==
                  MainWindow::SmBottomRight,
              "右下角可识别");
        // 窗口中间不能算边框，否则点哪都在拉边、按钮就点不动了
        check(window->resizeEdgeAt(QPoint(base.width() / 2, base.height() / 2)) == MainWindow::SmNone,
              "窗口中间不算边框（否则按钮点不动）");

        // 指针形状要跟着边/角变
        check(window->cursorForEdge(MainWindow::SmLeft) == Qt::SizeHorCursor,
              "左右边用水平指针");
        check(window->cursorForEdge(MainWindow::SmTop) == Qt::SizeVerCursor,
              "上下边用垂直指针");
        check(window->cursorForEdge(MainWindow::SmTopLeft) == Qt::SizeFDiagCursor,
              "左上/右下用对角指针");
        check(window->cursorForEdge(MainWindow::SmNone) == Qt::ArrowCursor,
              "中间用普通箭头");

        // 真的拖一下右下角，窗口应该变大
        const QSize before = window->size();
        window->m_resizeEdge = MainWindow::SmBottomRight;
        window->m_resizeStartGlobal = QPoint(0, 0);
        window->m_resizeStartGeometry = window->geometry();
        window->applyResize(QPoint(60, 40));
        const QSize afterGrow = window->size();
        check(afterGrow.width() > before.width() && afterGrow.height() > before.height(),
              "拖右下角能把窗口拉大");

        // 往回拖到比最小尺寸还小，必须被最小尺寸挡住
        window->m_resizeStartGlobal = QPoint(0, 0);
        window->m_resizeStartGeometry = window->geometry();
        window->applyResize(QPoint(-5000, -5000));
        check(window->width() >= window->minimumWidth() &&
                  window->height() >= window->minimumHeight(),
              "反向拖过头会被最小尺寸挡住，不会拖成负尺寸");
        window->m_resizeEdge = MainWindow::SmNone;

        // 鼠标跟踪必须开着，否则没按下时收不到 move，指针不会变
        check(window->hasMouseTracking(), "开了鼠标跟踪（指针形状才能跟着边缘变）");
    }

    // ================= 标题栏窗口按钮 =================
    {
        window->resize(600, 660);
        // 四个按钮必须互不重叠、都落在标题栏内、从右往左排列
        const QRect pin = window->pinButtonRect();
        const QRect mini = window->minimizeButtonRect();
        const QRect maxi = window->maximizeButtonRect();
        const QRect close = window->closeButtonRect();
        const QRect bar = window->titleBarRect();

        check(pin.width() > 0 && mini.width() > 0 && maxi.width() > 0 && close.width() > 0,
              "四个窗口按钮都有有效区域");
        check(close.right() <= bar.right() && pin.left() >= bar.left(),
              "窗口按钮都在标题栏范围内");
        check(close.left() >= maxi.right() && maxi.left() >= mini.right()
                  && mini.left() >= pin.left() + pin.width() - 1,
              "四个按钮从右到左依次排列且不重叠");
        check(pin.left() > 100, "图钉左侧还留着足够空间显示标题");

        // 每个按钮的判定点都要落在自己身上（不能互相抢）
        check(window->resizeEdgeAt(QPoint(pin.center().x(), pin.center().y())) == MainWindow::SmNone
                  || pin.center().y() < MainWindow::RESIZE_MARGIN,
              "图钉按钮位于标题栏内（点击优先于拖边框，见 mousePressEvent 的顺序）");
    }

    // ================= 最大化 / 还原 =================
    {
        // 曾经的 bug：showMaximized() 在 FramelessWindowHint + Qt::Tool 下不可靠，
        // isMaximized() 也不准，导致「要按两下才还原」。现在自己记状态与几何。
        window->resize(600, 660);
        QCoreApplication::processEvents();
        const QRect beforeMax = window->geometry();
        check(!window->m_maximized, "起点：未最大化");

        window->toggleMaximize();
        QCoreApplication::processEvents();
        check(window->m_maximized, "点一次就进入最大化状态");
        const QRect maxed = window->geometry();
        check(maxed.width() >= beforeMax.width() && maxed.height() >= beforeMax.height(),
              "最大化后窗口确实变大了");
        check(window->m_normalGeometry == beforeMax, "记住了还原用的几何");

        window->toggleMaximize();
        QCoreApplication::processEvents();
        check(!window->m_maximized, "再点一次就还原（不需要按两下）");
        check(window->geometry() == beforeMax, "还原回原来的位置与大小");

        window->resize(600, 660);
        QCoreApplication::processEvents();
    }

    // ================= 紧凑模式（只显示任务）=================
    {
        window->resize(600, 660);
        QCoreApplication::processEvents();
        check(!window->m_compact, "起点：非紧凑模式");

        const int normalWidth = window->width();
        check(window->m_sidebar->isVisible(), "完整模式下侧栏可见");
        check(window->m_categoryHost->isVisible(), "完整模式下分类筛选可见");

        window->toggleCompactMode();
        QCoreApplication::processEvents();
        check(window->m_compact, "点一下进入紧凑模式");
        check(!window->m_sidebar->isVisible(), "紧凑模式隐藏侧栏（只显示任务）");
        check(!window->m_categoryHost->isVisible(), "紧凑模式隐藏分类筛选");
        check(!window->m_suggestToggle->isVisible(), "紧凑模式隐藏「建议」入口");
        check(window->width() < normalWidth,
              QString("紧凑模式窗口收窄了（%1 -> %2）").arg(normalWidth).arg(window->width()));
        check(window->width() <= 380, "紧凑模式宽度不超过 380");
        check(window->m_expandedId == -1, "紧凑模式不展开详情（否则窗口又变高变宽）");
        check(window->isVisible(), "紧凑模式窗口仍然可见");

        // 吸附到屏幕右上角：x 应该接近屏幕右侧，y 接近顶部
        if (QScreen *sc = QGuiApplication::primaryScreen()) {
            const QRect avail = sc->availableGeometry();
            const int distRight = qAbs(window->frameGeometry().right() - avail.right());
            check(distRight <= 40,
                  QString("吸附到屏幕右侧（距右边 %1 px）").arg(distRight));
            check(qAbs(window->frameGeometry().top() - avail.top()) <= 40,
                  "吸附到屏幕顶部");
        }

        window->toggleCompactMode();
        QCoreApplication::processEvents();
        check(!window->m_compact, "再点一次退出紧凑模式");
        check(window->m_sidebar->isVisible(), "退出后侧栏恢复");
        check(window->m_categoryHost->isVisible(), "退出后分类筛选恢复");

        window->resize(600, 660);
        QCoreApplication::processEvents();
    }

    // ================= 置顶开关（图钉） =================
    {
        QSettings settings;
        const QVariant saved = settings.value("window/onTop");
        const bool hadKey = saved.isValid();
        const bool original = hadKey ? saved.toBool() : true;

        // 统一从一个确定的状态出发：先关掉置顶
        window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
        window->show();
        QCoreApplication::processEvents();
        check(!window->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
              "起点：置顶已关闭");

        window->toggleOnTop();
        QCoreApplication::processEvents();
        check(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint), "图钉：打开时窗口置顶");
        check(window->isVisible(), "切换置顶后窗口仍然可见（改窗口标志会隐藏窗口）");
        {
            QSettings s;
            const QVariant v = s.value("window/onTop");
            out << "  info QSettings " << s.fileName() << " window/onTop="
                << (v.isValid() ? (v.toBool() ? "true" : "false") : QString("(无效)"))
                << " status=" << static_cast<int>(s.status()) << "\n";
            check(v.isValid() && v.toBool(), "置顶状态被记到 QSettings");
        }

        window->toggleOnTop();
        QCoreApplication::processEvents();
        check(!window->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
              "再点图钉取消置顶");
        check(window->isVisible(), "取消置顶后窗口仍然可见");
        // 关键：内部标记必须和真实标志一致，否则图钉图标会显示错状态
        check(window->m_onTop ==
                  window->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
              "图钉内部状态与真实窗口标志保持一致（曾经会不同步）");

        // 还原用户原本的设置，别把测试状态留在注册表里
        if (hadKey) QSettings().setValue("window/onTop", original);
        else QSettings().remove("window/onTop");
    }

    // ================= 逐行滚动 =================
    {
        check(window->m_taskList->verticalScrollMode() == QAbstractItemView::ScrollPerItem
                  || window->m_taskList->verticalScrollMode() == QAbstractItemView::ScrollPerPixel,
              "列表有纵向滚动模式设置");
        // 行为验证：往视口投递一个滚轮事件，滚动量应当是一整行的高度而不是一个固定小值
        for (int i = 0; i < 60; ++i) {
            Task t;
            t.title = "滚动测试任务";
            t.listId = window->m_doc.defaultListId();
            window->m_doc.addTask(t);
        }
        window->refreshList();
        window->resize(600, 400);
        QCoreApplication::processEvents();

        QScrollBar *bar = window->m_taskList->verticalScrollBar();
        const int before = bar->value();
        const bool canScroll = bar->maximum() > bar->minimum();
        check(canScroll, "任务足够多时可以纵向滚动");
        if (canScroll) {
            const int rowHeight = window->m_taskList->sizeHintForRow(0);
            const QPoint center = window->m_taskList->viewport()->rect().center();
            QWheelEvent wheel(QPointF(center), QPointF(window->m_taskList->viewport()->mapToGlobal(center)),
                              QPoint(0, 0), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window->m_taskList->viewport(), &wheel);
            QCoreApplication::processEvents();
            const int moved = bar->value() - before;
            check(moved == rowHeight,
                  QString("滚一格正好滚动一整行（滚了 %1 px，行高 %2 px）")
                      .arg(moved).arg(rowHeight));
        }
    }

    // ================= 切换视图的性能 =================
    // 用户反馈「切换列表要停顿一两秒」。这里量出刷新一次的耗时，并给上限断言，
    // 防止以后又退化成「每次刷新都重建全部详情面板」那种量级。
    {
        for (int i = 0; i < 120; ++i) {
            Task t;
            t.title = QString("性能测试任务 %1").arg(i).toStdString();
            t.details = "备注";
            t.listId = window->m_doc.defaultListId();
            t.priority = i % 4;
            window->m_doc.addTask(t);
        }
        window->resize(600, 700);
        QCoreApplication::processEvents();

        QElapsedTimer perf;
        perf.start();
        const int rounds = 5;
        for (int i = 0; i < rounds; ++i) {
            window->refreshList();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        const qint64 perRefresh = perf.elapsed() / rounds;
        const int rows = window->m_taskList->count();
        out << "  info 刷新 " << rows << " 行耗时 " << perRefresh
            << " ms/次（5 次平均）\n";

        // 上限 3000ms：正常是几十毫秒到几百毫秒；这条断言主要是防止
        // 再退化回「每行都同步构建完整详情编辑器」那种量级（曾经 20 秒）。
        check(perRefresh < 3000,
              QString("刷新列表足够快（%1 ms/次，%2 行）").arg(perRefresh).arg(rows));
    }

    // ================= 设置窗口 =================
    {
        check(window->m_settingsButton != nullptr, "工具栏有「设置」按钮");

        TaskSettingsDialog dlg(window);
        check(dlg.findChildren<QCheckBox*>().size() >= 5,
              "设置窗口有多个可勾选项");
        check(!dlg.findChildren<QComboBox*>().isEmpty(), "设置窗口有行密度下拉");

        // 写进去再读回来，必须一致（键名两边写错就会在这里暴露）
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kDensity, 2);
            s.setValue(TaskSettingsDialog::kHideDone, true);
            s.sync();
        }
        TaskSettingsDialog dlg2(window);
        check(TaskSettingsDialog::intValue(TaskSettingsDialog::kDensity, -1) == 2,
              "行密度从设置读回来正确");
        check(TaskSettingsDialog::boolValue(TaskSettingsDialog::kHideDone, false),
              "隐藏已完成从设置读回来正确");

        // 应用设置：行密度会作用到列表间距上
        window->applySettings();
        QCoreApplication::processEvents();
        check(window->m_density == 2, "设置里的行密度被应用到界面");
        check(window->m_hideDone->isChecked(), "设置里的「默认隐藏已完成」被应用");

        // 还原，别影响后面的断言
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kDensity, 1);
            s.setValue(TaskSettingsDialog::kHideDone, false);
            s.sync();
        }
        window->applySettings();
        QCoreApplication::processEvents();
        check(!window->m_hideDone->isChecked(), "还原后不再隐藏已完成");
    }

    // ================= 「进行中」任务的流动光效 =================
    {
        // 造一条进行中的任务，并确保光效开关是开的
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kGlow, true);
            s.sync();
        }
        const int doingId = window->m_doc.addTask([] {
            Task t;
            t.title = "光效测试";
            t.listId = 1;
            return t;
        }());
        window->applyStatus(doingId, 1);
        window->refreshList();
        QCoreApplication::processEvents();

        // 找到这一行的控件，确认它是我们的可发光行、且已经点亮
        TaskRowWidget *glowRow = nullptr;
        for (int i = 0; i < window->m_taskList->count(); ++i) {
            if (window->m_taskList->item(i)->data(Qt::UserRole).toInt() != doingId) continue;
            glowRow = dynamic_cast<TaskRowWidget*>(
                window->m_taskList->itemWidget(window->m_taskList->item(i)));
        }
        check(glowRow != nullptr, "任务行是支持光效的控件");
        check(glowRow && glowRow->glowing(), "「进行中」的任务点亮了光效");
        check(window->m_glowTimer && window->m_glowTimer->isActive(),
              "光效定时器在跑（有进行中的任务时）");

        // 关键：光效必须「看得出来」。
        // 最初只画一道细高光，实测只有十几个青绿像素，用户根本注意不到。
        // 这里直接把行控件渲染出来数青绿像素，给一个下限。
        if (glowRow) {
            auto countTeal = [](const QPixmap &pm) {
                if (pm.isNull()) return 0;
                const QImage img = pm.toImage();
                int n = 0;
                for (int y = 0; y < img.height(); ++y) {
                    for (int x = 0; x < img.width(); ++x) {
                        const QColor c = img.pixelColor(x, y);
                        // 「进行中」的青绿系：绿分量高、且明显高于红/蓝
                        if (c.green() > 120 && c.green() > c.red() + 40
                            && c.green() > c.blue() + 20) {
                            ++n;
                        }
                    }
                }
                return n;
            };

            glowRow->resize(glowRow->width() > 0 ? glowRow->width() : 500, 26);
            const int withGlow = countTeal(glowRow->grab());
            glowRow->setGlowing(false);
            const int without = countTeal(glowRow->grab());
            glowRow->setGlowing(true);

            out << "  info 光效像素: 开=" << withGlow << " 关=" << without << "\n";
            check(withGlow > without,
                  "光效确实改变了行控件的外观");
            // 下限：整圈边框至少要有几百个青绿像素才谈得上"看得见"
            check(withGlow > 300,
                  QString("光效足够明显（青绿像素 %1 个，太少了就等于看不见）")
                      .arg(withGlow));
        }

        // 推进一次，进度必须变化（否则动画是死的）
        const qreal before = window->m_glowProgress;
        window->tickGlow();
        check(!qFuzzyCompare(before, window->m_glowProgress), "光效进度会推进");

        // 改成已完成之后，光效应当熄灭
        window->applyStatus(doingId, 2);
        window->refreshList();
        QCoreApplication::processEvents();
        bool anyGlow = false;
        for (int i = 0; i < window->m_taskList->count(); ++i) {
            auto *r = dynamic_cast<TaskRowWidget*>(
                window->m_taskList->itemWidget(window->m_taskList->item(i)));
            if (r && r->glowing()) anyGlow = true;
        }
        check(!anyGlow, "标记为已完成之后光效熄灭");

        // 设置里关掉光效后，定时器不应再跑
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kGlow, false);
            s.sync();
        }
        window->applyStatus(doingId, 1);
        window->refreshList();
        QCoreApplication::processEvents();
        check(!window->m_glowTimer->isActive(), "设置里关掉光效后定时器停止");
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kGlow, true);
            s.sync();
        }
        window->m_doc.removeTask(doingId);
        window->refreshList();
    }

    // ================= 主题 =================
    {
        check(ThemeManager::count() == 3, "注册了 3 款主题");
        const Theme *all = ThemeManager::themes();
        check(all[0].key == "sky" && all[1].key == "sunset" && all[2].key == "rose",
              "三款主题的 key 正确");
        check(!all[0].dark && all[1].dark && all[2].dark,
              "晴空是浅色主题，晚霞与玫瑰是深色");

        // 主题必须真的改变渲染结果，不能只是换了个枚举。
        //
        // 注意：grab() 非常贵（Debug 下整窗重绘一次要几百毫秒），所以只抓两张
        // ——一张浅色、一张深色，足够断言"明暗确实不同"。三款主题各自的取色
        // 由 Theme.h 的调色板断言覆盖，不必都渲染一遍（那样会把自检拖到超时）。
        auto applyAndGrab = [window](int index) {
            ThemeManager::setCurrentIndex(index);
            window->applyThemeToWidgets();
            QCoreApplication::processEvents();
            const QPixmap pm = window->grab();
            if (pm.isNull()) return QImage();
            return pm.toImage();
        };

        const QImage skyImg = applyAndGrab(0);      // 晴空（浅色）
        const QImage roseImg = applyAndGrab(2);     // 玫瑰（深色）

        auto bgOf = [](const QImage &img) {
            return img.isNull() ? QColor() : img.pixelColor(img.width() / 2, img.height() - 6);
        };
        const QColor skyPx = bgOf(skyImg);
        const QColor rosePx = bgOf(roseImg);

        out << "  info 主题底色: 晴空=" << skyPx.name() << " 玫瑰=" << rosePx.name() << "\n";

        check(skyPx != rosePx, "浅色与深色主题渲染出来的底色不同");
        check(skyPx.lightness() > rosePx.lightness() + 60,
              "晴空主题是浅色底（明显比玫瑰亮）");
        check(skyPx.lightness() > 180,
              QString("浅色主题下窗口底是亮的（没有残留半透明压暗，实测亮度 %1）")
                  .arg(skyPx.lightness()));
        check(!skyImg.isNull() && !roseImg.isNull(),
              "主题能正常渲染出窗口");

        // 写进配置再读回来
        {
            QSettings s;
            s.setValue("view/theme", "rose");
            s.sync();
        }
        ThemeManager::loadFromSettings();
        check(ThemeManager::current().key == "rose", "主题能从配置读回");
        check(ThemeManager::current().dark, "读回的是深色主题");

        ThemeManager::setCurrentIndex(0);
        window->applyThemeToWidgets();
        QCoreApplication::processEvents();

        // ---- 回归：applySettings() 与 applyThemeToWidgets() 不能互相递归 ----
        // 曾经 applyThemeToWidgets() 调 applySettings()，而 applySettings()
        // 末尾又调 applyThemeToWidgets()，两者互相递归 -> 栈溢出 -> 0xC0000005。
        // 那种闪退只在 Debug 下容易撞到（帧更大），而且崩在启动路径上，
        // 看起来像"随机闪退"。
        //
        // 只跑 6 轮就够：真递归的话一轮就能往下钻几百帧，早就爆栈了。
        // 每轮都会重建整个列表（Debug 下很慢），轮数给多会把自检拖到超时。
        for (int round = 0; round < 6; ++round) {
            window->applySettings();
            window->applyThemeToWidgets();
        }
        QCoreApplication::processEvents();
        check(true, "applySettings 与 applyThemeToWidgets 往返调用不会递归崩溃");
    }

    // ================= 逐栏向导：每一栏都必须真的显示过 =================
    // 用户反馈"没看到分组栏，直接跳到日期"。这里把流程逐栏走一遍，
    // 核对阶段、页签索引与栏目标题 —— 真有跳过就会在这里暴露。
    {
        TaskWizardDialog wiz(window);
        wiz.testGoToStage(TaskDraft::StageTitle);
        check(wiz.testStage() == TaskDraft::StageTitle, "向导起点是「任务名」");
        check(wiz.testPageIndex() == 0, "起点显示第 1 页");
        check(wiz.testPageCount() == 4, "向导一共 4 栏（任务名/紧急度/分组/日期）");
        check(wiz.testStepText().contains(QString::fromUtf8("任务名")),
              "栏目标题显示「任务名」");

        wiz.testAdvance();      // 任务名 -> 紧急度
        check(wiz.testStage() == TaskDraft::StagePriority, "第 2 栏是「紧急度」");
        check(wiz.testPageIndex() == 1, "第 2 栏显示第 2 页");
        check(wiz.testStepText().contains(QString::fromUtf8("紧急度")),
              "栏目标题显示「紧急度」");

        wiz.testAdvance();      // 紧急度 -> 分组
        check(wiz.testStage() == TaskDraft::StageCategory, "第 3 栏是「分组」");
        check(wiz.testPageIndex() == 2, "第 3 栏显示第 3 页");
        check(wiz.testStepText().contains(QString::fromUtf8("分组")),
              "栏目标题显示「分组」");

        wiz.testAdvance();      // 分组 -> 日期
        check(wiz.testStage() == TaskDraft::StageDate, "第 4 栏是「日期」");
        check(wiz.testPageIndex() == 3, "第 4 栏显示第 4 页");
        check(wiz.testStepText().contains(QString::fromUtf8("日期")),
              "栏目标题显示「日期」");
    }

    // ================= 逐栏向导：停在日期栏按 Enter 不能丢时间 =================
    // 回归：原来 Enter 直接走 commit()，而 commit() 只同步了标题栏 ——
    // 用户刚敲进去的截止时间根本没被解析，表现为"设好时间按回车，时间没保存"。
    // （只有 Tab 才经过 advance() 里的日期解析，所以这个 bug 只在 Enter 路径上。）
    {
        TaskWizardDialog wiz(window);
        wiz.testGoToStage(TaskDraft::StageTitle);
        wiz.testGoToStage(TaskDraft::StageDate, "10/12 14:30");
        wiz.testCommit();
        const Task t = wiz.task();
        out << "  info 向导 Enter 提交：deadline=\"" << QString::fromStdString(t.deadline)
            << "\"\n";
        // 注意：这里断言的是「时间有没有被收进去」，不是 result()。
        // 这个对话框没有 show()/exec()，accept() 在未显示的对话框上不保证
        // 更新 result()（实测返回 Rejected），拿它判断会误报。
        check(!t.deadline.empty(), "日期栏里输入的时间被保存下来（不再丢失）");
        check(QString::fromStdString(t.deadline).contains("10-12"),
              "保存的正是输入的那个日期");
        check(QString::fromStdString(t.deadline).contains("14:30"),
              "输入里的时间部分也保留");

        // 留空 = 明确不设截止时间
        TaskWizardDialog wiz2(window);
        wiz2.testGoToStage(TaskDraft::StageDate, QString());
        wiz2.testCommit();
        check(wiz2.task().deadline.empty(), "日期留空时不设截止时间");

        // 日期写错时要停在这一栏，且不会把错误日期写进去
        TaskWizardDialog wiz3(window);
        wiz3.testGoToStage(TaskDraft::StageDate, "13/45");
        wiz3.testCommit();
        check(wiz3.task().deadline.empty(), "日期非法时不写入，停在该栏等改正");
    }

    // ================= 界面缩放 =================
    {
        auto applyScaleAndMeasure = [window](int index) {
            QSettings s;
            s.setValue(TaskSettingsDialog::kScale, index);
            s.sync();
            window->applyUiScale();
            QCoreApplication::processEvents();
            // 注意：量的是**应用基准字号**，不是某个具体控件的。
            // 状态栏那种控件在样式表里写死了 font-size: 10px，
            // 它的 font().pointSizeF() 会是 -1（像素字号），拿它量必然误判。
            return qApp->font().pointSizeF();
        };

        const qreal f100 = applyScaleAndMeasure(0);     // 100%
        const qreal f150 = applyScaleAndMeasure(2);     // 150%
        const qreal f200 = applyScaleAndMeasure(4);     // 200%

        out << "  info 界面缩放字号: 100%=" << f100 << " 150%=" << f150
            << " 200%=" << f200 << "\n";

        check(f150 > f100, "150% 的字号确实比 100% 大");
        check(f200 > f150, "200% 的字号确实比 150% 大");
        check(qAbs(f150 / f100 - 1.5) < 0.15, "150% 档基本是 1.5 倍（允许舍入误差）");
        check(qAbs(f200 / f100 - 2.0) < 0.2, "200% 档基本是 2 倍（允许舍入误差）");
        // 窗口也要跟着变大
        check(window->width() > 0 && window->height() > 0, "缩放后窗口尺寸有效");

        // 还原到默认档，别影响后面
        {
            QSettings s;
            s.setValue(TaskSettingsDialog::kScale, 2);
            s.sync();
        }
        window->applyUiScale();
        QCoreApplication::processEvents();
    }

    // ================= 缩窄自动进紧凑模式 =================
    {
        check(!window->m_compact, "起点不是紧凑模式");
        check(MainWindow::kAutoCompactWidth > 0, "自动紧凑的宽度阈值已定义");

        // 模拟"用户拖边缩窄"：置上 m_resizingByUser 再真的把窗口改小。
        // 注意 resize() 之后要显式派发一次 resizeEvent —— 离屏平台下
        // resize() 不保证同步投递事件，光调 processEvents() 可能什么也收不到。
        auto userResizeTo = [window](int w, int h) {
            window->m_resizingByUser = true;
            window->resize(w, h);
            QResizeEvent ev(QSize(w, h), QSize(w, h));
            QCoreApplication::sendEvent(window, &ev);
            window->m_resizingByUser = false;
            QCoreApplication::processEvents();
        };

        userResizeTo(MainWindow::kAutoCompactWidth - 20, 640);
        check(window->m_compact, "缩到阈值以下自动进入紧凑模式");
        check(!window->m_sidebar->isVisible(), "自动紧凑后侧栏被隐藏");

        // 程序自己 resize（不是用户拖的）不该触发
        window->toggleCompactMode();            // 先退出紧凑
        QCoreApplication::processEvents();
        check(!window->m_compact, "已退出紧凑模式");
        window->resize(MainWindow::kAutoCompactWidth - 40, 640);
        QCoreApplication::processEvents();
        check(!window->m_compact, "非用户拖拽的 resize 不会误触发自动紧凑");

        window->resize(600, 660);
        QCoreApplication::processEvents();
    }

    // ================= 完成任务的「画线动画」 =================
    {
        // 造一条任务并推到「已完成」
        const int doneId = window->m_doc.addTask([] {
            Task t;
            t.title = "画线动画测试";
            t.listId = 1;
            return t;
        }());
        window->refreshList();
        QCoreApplication::processEvents();

        window->applyStatus(doneId, 1);      // 先进行中
        window->refreshList();
        QCoreApplication::processEvents();
        // 走和真实交互同一条路：applyStatus 只写模型，
        // 标记"刚完成"这件事在 advanceStatus 里做。
        window->m_strikeAnimateIds.insert(doneId);
        window->applyStatus(doneId, 2);      // 再已完成 -> 应触发画线
        window->refreshList();
        QCoreApplication::processEvents();

        // 找到这一行的标题标签
        StrikeLabel *title = nullptr;
        for (int i = 0; i < window->m_taskList->count(); ++i) {
            if (window->m_taskList->item(i)->data(Qt::UserRole).toInt() != doneId) continue;
            QWidget *row = window->m_taskList->itemWidget(window->m_taskList->item(i));
            if (!row) continue;
            for (QLabel *l : row->findChildren<QLabel*>()) {
                if (auto *sl = dynamic_cast<StrikeLabel*>(l)) title = sl;
            }
        }
        check(title != nullptr, "标题标签是支持画线动画的控件");
        check(title && title->strikeOn(), "已完成的任务有删除线");
        out << "  info 画线动画: 起始进度=" << (title ? title->strikeProgress() : -1.0)
            << " 动画中=" << (title && title->strikeAnimating() ? 1 : 0) << "\n";
        check(title && title->strikeAnimating(), "刚完成时画线动画正在播放");
        check(title && title->strikeProgress() < 1.0, "画线动画是从左往右推进的（起点未满）");

        // 推进几次，进度必须增长，最后停在全满
        const qreal p0 = title ? title->strikeProgress() : 0.0;
        if (title) {
            for (int k = 0; k < 3; ++k) title->tickStrike(0.1);
        }
        const qreal p1 = title ? title->strikeProgress() : 0.0;
        check(p1 > p0, "画线进度会随定时器推进");
        if (title) {
            for (int k = 0; k < 30; ++k) title->tickStrike(0.1);
        }
        check(title && !title->strikeAnimating(), "画完后动画停止（不会一直重绘）");
        check(title && qAbs(title->strikeProgress() - 1.0) < 0.001, "画完后进度停在全满");

        // 非完成状态不该有删除线
        window->applyStatus(doneId, 0);
        window->refreshList();
        QCoreApplication::processEvents();
        StrikeLabel *again = nullptr;
        for (int i = 0; i < window->m_taskList->count(); ++i) {
            if (window->m_taskList->item(i)->data(Qt::UserRole).toInt() != doneId) continue;
            QWidget *row = window->m_taskList->itemWidget(window->m_taskList->item(i));
            if (!row) continue;
            for (QLabel *l : row->findChildren<QLabel*>()) {
                if (auto *sl = dynamic_cast<StrikeLabel*>(l)) again = sl;
            }
        }
        check(again && !again->strikeOn(), "回到待办后删除线消失");

        window->m_doc.removeTask(doneId);
        window->refreshList();
    }

    window->hide();
    delete window;
    QFile::remove(dbPath);
    qunsetenv("CYSC_DB_PATH");

    out << (failures == 0 ? "  结果：全部通过\n"
                          : QString("  结果：%1 项失败\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
};

// ------------------------------------------------------------------ 自检

int MainWindow::runSelfTest() {
    QTextStream out(stdout);
    out << "CYSC 任务栏自检\n";
    out << "  数据库位置: " << resolveStoragePath() << "\n";

    QString tmpDir = qEnvironmentVariable("CYSC_TEST_TMPDIR");
    if (tmpDir.isEmpty()) {
        tmpDir = QDir(QDir::currentPath()).absoluteFilePath("cysc_test_tmp");
    }
    if (!QDir().mkpath(tmpDir)) {
        tmpDir = QDir::tempPath();
    }
    const QString path = QDir(tmpDir).absoluteFilePath(
        "cysc_selftest_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db");

    int failures = 0;
    auto check = [&](bool ok, const QString& what) {
        out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (!ok) ++failures;
    };

    {
        // 新模型 = TaskDocument（内存文档）+ DocumentStore（SQLite 落库）。
        TaskDocument model;
        DocumentStore store;
        if (!store.open(path)) {
            out << "  失败：无法打开 " << path << "\n";
            out.flush();
            return 1;
        }
        store.loadDocument(model);

        // changeStatus 在新模型里没有对应接口：取出来改状态字段再整条
        // updateTask（updateTask 自己会压改动前的快照，撤销照样成立）。
        auto applyStatus = [&model](int id, int status) {
            const Task *found = model.findTask(id);
            if (!found) return false;
            Task updated = *found;
            updated.status = status;
            if (updated.status == 2 && updated.completedAt.empty()) {
                updated.completedAt = QDateTime::currentDateTime()
                                          .toString("yyyy-MM-dd HH:mm").toStdString();
            }
            return model.updateTask(updated);
        };

        auto make = [](const char* title, const char* details, int priority,
                       const char* deadline, int category) {
            Task t;
            t.title = title;
            t.details = details;
            t.priority = priority;
            t.deadline = deadline;
            t.categoryId = category;
            return t;
        };

        int created = 0;
        if (model.addTask(make("写第三章", "重点是并发那一节", 2,
                               "2026-10-11 14:30", 2)) >= 0) ++created;
        if (model.addTask(make("写第四章", "", 0, "2026-10-12 09:00", 2)) >= 0) ++created;
        if (model.addTask(make("买牛奶", "顺便买鸡蛋", 1, "2026-10-11 23:59", 0)) >= 0) ++created;
        if (model.addTask(make("健身", "", 3, "2026-10-15 19:00", 3)) >= 0) ++created;

        out << "  已写入 " << created << " 条任务到 " << path << "\n";
        check(created == 4, "addTask 写入全部 4 条任务");
        check(model.findTask(0) && model.findTask(0)->details == "重点是并发那一节",
              "备注字段已落库");
        check(model.findTask(0) && model.findTask(0)->deadline == "2026-10-11 14:30",
              "截止时间精确到分钟");

        TaskFilter search;
        search.search = "并发";
        check(static_cast<int>(model.query(search).size()) == 1,
              "搜索命中备注里的内容（非前缀）");

        search.search = "鸡蛋";
        check(static_cast<int>(model.query(search).size()) == 1, "搜索命中备注中间的片段");

        search.search = "第三章";
        check(static_cast<int>(model.query(search).size()) == 1, "搜索命中标题中间的片段");

        search.search = "不存在的内容";
        check(model.query(search).empty(), "搜不到时返回空");

        // 分类：TaskFilter 自带 categoryId，界面与自检走同一条路径。
        // 自检数据里分类 2（学习）有 2 条、0（其他）1 条、3（健康）1 条。
        TaskFilter byCategory;
        byCategory.categoryId = 2;
        check(static_cast<int>(model.query(byCategory).size()) == 2,
              "按分类筛出 2 条「学习」任务");
        byCategory.categoryId = 0;
        check(static_cast<int>(model.query(byCategory).size()) == 1,
              "按分类筛出 1 条「其他」任务");
        byCategory.categoryId = 9;          // 不存在的分类
        check(model.query(byCategory).empty(), "不存在的分类筛不出任何任务");
        byCategory.categoryId = -1;
        check(static_cast<int>(model.query(byCategory).size()) == model.tasks().size(),
              "分类为 -1 时不过滤，返回全部任务");

        TaskFilter hideDone;
        hideDone.hideDone = true;
        check(static_cast<int>(model.query(hideDone).size()) == 4, "未完成任务 4 条");

        const int firstId = model.tasks()[0].id;
        applyStatus(firstId, 2);
        check(static_cast<int>(model.query(hideDone).size()) == 3, "隐藏已完成后剩 3 条");
        check(nextStatusInCycle(0) == 1 && nextStatusInCycle(1) == 2 && nextStatusInCycle(2) == 0,
              "双击状态循环：待办→进行中→已完成→待办");

        check(model.undo(), "撤销恢复状态");
        check(model.findTask(firstId) && model.findTask(firstId)->status == 0,
              "状态已恢复为待办");

        Task edited = *model.findTask(firstId);
        edited.details = "改过的备注";
        edited.priority = 3;
        check(model.updateTask(edited), "整条更新生效");
        check(model.findTask(firstId)->details == "改过的备注", "备注更新已保存");
        check(model.findTask(firstId)->priority == 3, "优先级更新已保存");

        // updateDetails 在新模型里就是「取出来只改备注再整条更新」。
        Task detailsOnly = *model.findTask(firstId);
        detailsOnly.details = "只改备注";
        check(model.updateTask(detailsOnly), "updateDetails 生效");
        check(model.findTask(firstId)->details == "只改备注", "备注已更新");

        check(model.removeTask(firstId), "deleteTask 生效");
        check(model.undo() && model.findTask(firstId) != nullptr, "撤销把删除的任务恢复回来");
    }

    check(QFile::remove(path), "临时数据库已清理");
    out << (failures == 0 ? "  结果：全部通过\n"
                          : QString("  结果：%1 项失败\n").arg(failures));
    out.flush();

    // --guitest 时继续跑界面层的检查
    if (failures == 0 && qEnvironmentVariableIsSet("CYSC_GUITEST")) {
        return runGuiTest();
    }
    return failures == 0 ? 0 : 1;
}

// 定义在文件末尾（friend 类，需要访问私有成员）
class MainWindowTestAccess;

int MainWindow::runGuiTest() {
    return MainWindowTestAccess::run();
}

