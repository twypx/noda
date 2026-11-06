#include "ShortcutsDialog.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <vector>

struct ShortcutInfo {
    const char* category;
    const char* actionName;
    const char* shortcut;
    const char* desc;
};

static const std::vector<ShortcutInfo> s_shortcuts = {
    // 文件
    {"文件", "新建", "Ctrl + N", "新建空文档标签页"},
    {"文件", "打开", "Ctrl + O", "打开已有文件"},
    {"文件", "保存", "Ctrl + S", "保存当前文档"},
    {"文件", "另存为", "Ctrl + Alt + S", "另存为新文件"},
    {"文件", "全部保存", "Ctrl + Shift + S", "保存所有已修改标签页"},
    {"文件", "关闭", "Ctrl + W", "关闭当前文档"},
    {"文件", "全部关闭", "Ctrl + Shift + W", "关闭所有打开的文档"},
    {"文件", "恢复关闭的文件", "Ctrl + Shift + T", "重新打开最近一个关闭的文档"},
    {"文件", "打印", "Ctrl + P", "把当前文档按行送到打印机（行号 + 等宽字体）"},
    {"文件", "退出", "Ctrl + Q", "退出 Notepad++"},

    // 编辑
    {"编辑", "撤销", "Ctrl + Z", "撤销上一步操作"},
    {"编辑", "恢复", "Ctrl + Y", "恢复上一步撤销的操作"},
    {"编辑", "剪切", "Ctrl + X", "剪切选区到剪贴板"},
    {"编辑", "复制", "Ctrl + C", "复制选区到剪贴板"},
    {"编辑", "粘贴", "Ctrl + V", "粘贴剪贴板内容"},
    {"编辑", "全选", "Ctrl + A", "全选当前文档文本"},
    {"编辑", "复制当前行", "Ctrl + D", "复制光标所在整行到下一行"},
    {"编辑", "删除当前行", "Ctrl + Shift + L / Ctrl + L", "直接删除光标所在整行"},
    {"编辑", "向上移动行", "Ctrl + Shift + Up", "将当前行与上一行交换位置"},
    {"编辑", "向下移动行", "Ctrl + Shift + Down", "将当前行与下一行交换位置"},
    {"编辑", "合并行", "Ctrl + J", "合并选中行或下一行"},
    {"编辑", "转为大写", "Ctrl + Shift + U", "将选区文字转换为大写"},
    {"编辑", "转为小写", "Ctrl + U", "将选区文字转换为小写"},
    {"编辑", "切换行注释", "Ctrl + Q / Ctrl + /", "为当前行添加/删除语言单行注释"},
    {"编辑", "区块注释", "Ctrl + Shift + Q", "用块注释包裹当前选区"},
    {"编辑", "上方插入空行", "Ctrl + Alt + Enter", "在当前行上方插入新行"},
    {"编辑", "下方插入空行", "Shift + Enter", "在当前行下方插入新行"},
    {"编辑", "开始/结束 选取", "Ctrl + Shift + B", "第一次按下记住起点，第二次按下选中两点之间的文本"},
    {"编辑", "开始/结束 按列选取", "Alt + Shift + B", "第一次按下记住起点，第二次按下选中两点之间的矩形列块"},
    {"编辑", "列选模式...", "（无）", "弹出说明，列出进入列选（矩形选区）的三种方式"},
    {"编辑", "列编辑器", "Alt + C", "在列块的每一行插入相同文本，或插入按增量递增的数字"},

    // 搜索
    {"搜索", "查找", "Ctrl + F", "打开查找对话框"},
    {"搜索", "替换", "Ctrl + H", "打开替换对话框"},
    {"搜索", "查找下一个", "F3", "向后查找下一个匹配项"},
    {"搜索", "查找上一个", "Shift + F3", "向前查找上一个匹配项"},
    {"搜索", "快速查找下一个", "Ctrl + F3", "取当前词向后查找"},
    {"搜索", "快速查找上一个", "Ctrl + Shift + F3", "取当前词向前查找"},
    {"搜索", "定位行", "Ctrl + G", "跳转到指定行号"},
    {"搜索", "跳转到匹配括号", "Ctrl + B", "跳转到当前括号的另一半"},
    {"搜索", "选中匹配括号内容", "Ctrl + Alt + B", "选中对应括号之间的全部文本"},
    {"搜索", "切换书签", "Ctrl + F2", "在当前行添加或删除书签标记"},
    {"搜索", "下一个书签", "F2", "跳转到下一个书签行"},
    {"搜索", "上一个书签", "Shift + F2", "跳转到上一个书签行"},

    // 视图
    {"视图", "全屏切换", "F11", "全屏或退出全屏"},
    {"视图", "放大", "Ctrl + = / Ctrl + +", "放大文本显示"},
    {"视图", "缩小", "Ctrl + -", "缩小文本显示"},
    {"视图", "重置缩放", "Ctrl + 0", "恢复默认文字大小"},
    {"视图", "折叠当前层次", "Ctrl + Alt + F", "折叠当前代码块"},
    {"视图", "展开当前层次", "Ctrl + Alt + Shift + F", "展开当前代码块"},
    {"视图", "折叠全部", "Alt + 0", "折叠全文所有代码块"},
    {"视图", "展开全部", "Alt + Shift + 0", "展开全文所有代码块"},
    {"视图", "下一个标签页", "Ctrl + PageDown / Ctrl + Tab", "切换到右侧标签页"},
    {"视图", "上一个标签页", "Ctrl + PageUp / Ctrl + Shift + Tab", "切换到左侧标签页"},

    // 高级功能
    {"操作", "列编辑模式", "Alt + 鼠标拖动", "创建矩形/列选取光标"},
    {"操作", "列选区扩展", "Alt + Shift + 方向键", "从光标处按列向各个方向扩展矩形选区，可选到行尾之外"},
    {"操作", "多重光标编辑", "Ctrl + 鼠标左键点击", "在任意多处添加独立光标"}
};

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("快捷键一览 (Shortcut Mapper)");
    resize(700, 500);

    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(this);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"类别", "功能名称", "快捷键", "说明"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);

    table->setRowCount(static_cast<int>(s_shortcuts.size()));
    for (size_t i = 0; i < s_shortcuts.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(s_shortcuts[i].category));
        table->setItem(i, 1, new QTableWidgetItem(s_shortcuts[i].actionName));
        table->setItem(i, 2, new QTableWidgetItem(s_shortcuts[i].shortcut));
        table->setItem(i, 3, new QTableWidgetItem(s_shortcuts[i].desc));
    }

    layout->addWidget(table);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btnBox);
}
