#pragma once
#include <string>
#include <string_view>
#include <filesystem>

enum class LineEnding {
    LF,    // Unix
    CRLF,  // Windows
    CR     // Mac Classic
};

class DocumentBuffer {
public:
    DocumentBuffer() = default;

    void setFilePath(const std::string& path) {
        _filePath = path;
    }

    const std::string& getFilePath() const {
        return _filePath;
    }

    std::string getFileName() const {
        if (_filePath.empty()) {
            return "new " + std::to_string(_newDocIndex);
        }
        return std::filesystem::path(_filePath).filename().string();
    }

    void setNewDocIndex(int idx) { _newDocIndex = idx; }
    int getNewDocIndex() const { return _newDocIndex; }

    bool isModified() const { return _isModified; }
    void setModified(bool modified) { _isModified = modified; }

    const std::string& getEncoding() const { return _encoding; }
    void setEncoding(const std::string& enc) { _encoding = enc; }

    bool isBinary() const { return _isBinary; }
    void setBinary(bool b) { _isBinary = b; }

    LineEnding getLineEnding() const { return _lineEnding; }
    void setLineEnding(LineEnding le) { _lineEnding = le; }

    std::string getLineEndingString() const {
        switch (_lineEnding) {
            case LineEnding::CRLF: return "Windows (CR LF)";
            case LineEnding::LF:   return "Unix (LF)";
            case LineEnding::CR:   return "Macintosh (CR)";
        }
        return "Unix (LF)";
    }

    static LineEnding detectLineEnding(std::string_view content);

private:
    std::string _filePath;
    std::string _encoding{"UTF-8"};
    LineEnding _lineEnding{LineEnding::LF};
    bool _isModified{false};
    bool _isBinary{false};
    int _newDocIndex{1};
};
