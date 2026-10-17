cmake_minimum_required(VERSION 3.19)
# VERSION 是必需的：macOS 的 bundle 会用它填 CFBundleVersion /
# CFBundleShortVersionString（见下面 APPLE 分支），没有的话那两个字段是空的。
project(cysc-taskbar VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql)

qt_standard_project_setup()

option(CYSC_BUILD_TESTS "Build the CYSC TaskBar unit tests" ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Files shared by the application and the test runner: the pure containers, the
# model layer and the SQLite persistence layer (no widgets).
set(CYSC_CORE_SOURCES
    src/ds/Trie.cpp
    src/ds/Trie.h
    src/ds/Vector.h
    src/ds/Stack.h
    src/ds/HashTable.h
    src/ds/SegmentTree.h
    src/ds/UnionFind.h
    src/models/Task.h
    src/models/TaskQuery.h
    src/models/TaskDraft.cpp
    src/models/TaskDraft.h
    src/models/TaskDocument.cpp
    src/models/TaskDocument.h
    src/models/TaskModel.cpp
    src/models/TaskModel.h
    src/core/DatabaseManager.cpp
    src/core/DatabaseManager.h
    src/core/DocumentStore.cpp
    src/core/DocumentStore.h
    src/core/StoragePaths.cpp
    src/core/StoragePaths.h
    src/ui/Theme.cpp
    src/ui/Theme.h
)

set(CYSC_APP_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    src/demo/ds_demo.cpp
    src/demo/ds_demo.h
    src/widgets/TaskDialog.cpp
    src/widgets/TaskDialog.h
    src/widgets/TaskGroupDialog.cpp
    src/widgets/TaskGroupDialog.h
    src/widgets/TaskSettingsDialog.cpp
    src/widgets/TaskSettingsDialog.h
    src/widgets/TaskSidebar.cpp
    src/widgets/TaskSidebar.h
    src/widgets/TaskVisualizer.cpp
    src/widgets/TaskVisualizer.h
    src/widgets/TaskWizardDialog.cpp
    src/widgets/TaskWizardDialog.h
)

qt_add_executable(cysc-taskbar
    WIN32 MACOSX_BUNDLE
    ${CYSC_CORE_SOURCES}
    ${CYSC_APP_SOURCES}
)

# ---- 应用图标 ----
# Windows：把 resources/app.rc（内含 app.ico）编进去，否则 exe 显示系统默认图标。
# macOS：同一个 .ico 换不出 .icns，需要在 Mac 上用 iconutil 转；这里先挂上
#        Info.plist（见下面的 MACOSX_BUNDLE_INFO_PLIST），图标转换步骤写在
#        docs/打包说明.md 里。
if(WIN32)
    enable_language(RC)
    target_sources(cysc-taskbar PRIVATE resources/app.rc)
    # ⚠ 这里**故意不把 OUTPUT_NAME 设成中文**。
    # Qt Creator 用 jom 构建，而 jom 在中文代码页下处理不了非 ASCII 文件名：
    # 实测改成 "CYSC任务管理" 后，jom 传给 link.exe 的 /out: 参数里文件名
    # 变成乱码（CYSC????????????.exe），链接直接 LNK1104 失败。
    # 所以构建产物保持 ASCII 名，**交付时由 scripts\package.bat 重命名**。
endif()

if(APPLE)
    set_target_properties(cysc-taskbar PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "CYSC 任务栏"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cysc.taskbar"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
    )
endif()

target_include_directories(cysc-taskbar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cysc-taskbar
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Sql
)

install(TARGETS cysc-taskbar
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET cysc-taskbar
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ---------------------------------------------------------------------------
#  Unit tests: a self-contained console runner registered with CTest.
# ---------------------------------------------------------------------------
if(CYSC_BUILD_TESTS)
    enable_testing()

    add_executable(cysc_tests
        tests/test_main.cpp
        ${CYSC_CORE_SOURCES}
    )
    target_include_directories(cysc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(cysc_tests PRIVATE Qt::Core Qt::Gui Qt::Sql)

    # ⚠ 这里必须用 $<TARGET_FILE:...> 而不是裸目标名。
    #
    # 为什么：在 Windows 上可执行文件就在 <构建目录>/cysc-taskbar.exe，
    # CTest 按名字直接找得到，所以裸名字一直"能跑"。
    # 但在 macOS 上目标是 app bundle，可执行文件在
    #   <构建目录>/cysc-taskbar.app/Contents/MacOS/cysc-taskbar
    # CTest 在构建目录下找不到叫 "cysc-taskbar" 的文件 —— 于是三个测试
    # 全部报 ***Not Run，总耗时 0.00 秒（一个都没启动）。
    #
    # 这个坑很隐蔽：构建是成功的、日志里能看到 .app 产物，
    # 崩的却是"跑测试"这一步，而且报错只是 Not Run，看不出是路径问题。
    #
    # $<TARGET_FILE:...> 由 CMake 在生成期展开成该平台真实的产物路径，
    # Windows 给 .exe、macOS 给 bundle 内的可执行文件，两边都对。
    add_test(NAME cysc_unit_tests COMMAND $<TARGET_FILE:cysc_tests>)

    # 界面自检：构建真实控件（offscreen）并驱动列表交互。
    add_test(NAME cysc_gui_tests COMMAND $<TARGET_FILE:cysc-taskbar> --guitest)

    # 数据结构演示：可现场演示，同时作为回归（演示项全部为真断言）。
    add_test(NAME cysc_ds_demo COMMAND $<TARGET_FILE:cysc-taskbar> --dsdemo)

    # ctest launches the executables directly, so they must be told where Qt's
    # runtime and plugins live (the SQLite driver is a plugin). Qt6_DIR is
    # "<prefix>/lib/cmake/Qt6", so the prefix is three levels up.
    get_filename_component(CYSC_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    set(CYSC_QT_PLUGIN_DIR "${CYSC_QT_PREFIX}/plugins")
    if(NOT EXISTS "${CYSC_QT_PLUGIN_DIR}")
        set(CYSC_QT_PLUGIN_DIR "")
    endif()

    set(CYSC_TEST_ENV "PATH=${CYSC_QT_PREFIX}/bin;$ENV{PATH};QT_PLUGIN_PATH=${CYSC_QT_PLUGIN_DIR};QT_QPA_PLATFORM=offscreen;CYSC_TEST_TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/cysc_test_tmp")

    set_tests_properties(cysc_unit_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "${CYSC_TEST_ENV}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(cysc_gui_tests PROPERTIES
        # 界面自检在 Debug 下要重建几百次列表，本来就慢（Release 十几秒，
        # Debug 两分钟上下）。给足余量，免得在慢机器/CI 上被误判为失败。
        TIMEOUT 300
        ENVIRONMENT "${CYSC_TEST_ENV}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(cysc_ds_demo PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "${CYSC_TEST_ENV}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
