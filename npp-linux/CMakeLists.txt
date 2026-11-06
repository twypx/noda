cmake_minimum_required(VERSION 3.20)
project(NotepadPlusPlus_Linux LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启高性能编译参数与链接时优化。
# 不使用 -march=native：它会按构建机 CPU 生成专有指令集，换机运行会 SIGILL；
# 改用 -mtune=generic（仅调度优化、指令集保持通用基线），既便于分发又不损失单核吞吐。
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mtune=generic -flto -DNDEBUG")

# GCC/Clang 兼容性：确保 <cstdint> 等基础类型头文件在编译时可见
add_compile_options(-include cstdint)

# 寻找 Qt6 (优先) 或 Qt5；PrintSupport 供“文件 > 打印”使用
find_package(Qt6 COMPONENTS Core Gui Widgets PrintSupport)
if (Qt6_FOUND)
    if (NOT TARGET Qt6::PrintSupport)
        message(FATAL_ERROR
            "缺少 Qt6::PrintSupport（打印功能需要它）。\n"
            "请安装: sudo apt install qt6-base-dev（或对应发行版的 qt6-printsupport 开发包）。")
    endif()
    message(STATUS "Found Qt6: version ${Qt6_VERSION}")
    set(QT_TARGETS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets PrintSupport REQUIRED)
    message(STATUS "Found Qt5: version ${Qt5_VERSION}")
    set(QT_TARGETS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport)
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
    src/utils/EncodingConverter.cpp
    src/widgets/TitleBar.cpp
    src/dialogs/FindReplaceDialog.cpp
    src/dialogs/GoToLineDialog.cpp
    src/dialogs/ColumnEditorDialog.cpp
    src/dialogs/DocSummaryDialog.cpp
    src/dialogs/PreferencesDialog.cpp
    src/dialogs/ShortcutsDialog.cpp
    src/dialogs/StyleConfigDialog.cpp
    src/utils/ThemeManager.cpp
    src/utils/PrintHelper.cpp
    src/utils/ToolbarIcon.cpp
)

add_executable(notepad-plus-plus-linux
    ${APP_SOURCES}
    resources/app.qrc
    resources/toolbar.qrc
)

# 自动将 langs.model.xml 和 stylers.model.xml 复制到构建输出目录，保证开箱即用
add_custom_command(TARGET notepad-plus-plus-linux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${POWEREDITOR_SRC}/langs.model.xml"
        "${CMAKE_CURRENT_BINARY_DIR}/langs.model.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${POWEREDITOR_SRC}/stylers.model.xml"
        "${CMAKE_CURRENT_BINARY_DIR}/stylers.model.xml"
)

target_link_libraries(notepad-plus-plus-linux PRIVATE
    ${QT_TARGETS}
    scintilla_qt
    lexilla
    uchardet
    pugixml
)

message(STATUS "Notepad++ (Linux Port) CMake configuration ready.")

# 安装规则：使 main.cpp 中 applicationDirPath() 相对定位能直接命中 XML，开箱即用
include(GNUInstallDirs)
install(TARGETS notepad-plus-plus-linux RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    "${POWEREDITOR_SRC}/langs.model.xml"
    "${POWEREDITOR_SRC}/stylers.model.xml"
    DESTINATION ${CMAKE_INSTALL_BINDIR})

# 应用图标与桌面入口：图标名与 .desktop 文件名一致，GNOME 才能把任务栏悬停提示关联到本窗口
set(NPP_ICON_NAME "notepad-plus-plus-linux")
install(DIRECTORY resources/icons/hicolor/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        FILES_MATCHING PATTERN "*.png")

# 用安装后的绝对路径生成 .desktop（GNOME 需要 Exec 为绝对路径或 PATH 中的可执行名）
set(NPP_APP_EXE "${CMAKE_INSTALL_FULL_BINDIR}/${NPP_ICON_NAME}")
configure_file(resources/${NPP_ICON_NAME}.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/${NPP_ICON_NAME}.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${NPP_ICON_NAME}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

