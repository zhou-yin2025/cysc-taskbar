#include "Theme.h"

#include <QFontDatabase>
#include <QRegularExpression>
#include <QSettings>

namespace {

// 三款主题的定义。
//
// 设计口径：
//   1) 晴空 —— 天蓝 + 白，浅色底、深色字，干净通透
//   2) 晚霞 —— 金橙 + 落日紫，暖色深底
//   3) 玫瑰 —— 玫红 + 浅灰 + 黑，高对比
Theme makeSky() {
    Theme t;
    t.key = "sky";
    t.name = "晴空（天蓝·白）";
    t.dark = false;

    t.windowTop    = QColor("#EAF4FF");
    t.windowBottom = QColor("#CFE4FB");
    t.windowBorder = QColor("#5EA8E0");
    t.titleBarLeft  = QColor("#FFFFFF");
    t.titleBarRight = QColor("#D8EBFF");

    t.accent    = QColor("#1E88E5");
    t.accentDim = QColor("#5B7C9B");
    t.onAccent  = QColor("#FFFFFF");

    t.cardBg            = QColor("#FFFFFF");
    t.cardBorder        = QColor("#B9D8F2");
    t.cardBgExpanded    = QColor("#F2F9FF");
    t.cardBorderExpanded = QColor("#1E88E5");
    t.cardTitleInk      = QColor("#0E2A45");
    t.cardMutedInk      = QColor("#5B7C9B");

    t.sidebarBg    = QColor("#FFFFFF");
    t.sidebarText  = QColor("#12324F");
    t.sidebarMuted = QColor("#6E8CA8");
    t.sidebarHover = QColor("#DCEDFF");
    t.sidebarActive = QColor("#BEDDF9");

    t.statusAccent[0] = QColor("#1E88E5");   // 待办
    t.statusAccent[1] = QColor("#00A97F");   // 进行中
    t.statusAccent[2] = QColor("#8FA3B5");   // 已完成
    t.statusAccent[3] = QColor("#D99A00");   // 已暂停
    t.statusChipBg[0] = QColor("#1E88E5");
    t.statusChipBg[1] = QColor("#00C08B");
    t.statusChipBg[2] = QColor("#8FA3B5");
    t.statusChipBg[3] = QColor("#FFC53D");
    t.statusChipInk[0] = QColor("#FFFFFF");
    t.statusChipInk[1] = QColor("#04321F");
    t.statusChipInk[2] = QColor("#FFFFFF");
    t.statusChipInk[3] = QColor("#3A2A00");

    // 优先级：浅色底需要更深的色才够对比
    t.priorityAccent[0] = QColor("#9AA0A8");
    t.priorityAccent[1] = QColor("#0B7FD4");
    t.priorityAccent[2] = QColor("#D98A00");
    t.priorityAccent[3] = QColor("#D62B2B");
    t.categoryAccent[0] = QColor("#7A7A88");
    t.categoryAccent[1] = QColor("#D6006B");
    t.categoryAccent[2] = QColor("#00876C");
    t.categoryAccent[3] = QColor("#0B62D6");

    t.danger    = QColor("#D8434E");
    t.warn      = QColor("#D97A00");
    t.toolbarBg = QColor("#FFFFFF");
    t.panelBg   = QColor("#F7FBFF");
    return t;
}

Theme makeSunset() {
    Theme t;
    t.key = "sunset";
    t.name = "晚霞（金黄·橙）";
    t.dark = true;

    t.windowTop    = QColor("#2A1A22");
    t.windowBottom = QColor("#160D12");
    t.windowBorder = QColor("#FFA733");
    t.titleBarLeft  = QColor("#4A2A1E");
    t.titleBarRight = QColor("#2E1A16");

    t.accent    = QColor("#FFB020");
    t.accentDim = QColor("#C08A5A");
    t.onAccent  = QColor("#2A1400");

    t.cardBg            = QColor("#33222A");
    t.cardBorder        = QColor("#5C3A2E");
    t.cardBgExpanded    = QColor("#3D2830");
    t.cardBorderExpanded = QColor("#FFB020");
    t.cardTitleInk      = QColor("#FFE9C8");
    t.cardMutedInk      = QColor("#C79A78");

    t.sidebarBg    = QColor("#241419");
    t.sidebarText  = QColor("#F3DCC4");
    t.sidebarMuted = QColor("#A87B60");
    t.sidebarHover = QColor("#3A2229");
    t.sidebarActive = QColor("#503024");

    t.statusAccent[0] = QColor("#FF9A3C");   // 待办
    t.statusAccent[1] = QColor("#FFC93C");   // 进行中
    t.statusAccent[2] = QColor("#9A7A68");   // 已完成
    t.statusAccent[3] = QColor("#E06A2B");   // 已暂停
    t.statusChipBg[0] = QColor("#FF8A2B");
    t.statusChipBg[1] = QColor("#FFD24D");
    t.statusChipBg[2] = QColor("#8A6A58");
    t.statusChipBg[3] = QColor("#E0562B");
    t.statusChipInk[0] = QColor("#3A1700");
    t.statusChipInk[1] = QColor("#3A2A00");
    t.statusChipInk[2] = QColor("#FFFFFF");
    t.statusChipInk[3] = QColor("#FFFFFF");

    // 优先级 / 分类：晚霞主题统一往金橙方向偏
    t.priorityAccent[0] = QColor("#B09080");
    t.priorityAccent[1] = QColor("#FFB020");
    t.priorityAccent[2] = QColor("#FF8A2B");
    t.priorityAccent[3] = QColor("#FF4D3D");
    t.categoryAccent[0] = QColor("#A08878");
    t.categoryAccent[1] = QColor("#FF7A3C");
    t.categoryAccent[2] = QColor("#E0B040");
    t.categoryAccent[3] = QColor("#D96A2B");

    t.danger    = QColor("#FF5C4D");
    t.warn      = QColor("#FFB020");
    t.toolbarBg = QColor("#2B1A20");
    t.panelBg   = QColor("#2E1E24");
    return t;
}

Theme makeRose() {
    Theme t;
    t.key = "rose";
    t.name = "玫瑰（玫红·浅灰·黑）";
    t.dark = true;

    t.windowTop    = QColor("#1C1C1F");
    t.windowBottom = QColor("#0E0E10");
    t.windowBorder = QColor("#FF2D78");
    t.titleBarLeft  = QColor("#3A1A28");
    t.titleBarRight = QColor("#1E1E22");

    t.accent    = QColor("#FF2D78");
    t.accentDim = QColor("#9A8A92");
    t.onAccent  = QColor("#FFFFFF");

    t.cardBg            = QColor("#2A2A2E");
    t.cardBorder        = QColor("#4A4A52");
    t.cardBgExpanded    = QColor("#34343A");
    t.cardBorderExpanded = QColor("#FF2D78");
    t.cardTitleInk      = QColor("#F2E9EC");
    t.cardMutedInk      = QColor("#A0A0A8");

    t.sidebarBg    = QColor("#151517");
    t.sidebarText  = QColor("#EDE7E9");
    t.sidebarMuted = QColor("#8A8A92");
    t.sidebarHover = QColor("#2A2A2E");
    t.sidebarActive = QColor("#3A2230");

    t.statusAccent[0] = QColor("#9AA0A8");   // 待办
    t.statusAccent[1] = QColor("#FF2D78");   // 进行中
    t.statusAccent[2] = QColor("#6E6E76");   // 已完成
    t.statusAccent[3] = QColor("#C8A02D");   // 已暂停
    t.statusChipBg[0] = QColor("#9AA0A8");
    t.statusChipBg[1] = QColor("#FF2D78");
    t.statusChipBg[2] = QColor("#6E6E76");
    t.statusChipBg[3] = QColor("#D9B23C");
    t.statusChipInk[0] = QColor("#1A1A1C");
    t.statusChipInk[1] = QColor("#FFFFFF");
    t.statusChipInk[2] = QColor("#E8E8EC");
    t.statusChipInk[3] = QColor("#2A2200");

    // 优先级 / 分类：玫瑰主题用玫红 + 灰阶 + 一点金
    t.priorityAccent[0] = QColor("#9AA0A8");
    t.priorityAccent[1] = QColor("#C8C8D0");
    t.priorityAccent[2] = QColor("#D9B23C");
    t.priorityAccent[3] = QColor("#FF2D5E");
    t.categoryAccent[0] = QColor("#8A8A92");
    t.categoryAccent[1] = QColor("#FF2D78");
    t.categoryAccent[2] = QColor("#B0B0B8");
    t.categoryAccent[3] = QColor("#E0648A");

    t.danger    = QColor("#FF3B4E");
    t.warn      = QColor("#D9B23C");
    t.toolbarBg = QColor("#232326");
    t.panelBg   = QColor("#242428");
    return t;
}

// 第四款：谷地（信号黄·工业）—— 参考 hhikr/endfield-ui-skill 的 standard 预设。
//
// 这套风格的规矩和前三款不一样，映射时踩过两次坑，记在这里：
//
//   1) 底色是**中性灰白** #F4F4F2，不是米色（米色只出现在 site 预设里）
//   2) 信号黄 #FFFA00 **只做填充**：原设计写明 "Fill only; never a line or text
//      on light"。而我们 accent 的语义恰好是「描边/图标/状态栏文字」，
//      所以 accent 映射成墨黑 #161616（原设计里"线和字"由墨承担），
//      黄色单独放进 signal，只用于选中条与「进行中」的填充
//   3) 全直角：radius = 0
//   4) 背景是平面，不用渐变；底纹是等高线（不是斜线）
Theme makeEndfield() {
    Theme t;
    t.key = "endfield";
    t.name = "谷地（信号黄·工业）";
    t.dark = false;

    // 平面底：windowTop 与 windowBottom 取同一个值，等于关掉渐变
    t.windowTop    = QColor("#F4F4F2");
    t.windowBottom = QColor("#F4F4F2");
    t.windowBorder = QColor("#161616");
    t.titleBarLeft  = QColor("#F4F4F2");
    t.titleBarRight = QColor("#E4E4E0");

    // 线由墨色承担，黄色只做填充
    t.accent    = QColor("#161616");
    t.accentDim = QColor("#5C5C5C");
    t.onAccent  = QColor("#FFFFFF");

    t.signal    = QColor("#FFFA00");
    t.onSignal  = QColor("#161616");
    t.radius    = 0;
    t.rowNumbers = true;
    t.contour    = true;
    t.wideFont   = true;

    t.cardBg            = QColor("#FFFFFF");
    t.cardBorder        = QColor("#D9D9D6");
    t.cardBgExpanded    = QColor("#FFFFFF");
    t.cardBorderExpanded = QColor("#161616");
    t.cardTitleInk      = QColor("#161616");
    t.cardMutedInk      = QColor("#5C5C5C");

    t.sidebarBg    = QColor("#F4F4F2");
    t.sidebarText  = QColor("#161616");
    t.sidebarMuted = QColor("#5C5C5C");
    t.sidebarHover = QColor("#E4E4E0");
    t.sidebarActive = QColor("#FFFA00");

    // 单一亮色原则：只有「进行中」用信号黄填充，其余靠墨色深浅区分
    t.statusAccent[0] = QColor("#5C5C5C");   // 待办
    t.statusAccent[1] = QColor("#FFFA00");   // 进行中（填充）
    t.statusAccent[2] = QColor("#161616");   // 已完成
    t.statusAccent[3] = QColor("#B26A00");   // 已暂停（琥珀）
    t.statusChipBg[0] = QColor("#E4E4E0");
    t.statusChipBg[1] = QColor("#FFFA00");
    t.statusChipBg[2] = QColor("#161616");
    t.statusChipBg[3] = QColor("#F2C14E");
    t.statusChipInk[0] = QColor("#161616");
    t.statusChipInk[1] = QColor("#161616");
    t.statusChipInk[2] = QColor("#FFFFFF");
    t.statusChipInk[3] = QColor("#161616");

    // 优先级：灰阶递进，只有最紧急用危险色
    t.priorityAccent[0] = QColor("#B8B8B4");
    t.priorityAccent[1] = QColor("#5C5C5C");
    t.priorityAccent[2] = QColor("#161616");
    t.priorityAccent[3] = QColor("#B3261E");

    // 分类已并入列表（F-006），这组只剩历史用途，给中性灰阶
    t.categoryAccent[0] = QColor("#B8B8B4");
    t.categoryAccent[1] = QColor("#5C5C5C");
    t.categoryAccent[2] = QColor("#161616");
    t.categoryAccent[3] = QColor("#B3261E");

    t.danger  = QColor("#B3261E");
    t.warn    = QColor("#B26A00");
    t.toolbarBg = QColor("#FFFFFF");
    t.panelBg   = QColor("#F4F4F2");
    return t;
}

Theme g_themes[4] = {makeSky(), makeSunset(), makeRose(), makeEndfield()};

int g_current = 0;

const char *kSettingsKey = "view/theme";

} // namespace

namespace ThemeManager {

const Theme *themes() { return g_themes; }
int count() { return 4; }

const Theme &current() { return g_themes[g_current]; }

bool setCurrentByKey(const QString &key) {
    for (int i = 0; i < 4; ++i) {
        if (g_themes[i].key == key) {
            g_current = i;
            return true;
        }
    }
    return false;
}

void setCurrentIndex(int index) {
    if (index >= 0 && index < 4) g_current = index;
}

int currentIndex() { return g_current; }

void loadFromSettings() {
    QSettings s;
    const QString key = s.value(QString::fromLatin1(kSettingsKey),
                                g_themes[0].key).toString();
    if (!setCurrentByKey(key)) g_current = 0;
}

void saveToSettings() {
    QSettings s;
    s.setValue(QString::fromLatin1(kSettingsKey), current().key);
    s.sync();
}


bool wideFontAvailable() {
    static const bool has = [] {
        return QFontDatabase::families().contains(QStringLiteral("Saira"));
    }();
    return has;
}

QFont wideFont(qreal pointSize, qreal letterSpacing) {
    QFont f;
    if (wideFontAvailable()) f.setFamily(QStringLiteral("Saira"));
    f.setPointSizeF(pointSize);
    if (letterSpacing > 0.0) {
        f.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    }
    return f;
}

QString radiusCss() { return QString::number(current().radius); }

QString withRadius(QString css) {
    // 把样式表里所有 border-radius: <数字>px 换成当前主题的圆角。
    //
    // 为什么用"事后重写"而不是给每处样式表都插一个 %N 占位：
    // 圆角散落在十几个样式表里、各自 .arg 数量不同，逐个插参数极易错位；
    // 统一在字符串上改写一次，既不会漏，也不会碰坏已有的 .arg 链。
    // 用原始字符串字面量：正则里的 \s / \d 在普通字符串里会被当成未知转义，
    // 编出来变成 s* / d+px —— 这条规则就永远匹配不上，表现是"直角主题照样有圆角"。
    static const QRegularExpression re(QStringLiteral(R"(border-radius:\s*\d+px)"));
    const QString replacement = QStringLiteral("border-radius: %1px").arg(current().radius);
    css.replace(re, replacement);
    return css;
}

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

} // namespace ThemeManager
