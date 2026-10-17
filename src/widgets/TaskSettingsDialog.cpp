#include "TaskSettingsDialog.h"

#include "../ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

const char *TaskSettingsDialog::kHideDone = "view/hideDone";
const char *TaskSettingsDialog::kSinkDone = "view/sinkDone";
const char *TaskSettingsDialog::kGlow = "view/glowRunning";
const char *TaskSettingsDialog::kCompact = "window/compact";
const char *TaskSettingsDialog::kOnTop = "window/onTop";
const char *TaskSettingsDialog::kDensity = "view/density";
const char *TaskSettingsDialog::kScale = "view/scale";
const char *TaskSettingsDialog::kConfirmDelete = "view/confirmDelete";

bool TaskSettingsDialog::boolValue(const char *key, bool fallback) {
    QSettings s;
    return s.value(QString::fromLatin1(key), fallback).toBool();
}

int TaskSettingsDialog::intValue(const char *key, int fallback) {
    QSettings s;
    return s.value(QString::fromLatin1(key), fallback).toInt();
}

TaskSettingsDialog::TaskSettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("设置");
    setModal(true);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setSpacing(8);

    QLabel *themeTitle = new QLabel("主题", this);
    themeTitle->setStyleSheet("QLabel { font-weight: bold; }");
    root->addWidget(themeTitle);

    QFormLayout *themeForm = new QFormLayout();
    m_theme = new QComboBox(this);
    for (int i = 0; i < ThemeManager::count(); ++i) {
        m_theme->addItem(ThemeManager::themes()[i].name);
    }
    m_theme->setToolTip("切换整套配色：晴空（天蓝·白）/ 晚霞（金黄·橙）/ 玫瑰（玫红·浅灰·黑）");
    themeForm->addRow("配色：", m_theme);
    root->addLayout(themeForm);

    QLabel *viewTitle = new QLabel("列表显示", this);
    viewTitle->setStyleSheet("QLabel { font-weight: bold; }");
    root->addWidget(viewTitle);

    m_hideDone = new QCheckBox("默认隐藏已完成的任务", this);
    m_hideDone->setToolTip("勾选后列表里不显示已完成；也可以随时用工具栏的开关临时切换");
    root->addWidget(m_hideDone);

    m_sinkDone = new QCheckBox("已完成的任务自动置底", this);
    m_sinkDone->setToolTip("所有排序方式下都成立；关掉后已完成任务保持它原本的位置");
    root->addWidget(m_sinkDone);

    m_glow = new QCheckBox("「进行中」的任务显示流动光效", this);
    m_glow->setToolTip("边框上有一道绕行的青绿高光，用来一眼看出哪些任务在做");
    root->addWidget(m_glow);

    m_confirmDelete = new QCheckBox("删除任务前弹出确认", this);
    root->addWidget(m_confirmDelete);

    QFormLayout *densityForm = new QFormLayout();
    m_density = new QComboBox(this);
    m_density->addItems({"紧凑", "标准", "宽松"});
    m_density->setToolTip("任务行的高度与内边距");
    densityForm->addRow("行密度：", m_density);

    // 界面缩放：整体放大字号与控件尺寸。
    //
    // 需求原话是"放大到原来的两倍左右"，但硬编码 2 倍会让窗口在 1080p 屏上
    // 超出可用高度，所以做成可调档位、默认 150%。实际是把基准字号按比例放大，
    // Qt 的样式表用的是相对字号，所以文字、行高、控件高度会一起变大；
    // 再按屏幕可用高度封顶，避免放大后窗口装不下。
    m_scale = new QComboBox(this);
    m_scale->addItems({"100%（紧凑）", "125%", "150%（默认）", "175%", "200%（最大）"});
    m_scale->setToolTip("整体放大字号与控件。放大后会按屏幕可用高度自动封顶，"
                        "避免窗口超出屏幕");
    densityForm->addRow("界面缩放：", m_scale);
    root->addLayout(densityForm);

    QLabel *winTitle = new QLabel("窗口", this);
    winTitle->setStyleSheet("QLabel { font-weight: bold; }");
    root->addWidget(winTitle);

    m_onTop = new QCheckBox("窗口置顶（总在最前）", this);
    m_onTop->setToolTip("等同于标题栏的图钉按钮");
    root->addWidget(m_onTop);

    m_compact = new QCheckBox("紧凑模式（只显示任务）", this);
    m_compact->setToolTip("等同于工具栏的 ⊡ 按钮；开启后会吸附到屏幕右上角");
    root->addWidget(m_compact);

    root->addStretch(1);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText("确定");
    box->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(box, &QDialogButtonBox::accepted, this, &TaskSettingsDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &TaskSettingsDialog::reject);
    root->addWidget(box);

    loadFromSettings();
}

void TaskSettingsDialog::loadFromSettings() {
    m_hideDone->setChecked(boolValue(kHideDone, false));
    m_sinkDone->setChecked(boolValue(kSinkDone, true));
    m_glow->setChecked(boolValue(kGlow, true));
    m_confirmDelete->setChecked(boolValue(kConfirmDelete, true));
    m_density->setCurrentIndex(qBound(0, intValue(kDensity, 1), 2));
    m_scale->setCurrentIndex(qBound(0, intValue(kScale, 2), 4));
    m_onTop->setChecked(boolValue(kOnTop, true));
    m_compact->setChecked(boolValue(kCompact, false));
}

void TaskSettingsDialog::saveToSettings() {
    QSettings s;
    s.setValue(kHideDone, m_hideDone->isChecked());
    s.setValue(kSinkDone, m_sinkDone->isChecked());
    s.setValue(kGlow, m_glow->isChecked());
    s.setValue(kConfirmDelete, m_confirmDelete->isChecked());
    s.setValue(kDensity, m_density->currentIndex());
    s.setValue(kScale, m_scale->currentIndex());
    s.setValue(kOnTop, m_onTop->isChecked());
    s.setValue(kCompact, m_compact->isChecked());
    // 主题由 ThemeManager 自己写（它持有 key），这里只切当前索引
    ThemeManager::setCurrentIndex(m_theme->currentIndex());
    ThemeManager::saveToSettings();
    // 立刻落盘：进程被强杀时这些偏好不会丢
    s.sync();
}

int TaskSettingsDialog::selectedScaleIndex() const {
    return m_scale ? m_scale->currentIndex() : 2;
}

int TaskSettingsDialog::selectedThemeIndex() const {
    return m_theme ? m_theme->currentIndex() : 0;
}

void TaskSettingsDialog::accept() {
    saveToSettings();
    QDialog::accept();
}
