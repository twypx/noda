#include "SessionManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
constexpr const char* kSessionFile = "session.xml";
// 与上游 Parameters.cpp 的 SESSION_BACKUP_EXT 同名，便于对照。
constexpr const char* kCorruptSuffix = ".inCaseOfCorruption.bak";

bool parseSessionFile(const QString& source, QVector<SessionFile>& files, int& activeIndex) {
    files.clear();
    activeIndex = 0;

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) return false;

    QXmlStreamReader r(&in);
    bool sawSession = false;
    // 标准写法：先进入根元素，再逐层 readNextStartElement。
    // 不能在 while (!atEnd()) 里反复调 readNextStartElement()：读到 EndDocument 之后再调
    // 一次，Qt 会立即置 hasError("Premature end of document")，把解析成功的内容判成损坏，
    // 于是 read() 永远回退到备份分支，会话永远恢复不出来。
    if (r.readNextStartElement() && r.name() == QLatin1String("NotepadPlus")) {
        while (r.readNextStartElement()) {
            if (r.name() != QLatin1String("Session")) {
                r.skipCurrentElement();
                continue;
            }

            sawSession = true;
            while (r.readNextStartElement()) {
                if (r.name() != QLatin1String("mainView")) {
                    r.skipCurrentElement();
                    continue;
                }
                activeIndex = r.attributes().value(QStringLiteral("activeIndex")).toInt();
                while (r.readNextStartElement()) {
                    if (r.name() != QLatin1String("File")) {
                        r.skipCurrentElement();
                        continue;
                    }
                    const QXmlStreamAttributes a = r.attributes();
                    SessionFile f;
                    f.path = a.value(QStringLiteral("filename")).toString();
                    f.backupPath = a.value(QStringLiteral("backupFilePath")).toString();
                    f.lang = a.value(QStringLiteral("lang")).toString();
                    f.encoding = a.value(QStringLiteral("encoding")).toString();
                    f.firstVisibleLine = a.value(QStringLiteral("firstVisibleLine")).toInt();
                    // toInt() 的第一个参数是 bool*，不能直接传 -1；缺属性时按“无选区”处理。
                    const QString startStr = a.value(QStringLiteral("startPos")).toString();
                    const QString endStr = a.value(QStringLiteral("endPos")).toString();
                    f.selStart = startStr.isEmpty() ? -1 : startStr.toInt();
                    f.selEnd = endStr.isEmpty() ? -1 : endStr.toInt();
                    f.caretPos = f.selEnd >= 0 ? f.selEnd : qMax(f.selStart, 0);
                    f.eolMode = a.value(QStringLiteral("eolMode")).toInt();
                    f.newDocIndex = a.value(QStringLiteral("newDocIndex")).toInt();
                    f.readOnly = a.value(QStringLiteral("userReadOnly")) == QLatin1String("yes");
                    r.skipCurrentElement();
                    // 既没有真实路径也没有备份的条目没有恢复价值（比如已被清空的空标签）。
                    if (!f.path.isEmpty() || !f.backupPath.isEmpty()) {
                        files.append(f);
                    }
                }
            }
        }
    }

    if (r.hasError() || !sawSession) {
        files.clear();
        activeIndex = 0;
        return false;
    }
    return true;
}
} // namespace

QString SessionManager::configDir() {
    // 直接取 QSettings 的落点，而不是 QStandardPaths::AppConfigLocation：
    // 本程序的组织名与应用名都是 "Notepad++"，后者会推导出
    // ~/.config/Notepad++/Notepad++ 这种多一层的路径，与 Notepad++.conf 分家。
    QSettings s;
    return QFileInfo(s.fileName()).absolutePath();
}

QString SessionManager::sessionPath() {
    return configDir() + QLatin1Char('/') + QLatin1String(kSessionFile);
}

QString SessionManager::backupDir() {
    return configDir() + QLatin1String("/backup");
}

QString SessionManager::newBackupPath(const QString& displayName) {
    QDir dir(backupDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return QString();
    }
    // 标签名可能带路径分隔符以外的任意字符，统一把 '/' 与 '\' 换成 '_' 以保文件名合法。
    QString base = displayName;
    base.replace(QLatin1Char('/'), QLatin1Char('_'));
    base.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (base.isEmpty()) base = QStringLiteral("untitled");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    return dir.filePath(base + QLatin1Char('@') + stamp);
}

bool SessionManager::writeBackup(const QString& path, const QString& utf8) {
    if (path.isEmpty()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = utf8.toUtf8();
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

void SessionManager::removeBackupFile(const QString& path) {
    if (path.isEmpty()) return;
    QFile::remove(path);
}

bool SessionManager::write(const QVector<SessionFile>& files, int activeIndex) {
    QDir().mkpath(configDir()); // 首次运行时配置目录可能还不存在

    const QString target = sessionPath();
    // 覆盖前留一份，XML 写坏时 read() 可以退回上一版（同上游 writeSession 的 CopyFile）。
    if (QFile::exists(target)) {
        const QString corrupted = target + QLatin1String(kCorruptSuffix);
        QFile::remove(corrupted);
        QFile::copy(target, corrupted);
    }

    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("NotepadPlus"));
    w.writeStartElement(QStringLiteral("Session"));
    w.writeAttribute(QStringLiteral("activeView"), QStringLiteral("0"));
    // 本移植只有单视图，subView 仍写出来，保持与上游 session.xml 结构一致。
    w.writeStartElement(QStringLiteral("mainView"));
    w.writeAttribute(QStringLiteral("activeIndex"), QString::number(activeIndex));

    for (const SessionFile& f : files) {
        w.writeStartElement(QStringLiteral("File"));
        w.writeAttribute(QStringLiteral("firstVisibleLine"), QString::number(f.firstVisibleLine));
        w.writeAttribute(QStringLiteral("startPos"), QString::number(f.selStart >= 0 ? f.selStart : f.caretPos));
        w.writeAttribute(QStringLiteral("endPos"), QString::number(f.selEnd >= 0 ? f.selEnd : f.caretPos));
        w.writeAttribute(QStringLiteral("lang"), f.lang);
        w.writeAttribute(QStringLiteral("encoding"), f.encoding);
        w.writeAttribute(QStringLiteral("userReadOnly"), f.readOnly ? QStringLiteral("yes") : QStringLiteral("no"));
        w.writeAttribute(QStringLiteral("eolMode"), QString::number(f.eolMode));
        w.writeAttribute(QStringLiteral("newDocIndex"), QString::number(f.newDocIndex));
        w.writeAttribute(QStringLiteral("filename"), f.path);
        w.writeAttribute(QStringLiteral("backupFilePath"), f.backupPath);
        w.writeEndElement(); // File
    }

    w.writeEndElement(); // mainView
    w.writeStartElement(QStringLiteral("subView"));
    w.writeAttribute(QStringLiteral("activeIndex"), QStringLiteral("0"));
    w.writeEndElement(); // subView
    w.writeEndElement(); // Session
    w.writeEndElement(); // NotepadPlus
    w.writeEndDocument();

    return out.commit();
}

bool SessionManager::read(QVector<SessionFile>& files, int& activeIndex) {
    files.clear();
    activeIndex = 0;

    const QString target = sessionPath();
    // 先读主文件；解析失败（被写坏/截断）再退回上一版备份，避免一次坏写让会话永久丢失。
    if (QFile::exists(target) && parseSessionFile(target, files, activeIndex)) {
        return true;
    }
    const QString corrupted = target + QLatin1String(kCorruptSuffix);
    if (QFile::exists(corrupted) && parseSessionFile(corrupted, files, activeIndex)) {
        return true;
    }
    files.clear();
    activeIndex = 0;
    return false;
}
