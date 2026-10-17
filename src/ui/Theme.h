#ifndef THEME_H
#define THEME_H

// ============================================================================
//  Theme.h
//  职责：主题配色表（三款主题：晴空 / 晚霞 / 玫瑰），供全部界面取色
//  依赖：QtGui（QColor）
// ============================================================================

#include <QColor>
#include <QString>

// 一款主题的全部取色。
//
// 为什么要把颜色集中到这里：之前颜色散落在 mainwindow.cpp / TaskSidebar.cpp /
// 各个对话框里（全项目两百多处字面量），加主题就得满仓库改。集中成一张表之后，
// 换主题只是换一个结构体，界面各处统一查表。
//
// 命名按「用途」而不是「颜色」：这样换主题时不会出现
// 「变量叫 blue 但值其实是金橙色」这种误导。
struct Theme {
    QString key;            // 存进配置用的标识（ascii）
    QString name;           // 设置界面里显示的名字
    bool dark = true;       // 深色还是浅色（决定文字用亮色还是暗色）

    // ---- 窗口 ----
    QColor windowTop;       // 背景渐变起点
    QColor windowBottom;    // 背景渐变终点（底）
    QColor windowBorder;    // 外边框
    QColor titleBarLeft;    // 标题栏渐变左
    QColor titleBarRight;   // 标题栏渐变右

    // ---- 强调色（按钮描边、图标、状态栏文字）----
    QColor accent;
    QColor accentDim;       // 强调色的弱化版（次级文字）
    QColor onAccent;        // 叠在强调色上的前景色

    // ---- 任务卡片 ----
    QColor cardBg;          // 折叠卡片底色
    QColor cardBorder;      // 折叠卡片描边
    QColor cardBgExpanded;  // 展开卡片底色
    QColor cardBorderExpanded;
    QColor cardTitleInk;    // 标题文字
    QColor cardMutedInk;    // 次要文字（时间等）

    // ---- 侧栏 ----
    QColor sidebarBg;
    QColor sidebarText;
    QColor sidebarMuted;
    QColor sidebarHover;
    QColor sidebarActive;

    // ---- 状态强调色：待办 / 进行中 / 已完成 / 已暂停 ----
    QColor statusAccent[4];
    QColor statusChipBg[4];
    QColor statusChipInk[4];

    // ---- 优先级（无 / 低 / 中 / 高）与分类（其他 / 工作 / 学习 / 健康）----
    //
    // 这两个调色板原来在 mainwindow.cpp、TaskDialog.cpp、TaskWizardDialog.cpp
    // 各抄了一份，加主题时必然对不齐。集中到这里，三处共用一份。
    QColor priorityAccent[4];
    QColor categoryAccent[4];

    // ---- 其他 ----
    QColor danger;          // 删除、错误提示
    QColor warn;            // 警告、逾期
    QColor toolbarBg;       // 工具栏输入框/按钮底色
    QColor panelBg;         // 详情面板底色
};

namespace ThemeManager {

// 三款主题的注册表（按顺序：晴空 / 晚霞 / 玫瑰）。
const Theme *themes();
int count();

// 当前主题。默认第 0 款。
const Theme &current();
// 按 key 切换；找不到就保持原样并返回 false。
bool setCurrentByKey(const QString &key);
// 按索引切换。
void setCurrentIndex(int index);
int currentIndex();

// 从 QSettings 读回上次选择并应用（启动时调一次）。
void loadFromSettings();
// 把当前选择写进 QSettings。
void saveToSettings();

// 便捷取色：直接拿 16 进制字符串，方便拼样式表。
QString hex(const QColor &c);

} // namespace ThemeManager

#endif
