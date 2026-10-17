#ifndef TASKSETTINGSDIALOG_H
#define TASKSETTINGSDIALOG_H

// ============================================================================
//  TaskSettingsDialog.h
//  职责：设置窗口（多功能自定义项，持久化到 INI 配置）
//  依赖：Qt Widgets
// ============================================================================

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

// 「设置」窗口。
//
// 设置项都存在 QSettings（INI 文件，见 main.cpp 里的 setPath）里，
// 主窗口读这些键决定行为。这个类自己不碰主窗口，只负责编辑与落盘，
// 所以主窗口只是「打开它 → 如果被接受就重新应用一遍设置」。
class TaskSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskSettingsDialog(QWidget *parent = nullptr);

    // 从 QSettings 读进控件。
    void loadFromSettings();
    // 把控件里的值写回 QSettings（accept 时调用）。
    void saveToSettings();

    // ---- 设置项的键名与默认值（主窗口也用这些，避免两边写错字符串）----
    static const char *kHideDone;       // 默认隐藏已完成
    static const char *kSinkDone;       // 已完成自动置底
    static const char *kGlow;           // 进行中任务边框流动光效
    static const char *kCompact;        // 紧凑模式
    static const char *kOnTop;          // 窗口置顶
    static const char *kDensity;        // 行密度：0 紧凑 / 1 标准 / 2 宽松
    static const char *kScale;          // 界面缩放档位：0=100% 1=125% 2=150% 3=175% 4=200%
    static const char *kConfirmDelete;  // 删除任务前确认

    static bool boolValue(const char *key, bool fallback);
    static int intValue(const char *key, int fallback);

protected:
    void accept() override;

private:
    QCheckBox *m_hideDone = nullptr;
    QCheckBox *m_sinkDone = nullptr;
    QCheckBox *m_glow = nullptr;
    QCheckBox *m_compact = nullptr;
    QCheckBox *m_onTop = nullptr;
    QCheckBox *m_confirmDelete = nullptr;
    QComboBox *m_density = nullptr;
    QComboBox *m_scale = nullptr;
    QComboBox *m_theme = nullptr;

public:
    // 设置界面里选中的主题索引（主窗口据此重绘）。
    int selectedThemeIndex() const;
    // 设置界面里选中的缩放档位。
    int selectedScaleIndex() const;
};

#endif
