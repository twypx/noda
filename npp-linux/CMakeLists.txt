cmake_minimum_required(VERSION 3.20)
project(Noda
    VERSION 1.0.20
    LANGUAGES CXX C)

add_compile_definitions(NPP_VERSION_STRING="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启高性能编译参数与链接时优化。
# 不使用 -march=native：它会按构建机 CPU 生成专有指令集，换机运行会 SIGILL；
# 改用 -mtune=generic（仅调度优化、指令集保持通用基线），既便于分发又不损失单核吞吐。
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mtune=generic -flto=auto -DNDEBUG")

# GCC/Clang 兼容性：确保 <cstdint> 等基础类型头文件在编译时可见
add_compile_options(-include cstdint)

# 寻找 Qt6 (优先) 或 Qt5；PrintSupport 供“文件 > 打印”使用，Network 供单实例进程间通信使用
find_package(Qt6 COMPONENTS Core Gui Widgets PrintSupport Concurrent Network)
if (Qt6_FOUND)
    if (NOT TARGET Qt6::PrintSupport)
        message(FATAL_ERROR
            "缺少 Qt6::PrintSupport（打印功能需要它）。\n"
            "请安装: sudo apt install qt6-base-dev（或对应发行版的 qt6-printsupport 开发包）。")
    endif()
    message(STATUS "Found Qt6: version ${Qt6_VERSION}")
    set(QT_TARGETS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Concurrent Qt6::Network)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets PrintSupport Concurrent Network REQUIRED)
    message(STATUS "Found Qt5: version ${Qt5_VERSION}")
    set(QT_TARGETS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport Qt5::Concurrent Qt5::Network)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

# 源码与头文件路径
set(NPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SCINTILLA_DIR ${NPP_ROOT}/scintilla)
set(LEXILLA_DIR ${NPP_ROOT}/lexilla)
set(POWEREDITOR_SRC ${NPP_ROOT}/PowerEditor/src)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${SCINTILLA_DIR}/include
    ${SCINTILLA_DIR}/src
    ${SCINTILLA_DIR}/qt/ScintillaEditBase
    ${LEXILLA_DIR}/include
    ${LEXILLA_DIR}/lexlib
    ${POWEREDITOR_SRC}/uchardet
    ${POWEREDITOR_SRC}/pugixml
)

# 1. 编译 uchardet 编码识别模块
file(GLOB UCHARDET_SOURCES "${POWEREDITOR_SRC}/uchardet/*.cpp")
add_library(uchardet STATIC ${UCHARDET_SOURCES})

# 2. 编译 pugixml 模块
add_library(pugixml STATIC "${POWEREDITOR_SRC}/pugixml/pugixml.cpp")

# 2.5 编译 AStyle 代码格式化核心（third_party/astyle，MIT）。
# 只取 headless 的 ASTYLE_LIB 引擎，支持 C/C++/C#/Java/Objective-C，无 Qt/UI 依赖；
# 刻意不引入 CoolFormat 的其余引擎（PHP 引擎有二次方拷贝，违背高性能目标）。
# 引擎按 C++11 编译（与其上游一致），应用侧通过 utils/CodeFormatter 在工作线程调用。
set(ASTYLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/astyle)
add_library(astyle STATIC
    ${ASTYLE_DIR}/ASBeautifier.cpp
    ${ASTYLE_DIR}/ASEnhancer.cpp
    ${ASTYLE_DIR}/ASFormatter.cpp
    ${ASTYLE_DIR}/ASResource.cpp
    ${ASTYLE_DIR}/astyle_main.cpp
)
target_compile_definitions(astyle PRIVATE ASTYLE_LIB)
target_include_directories(astyle PRIVATE ${ASTYLE_DIR})
set_target_properties(astyle PROPERTIES CXX_STANDARD 11)

# 3. 编译 Lexilla 词法高亮引擎
file(GLOB LEXILLA_LEXLIB_SOURCES "${LEXILLA_DIR}/lexlib/*.cxx")
file(GLOB LEXILLA_LEXERS_SOURCES "${LEXILLA_DIR}/lexers/*.cxx")
add_library(lexilla STATIC
    "${LEXILLA_DIR}/src/Lexilla.cxx"
    ${LEXILLA_LEXLIB_SOURCES}
    ${LEXILLA_LEXERS_SOURCES}
)

# 4. 编译 Scintilla Qt 基础控件
file(GLOB SCINTILLA_CORE_SOURCES "${SCINTILLA_DIR}/src/*.cxx")
set(SCINTILLA_QT_SOURCES
    ${SCINTILLA_DIR}/qt/ScintillaEditBase/PlatQt.cpp
    ${SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaEditBase.cpp
    ${SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaQt.cpp
)

add_library(scintilla_qt STATIC
    ${SCINTILLA_CORE_SOURCES}
    ${SCINTILLA_QT_SOURCES}
)
target_link_libraries(scintilla_qt PUBLIC ${QT_TARGETS})

# 5. 应用程序主目标
# 应用图标 PNG 由 tools/gen_icons.py 把 notepad++.png 处理成 iOS 风格（超椭圆 + 绿色渐变底）
# 后生成并随仓库提交；若被误删，配置阶段自动重生成，保证下面的 qrc 不会因缺文件而编译失败。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_256.png")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_icons.py"
            RESULT_VARIABLE NPP_ICON_GEN_RC)
        message(STATUS "重新生成应用图标，返回码=${NPP_ICON_GEN_RC}")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_256.png")
        message(FATAL_ERROR
            "缺少 resources/icons/app_*.png，且无法自动生成（需 python3-pil）。\n"
            "请手动执行: sudo apt install python3-pil && python3 tools/gen_icons.py")
    endif()
endif()

# 工具栏动作图标：源 SVG（Lucide 形态，ISC）已固化在 resources/icons/toolbar/src，
# PNG 与 toolbar.qrc 清单由 tools/gen_toolbar_icons.py 离线生成，运行时不依赖 Qt6Svg。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/toolbar/dark/save_16.png")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_toolbar_icons.py"
            RESULT_VARIABLE NPP_TB_ICON_GEN_RC)
        message(STATUS "重新生成工具栏图标，返回码=${NPP_TB_ICON_GEN_RC}")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/toolbar/dark/save_16.png")
        message(FATAL_ERROR
            "缺少 resources/icons/toolbar/{dark,light}/*.png，且无法自动生成（需 python3-gi + cairo + librsvg）。\n"
            "请手动执行: python3 tools/gen_toolbar_icons.py")
    endif()
endif()

set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/TabManager.cpp
    src/EditorView.cpp
    src/DocumentBuffer.cpp
    src/StyleManager.cpp
    src/SessionManager.cpp
    src/utils/EncodingConverter.cpp
    src/widgets/TitleBar.cpp
    src/widgets/FolderTreePane.cpp
    src/widgets/HexView.cpp
    src/widgets/CompareWidget.cpp
    src/widgets/DiffMiniMap.cpp
    src/dialogs/FindReplaceDialog.cpp
    src/dialogs/GoToLineDialog.cpp
    src/dialogs/ColumnEditorDialog.cpp
    src/dialogs/DocSummaryDialog.cpp
    src/dialogs/AboutDlg.cpp
    src/dialogs/PreferencesDialog.cpp
    src/dialogs/ShortcutsDialog.cpp
    src/dialogs/ShortcutManager.cpp
    src/dialogs/StyleConfigDialog.cpp
    src/dialogs/StyleOverride.cpp
    src/utils/ThemeManager.cpp
    src/utils/LocalizationManager.cpp
    src/utils/PrintHelper.cpp
    src/utils/ToolbarIcon.cpp
    src/utils/CodeFormatter.cpp
    src/utils/JsonFormatter.cpp
    src/utils/StartupNotify.cpp
    src/utils/SingleInstance.cpp
    src/utils/DiffEngine.cpp
    src/utils/ConfigMigration.cpp
)

# 主程序与回归测试复用同一批应用对象文件：抽成 OBJECT 库，避免把 22 个 .cpp 编两遍。
# main.cpp 不进库，测试可执行文件才能提供自己的入口。
set(CORE_SOURCES ${APP_SOURCES})
list(REMOVE_ITEM CORE_SOURCES src/main.cpp)
add_library(npp_core OBJECT ${CORE_SOURCES})
# npp_core 含 CodeFormatter.cpp / JsonFormatter.cpp，二者引用 AStyle 符号。
# OBJECT 库本身不产生链接步骤，PUBLIC 只传递使用需求；真正链接 astyle 的仍是两个可执行目标。
target_link_libraries(npp_core PUBLIC
    ${QT_TARGETS}
    scintilla_qt
    lexilla
    uchardet
    pugixml
    astyle
)

add_executable(noda
    src/main.cpp
    resources/app.qrc
    resources/toolbar.qrc
    $<TARGET_OBJECTS:npp_core>
)

# 语言/主题模型 XML 必须在 configure 阶段就落到构建目录。
# 测试目标 npp_tests 的 loadRealConfigs() 从 applicationDirPath() 读取它们；
# 若只挂在 noda POST_BUILD，CI 只构建 npp_tests 时这两个文件就永远缺失，
# 会让 8 个依赖真实配置的用例失败（本地构建过 noda 时会被偶然掩盖）。
# configure_file(COPYONLY) 还会在上游 XML 变动时自动触发重新配置。
configure_file("${POWEREDITOR_SRC}/langs.model.xml"
               "${CMAKE_CURRENT_BINARY_DIR}/langs.model.xml" COPYONLY)
configure_file("${POWEREDITOR_SRC}/stylers.model.xml"
               "${CMAKE_CURRENT_BINARY_DIR}/stylers.model.xml" COPYONLY)

target_link_libraries(noda PRIVATE
    ${QT_TARGETS}
    scintilla_qt
    lexilla
    uchardet
    pugixml
    astyle
)

# X11 启动通知（把 _NET_STARTUP_ID 写到主窗口上，消除任务栏转圈）需要直接用 libxcb。
# 做成可选：没有 libxcb1-dev 时只是这个补漏不生效（行为退回 Qt 原生那套），
# 不能让一个体验细节把整个工程卡住，Wayland/mac 等平台上也根本没有 xcb。
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET xcb)
endif()
if(XCB_FOUND)
    # StartupNotify.cpp 现在编进 npp_core，条件编译宏必须打在真正编译它的目标上；
    # 链接留给最终的可执行文件（OBJECT 库本身不产生链接步骤）。
    target_compile_definitions(npp_core PRIVATE NPP_HAVE_XCB=1)
    target_include_directories(npp_core PRIVATE ${XCB_INCLUDE_DIRS})
    target_compile_definitions(noda PRIVATE NPP_HAVE_XCB=1)
    target_link_libraries(noda PRIVATE ${XCB_LIBRARIES})
    message(STATUS "找到 libxcb（${XCB_VERSION}）：启用 X11 启动通知补漏")
else()
    message(STATUS "未找到 libxcb（可装 libxcb1-dev）：跳过 X11 启动通知补漏")
endif()

message(STATUS "Noda CMake configuration ready.")

# 包名/安装目录名。Debian 包名只允许小写字母、数字和连字符，这里同时用作
# 图标名与 .desktop 文件名，三者必须一致 GNOME 才能把窗口关联到桌面入口。
set(NPP_PKG_NAME "noda")

# 安装规则：XML 装进 share 而非 bin（bin 下只放可执行文件）。main.cpp 通过
# applicationDirPath()/../share/noda 相对定位，因此无论前缀是
# /usr、/usr/local 还是解包到临时目录，都能命中，不依赖任何编译期绝对路径。
include(GNUInstallDirs)
install(TARGETS noda RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    "${POWEREDITOR_SRC}/langs.model.xml"
    "${POWEREDITOR_SRC}/stylers.model.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${NPP_PKG_NAME})

# 官方主题包（直接取自上游源码 PowerEditor/installer/themes）：
# 构建目录留一份供运行/测试直接命中，安装时进 share/noda/themes。
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/themes"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/themes/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${NPP_PKG_NAME}/themes)

# 官方中英文界面语言资源（PowerEditor/installer/nativeLang）。
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/nativeLang"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nativeLang/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${NPP_PKG_NAME}/nativeLang)

# 应用图标与桌面入口：图标名与 .desktop 文件名一致，GNOME 才能把任务栏悬停提示关联到本窗口
set(NPP_ICON_NAME "noda")
install(DIRECTORY resources/icons/hicolor/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        FILES_MATCHING PATTERN "*.png")

# 用安装后的绝对路径生成 .desktop（GNOME 需要 Exec 为绝对路径或 PATH 中的可执行名）
set(NPP_APP_EXE "${CMAKE_INSTALL_FULL_BINDIR}/${NPP_ICON_NAME}")
configure_file(resources/${NPP_ICON_NAME}.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/${NPP_ICON_NAME}.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${NPP_ICON_NAME}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Debian 规范要求 /usr/share/doc/<pkg>/copyright。不要指望 CPACK_RESOURCE_FILE_LICENSE
# 自动把这个文件装进去 —— 实测 DEB 生成器并不落地，所以这里显式装一份。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${NPP_PKG_NAME}
            RENAME copyright)
endif()

# ---------------------------------------------------------------------------
# Debian 打包（CPack DEB 生成器）
#
# 打包必须用 -DCMAKE_INSTALL_PREFIX=/usr 配置一次：上面 .desktop 里的 Exec 是
# configure 阶段按 CMAKE_INSTALL_FULL_BINDIR 固化的绝对路径，若沿用默认前缀
# /usr/local，打出来的包会写 Exec=/usr/local/bin/... 而文件实际装在 /usr/bin，
# 装完点图标毫无反应。build_deb.sh 已按正确前缀调用。
# ---------------------------------------------------------------------------
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "${NPP_PKG_NAME}")
set(CPACK_PACKAGE_VENDOR "twypx")
set(CPACK_PACKAGE_CONTACT "twypx <twypx_1986@sina.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Noda: native Qt6 Widgets text editor with the Scintilla engine")
# 仓库地址保持真实位置，不随产品名改写，避免链接失效。
set(CPACK_PACKAGE_HOMEPAGE_URL "https://gitee.com/twypx/notepad-plus-plus-linux")

# 按 Debian 政策装到 /usr，而不是 CMake 默认的 /usr/local
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Depends 交给 dpkg-shlibdeps 按真实链接关系生成，不手写：Ubuntu 24.04 的 Qt6 包名
# 带 t64 后缀（libqt6gui6t64 等），手写极容易在别的发行版上直接依赖不上。
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# X11 的 libqxcb.so 就在 libqt6gui6t64 里，无需额外包；但 Wayland 会话用的
# libqwayland-*.so 单独放在 qt6-wayland，缺了它 Qt 只能退回 XWayland。
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")

# DEB 生成器会把这份许可文本装到 /usr/share/doc/<pkg>/copyright
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
endif()

# 安装/卸载后刷新图标缓存与 desktop 数据库：CPack 不像 debhelper 那样生成 dpkg
# 触发器，不主动刷的话 GNOME 应用列表里可能一时看不到图标。
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/resources/deb/postrm")

include(CPack)

# ---------------------------------------------------------------------------
# 回归测试（Qt Test）
#
# 默认关闭，不干扰 build.sh / build_deb.sh / CPack。tests/run_tests.sh 会显式打开。
# 测试进程用 offscreen 平台插件，并把 XDG_CONFIG_HOME 指到构建目录下的 test-config，
# 避免 QSettings 写坏真实用户配置。
# ---------------------------------------------------------------------------
option(NPP_BUILD_TESTS "Build the Qt Test regression suite" OFF)
if(NPP_BUILD_TESTS)
    if(Qt6_FOUND)
        find_package(Qt6 COMPONENTS Test REQUIRED)
        set(NPP_TEST_TARGET Qt6::Test)
    else()
        find_package(Qt5 COMPONENTS Test REQUIRED)
        set(NPP_TEST_TARGET Qt5::Test)
    endif()

    enable_testing()
    file(GLOB NPP_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
    file(GLOB NPP_TEST_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")
    add_executable(npp_tests
        ${NPP_TEST_SOURCES}
        ${NPP_TEST_HEADERS}
        resources/toolbar.qrc
        $<TARGET_OBJECTS:npp_core>
    )
    target_include_directories(npp_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(npp_tests PRIVATE
        ${QT_TARGETS}
        ${NPP_TEST_TARGET}
        scintilla_qt
        lexilla
        uchardet
        pugixml
        astyle
    )
    if(XCB_FOUND)
        target_link_libraries(npp_tests PRIVATE ${XCB_LIBRARIES})
    endif()

    add_test(NAME npp_tests COMMAND npp_tests)
    set_tests_properties(npp_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
        TIMEOUT 180)
    message(STATUS "回归测试已启用：npp_tests（ctest --test-dir build --output-on-failure）")
endif()

