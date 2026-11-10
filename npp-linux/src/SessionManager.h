#pragma once

#include <QString>
#include <QVector>
#include <string_view>

// 上游 的“会话快照”由两部分组成，缺一不可：
//   session.xml  —— 记录打开过哪些文件、每个标签的光标/滚动位置、语言、编码；
//   backup/ 目录 —— 把未保存的修改按 <文件名>@<创建时间> 落盘。
// 两者配合才能做到“连没保存的内容也能在下次启动时原样恢复”，
// 这也是 上游 在快照模式下退出时**不弹**“是否保存”的原因。
// 设计依据见上游 ScintillaComponent/Buffer.cpp 里 backupCurrentBuffer() 上方那段
// “Specs and Algorithm of session snapshot & periodic backup system”。
struct SessionFile {
    QString path;         // 绝对路径；未命名文档为空
    QString backupPath;   // 非空表示有未保存内容，内容以 UTF-8 存在该文件里
    QString lang;         // 语言 id，如 "cpp"
    QString encoding;     // 缓冲区编码，如 "UTF-8"
    int caretPos{0};      // 光标字节位置
    int selStart{-1};     // 选区起点，-1 表示无选区
    int selEnd{-1};       // 选区终点
    int firstVisibleLine{0}; // 首个可见行的“文档行号”（还没换行折行之前的行号）
    int xOffset{0};       // 横向滚动偏移（只在关闭自动换行时有意义），缺了它重开后会只看到一行的后半截
    int eolMode{2};       // Scintilla 的 SC_EOL_*：0=CRLF 1=CR 2=LF（默认 LF）
    int newDocIndex{0};   // 未命名文档的编号（标签上显示 "new N"）
    bool readOnly{false};
};

class SessionManager {
public:
    // 配置目录：~/.config/Noda（与 QSettings 的落点一致）
    static QString configDir();
    static QString sessionPath();
    static QString backupDir();

    // 为某个标签生成备份文件名：<backup>/<name>@YYYY-MM-DD_HHMMSS
    // 时间戳只在第一次为脏缓冲区建立备份时出现，之后这个路径固定不变（同上游）。
    static QString newBackupPath(const QString& displayName);

    static bool writeBackup(const QString& path, std::string_view utf8);
    static void removeBackupFile(const QString& path);

    // 写 session.xml：先复制一份 session.xml.inCaseOfCorruption.bak（同上游做法），
    // 再用 QSaveFile 原子替换，避免断电留下半截 XML 导致下次启动读不出会话。
    static bool write(const QVector<SessionFile>& files, int activeIndex);

    // 读 session.xml；文件损坏时回退到 .inCaseOfCorruption.bak。
    static bool read(QVector<SessionFile>& files, int& activeIndex);
};
