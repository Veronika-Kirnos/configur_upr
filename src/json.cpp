#include "app.h"
#include <cctype>
#include <fstream>
#include <filesystem>
#include <regex>
#include <sstream>
#include <stdexcept>

std::string read_file(const std::string& filename) {
    std::ifstream file(std::filesystem::u8path(filename), std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read file: " + filename);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

class JsonParser {
    const std::string& source;
    size_t pos = 0;

    void spaces() {
        while (pos < source.size() && std::string(" \t\r\n").find(source[pos]) != std::string::npos) ++pos;
    }

    char take() {
        if (pos == source.size()) throw std::runtime_error("Unexpected end of JSON");
        return source[pos++];
    }

    void expect(char symbol) {
        spaces();
        if (take() != symbol) throw std::runtime_error("Invalid JSON near position " + std::to_string(pos));
    }

    unsigned hex() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = take();
            value *= 16;
            if (ch >= '0' && ch <= '9') value += ch - '0';
            else if (ch >= 'a' && ch <= 'f') value += ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') value += ch - 'A' + 10;
            else throw std::runtime_error("Invalid Unicode escape in JSON");
        }
        return value;
    }

    void unicode(std::string& result) {
        unsigned code = hex();
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (take() != '\\' || take() != 'u') throw std::runtime_error("Missing Unicode surrogate");
            unsigned low = hex();
            if (low < 0xDC00 || low > 0xDFFF) throw std::runtime_error("Invalid Unicode surrogate");
            code = 0x10000 + (code - 0xD800) * 1024 + low - 0xDC00;
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            throw std::runtime_error("Invalid Unicode surrogate");
        }
        if (code < 0x80) result += static_cast<char>(code);
        else {
            int count = code < 0x800 ? 2 : (code < 0x10000 ? 3 : 4);
            result += static_cast<char>((0xFF << (8 - count)) | (code >> (6 * (count - 1))));
            for (int i = count - 2; i >= 0; --i)
                result += static_cast<char>(0x80 | ((code >> (6 * i)) & 0x3F));
        }
    }

    std::string string() {
        expect('"');
        std::string result;
        while (true) {
            char ch = take();
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 32) throw std::runtime_error("Control character in JSON");
            if (ch != '\\') { result += ch; continue; }
            char escape = take();
            if (escape == 'u') { unicode(result); continue; }
            std::string keys = "\"\\/bfnrt";
            std::string values = "\"\\/\b\f\n\r\t";
            size_t index = keys.find(escape);
            if (index == std::string::npos) throw std::runtime_error("Invalid JSON escape");
            result += values[index];
        }
    }

    Json collection(bool object, int depth) {
        Json result;
        result.type = object ? Json::Object : Json::Array;
        expect(object ? '{' : '[');
        spaces();
        char closing = object ? '}' : ']';
        if (pos < source.size() && source[pos] == closing) { ++pos; return result; }
        while (true) {
            if (object) {
                std::string key = string();
                expect(':');
                if (!result.object.emplace(key, value(depth + 1)).second)
                    throw std::runtime_error("Duplicate JSON key: " + key);
            } else result.array.push_back(value(depth + 1));
            spaces();
            char separator = take();
            if (separator == closing) return result;
            if (separator != ',') throw std::runtime_error("Expected comma in JSON");
        }
    }

    Json value(int depth) {
        if (depth > 64) throw std::runtime_error("JSON nesting is too deep");
        spaces();
        if (pos == source.size()) throw std::runtime_error("Empty JSON value");
        char ch = source[pos];
        if (ch == '{' || ch == '[') return collection(ch == '{', depth);
        Json result;
        if (ch == '"') { result.type = Json::String; result.text = string(); return result; }
        size_t start = pos;
        while (pos < source.size() && std::string(",]} \t\r\n").find(source[pos]) == std::string::npos) ++pos;
        result.text = source.substr(start, pos - start);
        if (result.text == "true" || result.text == "false") result.type = Json::Boolean;
        else if (result.text == "null") result.type = Json::Null;
        else {
            static const std::regex number("-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?");
            if (!std::regex_match(result.text, number)) throw std::runtime_error("Invalid JSON value");
            result.type = Json::Number;
        }
        return result;
    }

public:
    explicit JsonParser(const std::string& text) : source(text) {}
    Json parse() {
        if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;
        Json result = value(0);
        spaces();
        if (pos != source.size()) throw std::runtime_error("Extra text after JSON");
        return result;
    }
};

Json parse_json(const std::string& text) {
    return JsonParser(text).parse();
}
