#pragma once

#include <QObject>

// .ts / .tsx 的高亮链路：扩展名识别 -> Lexilla lexer -> 实际染色。
class TestTypeScript : public QObject {
    Q_OBJECT
private slots:
    void realConfigsMapTsAndTsxToTypescript();
    void typescriptUsesCppLexer();
    void configlessFallbackUsesTypescript();
    void openingTsFileColoursKeywords();
    void typescriptLexerPropertiesMatchUpstream();
};
