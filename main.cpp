#include "mainwindow.h"
#include "src/ui/Theme.h"
#include "src/demo/ds_demo.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFont>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

namespace {

// The headless self-test must run without a windowing system: on non-Windows
// hosts force the offscreen Qt platform plugin before QApplication exists.
void selectHeadlessPlatformIfNeeded() {
#if !defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
#endif
}

} // namespace

// 诊断用：WIN32 子系统没有可用的 stderr，设了 CYSC_LOG_FILE 时把 qInfo 等
// 消息追加到文件里（排查鼠标/焦点这类只在真机出现的问题时很有用）。
static void fileMessageHandler(QtMsgType, const QMessageLogContext &, const QString &msg) {
    QFile f(qEnvironmentVariable("CYSC_LOG_FILE"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(msg.toUtf8());
        f.write("\n");
    }
}

int main(int argc, char *argv[]) {
    // Peek at the arguments before constructing the application, so the
    // platform plugin can be chosen for the headless runs.
    bool selfTest = false;
    bool guiTest = false;
    bool dsDemo = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selfTest = true;
        } else if (qstrcmp(argv[i], "--guitest") == 0) {
            guiTest = true;
            selfTest = true;      // 界面自检在模型自检之后继续跑
        } else if (qstrcmp(argv[i], "--dsdemo") == 0) {
            dsDemo = true;
        }
    }
    // 数据结构演示只用模型层，不需要窗口系统。
    if (dsDemo) {
        selectHeadlessPlatformIfNeeded();
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
        }
    }
    if (selfTest) {
        selectHeadlessPlatformIfNeeded();
        if (guiTest) {
            // 界面自检需要真正创建控件，但不必显示到屏幕上。
            qputenv("CYSC_GUITEST", QByteArray("1"));
            if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
                qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
            }
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("CYSC TaskBar");
    QApplication::setOrganizationName("CYSC");
    QApplication::setApplicationVersion("0.1.0");

    // Base font for every widget. The first family that exists is used, so the
    // list degrades gracefully on machines without 微软雅黑。
    //
    // 必须包含各平台自带的中文字体：
    //   Windows  -> Microsoft YaHei UI / Microsoft YaHei
    //   macOS    -> PingFang SC（苹方，10.11 起系统自带）/ Heiti SC
    //   Linux    -> Noto Sans SC
    // 漏掉 macOS 那一档的话，Mac 上会选到一个**不含中文字形**的西文字体，
    // 整个界面中文全变方块。
    QFont baseFont = app.font();
    baseFont.setFamilies({"Microsoft YaHei UI", "Microsoft YaHei",
                          "PingFang SC", "Heiti SC",
                          "Noto Sans SC", "SimSun"});
    baseFont.setPointSizeF(9.0);
    app.setFont(baseFont);

    if (dsDemo) {
        return runDsDemo();
    }

    if (selfTest) {
        return MainWindow::runSelfTest();
    }

    if (!qEnvironmentVariable("CYSC_LOG_FILE").isEmpty()) {
        qInstallMessageHandler(fileMessageHandler);
    }

    MainWindow w;
    w.show();

    // 便于检查/截图：把三款主题各渲染一张图，文件名 <前缀>_<主题key>.png。
    // 用法：--theme-render <前缀>
    {
        const int tri = app.arguments().indexOf("--theme-render");
        if (tri >= 0 && tri + 1 < app.arguments().size()) {
            const QString prefix = app.arguments().at(tri + 1);
            int saved = 0;
            for (int i = 0; i < ThemeManager::count(); ++i) {
                ThemeManager::setCurrentIndex(i);
                w.applyThemeToWidgets();
                QCoreApplication::processEvents();
                const QString path = QString("%1_%2.png")
                                         .arg(prefix, ThemeManager::current().key);
                if (w.renderToImage(path) > 0) ++saved;
            }
            QTextStream out(stdout);
            out << "已渲染 " << saved << " 张主题图，前缀：" << prefix << "\n";
            out.flush();
            return saved > 0 ? 0 : 1;
        }
    }

    // 便于检查/截图：指定主题启动。用法：--theme sky|sunset|rose
    // 放在 MainWindow 构造之后只是为了让截图脚本简单；正常路径是走设置里的。
    {
        const int ti = app.arguments().indexOf("--theme");
        if (ti >= 0 && ti + 1 < app.arguments().size()) {
            if (ThemeManager::setCurrentByKey(app.arguments().at(ti + 1))) {
                w.applyThemeToWidgets();
            }
        }
    }

    // 便于检查/截图内联详情编辑区：启动就展开第一条任务。
    if (app.arguments().contains("--expand-first")) {
        w.expandFirstVisibleTask();
    }

    // 便于检查/截图逐栏向导：启动就打开新建窗口。
    if (app.arguments().contains("--wizard")) {
        w.openNewTaskWizard();
    }

    // 演示用：启动就打开数据结构可视化面板。
    // --visualize-tab N 会额外切到第 N 个页签（0=哈希 1=Trie 2=线段树 3=并查集）。
    const int vizTabIdx = app.arguments().indexOf("--visualize-tab");
    if (vizTabIdx >= 0 && vizTabIdx + 1 < app.arguments().size()) {
        w.openVisualizerTab(app.arguments().at(vizTabIdx + 1).toInt());
    } else if (app.arguments().contains("--visualize")) {
        w.openVisualizer();
    }

    // 把可视化面板的四个页签导出成 PNG（使用文档插图用）。
    // 用法：--visualize-render <输出前缀>
    const QStringList args = app.arguments();
    const int renderIdx = args.indexOf("--visualize-render");
    if (renderIdx >= 0 && renderIdx + 1 < args.size()) {
        const QString prefix = args.at(renderIdx + 1);
        const int saved = w.exportVisualizerImages(prefix);
        QTextStream out(stdout);
        out << "已导出 " << saved << " 张可视化图片，前缀：" << prefix << "\n";
        out.flush();
        return saved > 0 ? 0 : 1;
    }

    // 让程序自己把窗口渲染成图片（不依赖抓屏与图片格式插件）。
    // 用法：--render-window <输出文件>
    const int renderWinIdx = args.indexOf("--render-window");
    if (renderWinIdx >= 0 && renderWinIdx + 1 < args.size()) {
        const int saved = w.renderToImage(args.at(renderWinIdx + 1));
        QTextStream out(stdout);
        out << (saved > 0 ? "窗口已渲染成图片\n" : "渲染失败\n");
        out.flush();
        return saved > 0 ? 0 : 1;
    }

    // 诊断用：强制假定托盘可用。用于在没有通知区域的会话里验证
    // 「关窗 -> 收进托盘」这条路径本身是否正常。
    if (args.contains("--force-tray")) {
        qputenv("CYSC_FORCE_TRAY", "1");
    }

    // 偏好设置（目前只有「是否置顶」）存成 INI 文件，而不是默认的注册表。
    //
    // 为什么不用注册表：QSettings 在 Windows 上默认写 HKCU\Software\<组织>\<应用>，
    // 在受限环境或 32/64 位注册表视图不一致时会出现「写入返回成功、读回却是空的」，
    // 很难排查。INI 文件路径明确、可直接查看，也便于测试指向临时目录。
    {
        QString settingsPath = qEnvironmentVariable("CYSC_SETTINGS_PATH");
        if (settingsPath.isEmpty()) {
            settingsPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        }
        if (!settingsPath.isEmpty()) {
            QDir().mkpath(settingsPath);
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
        }
    }

    // 便于人工检查/截图：直接打开「选择列表颜色」对话框。
    if (args.contains("--color-dialog")) {
        QString color = "#3D6BFF";
        w.pickListColor(color);
        return 0;
    }

    return app.exec();
}
