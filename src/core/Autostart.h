#ifndef AUTOSTART_H
#define AUTOSTART_H

// ============================================================================
//  Autostart.h
//  职责：开机自启的登记与查询（Windows 注册表 Run 键 / macOS LaunchAgents）
//  依赖：QtCore
// ============================================================================

#include <QString>

// 开机自启（F-001）。
//
// 为什么需要它：到点提醒依赖进程在跑。不随开机启动的话，用户不开程序就收不到
// 任何提醒 —— 提醒功能等于半残。默认**关闭**，只有用户在设置里勾选后才登记。
//
// 设计上把「查系统状态」和「我们记的偏好」分开：isEnabled() 一律读系统真实状态
// （注册表项 / plist 文件在不在），不读配置文件。否则用户手动删掉注册表项之后，
// 设置窗口还会显示"已开启"，与实际不符。
namespace Autostart {

// 当前平台是否支持（Windows / macOS 支持，其它平台返回 false）。
bool isSupported();

// 现在是否已登记开机自启。
bool isEnabled();

// 登记/取消登记。返回 false 表示写系统失败（权限、只读 FS 等）。
bool setEnabled(bool on);

// 登记到系统里的命令行：带引号的可执行文件路径。
QString commandLine();

// 测试用：把系统存储换成指定路径（Windows 用 INI 文件代替注册表，
// macOS 用这个路径代替 LaunchAgents 下的 plist）。
// 传空字符串恢复真实系统存储。--guitest 绝不该动用户真正的注册表。
void setStorageOverrideForTests(const QString &path);

// 当前生效的存储路径（测试用来断言写到了哪里；真实系统存储返回空）。
QString storageOverrideForTests();

} // namespace Autostart

#endif
