#ifndef CHECKER_GUI_CODE_VIEW_H
#define CHECKER_GUI_CODE_VIEW_H

#include "imgui.h"

#include <cctype>
#include <cstddef>
#include <string_view>

namespace checker::gui::detail {

inline bool is_code_identifier_start(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    return std::isalpha(c) || ch == '_';
}

inline bool is_code_identifier_body(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    return std::isalnum(c) || ch == '_';
}

inline bool is_cpp_keyword(std::string_view token) {
    static constexpr std::string_view keywords[] = {
        "alignas", "alignof", "asm", "auto", "break", "case", "catch", "class",
        "concept", "const", "constexpr", "continue", "decltype", "default", "delete",
        "do", "else", "enum", "explicit", "export", "extern", "final", "for",
        "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
        "operator", "override", "private", "protected", "public", "requires", "return",
        "sizeof", "static", "struct", "switch", "template", "this", "throw", "try",
        "typedef", "typename", "using", "virtual", "volatile", "while",
    };
    for (std::string_view keyword : keywords) {
        if (token == keyword) {
            return true;
        }
    }
    return false;
}

inline bool is_cpp_type_word(std::string_view token) {
    static constexpr std::string_view types[] = {
        "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int",
        "long", "short", "signed", "unsigned", "void", "wchar_t", "size_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "string", "string_view", "vector", "array", "map",
        "set", "optional", "pair", "tuple",
    };
    for (std::string_view type : types) {
        if (token == type) {
            return true;
        }
    }
    return false;
}

inline bool is_cpp_literal_word(std::string_view token) {
    return token == "true" || token == "false" || token == "nullptr" || token == "NULL";
}

inline void draw_code_span(std::string_view text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

inline void draw_code_span(std::string_view text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    draw_code_span(text);
    ImGui::PopStyleColor();
}

inline void draw_cpp_highlighted_line(std::string_view line, bool& in_block_comment) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    bool has_span = false;
    auto draw_plain = [&](size_t begin, size_t end) {
        if (end <= begin) {
            return;
        }
        if (has_span) {
            ImGui::SameLine(0.0f, 0.0f);
        }
        draw_code_span(line.substr(begin, end - begin));
        has_span = true;
    };
    auto draw_colored = [&](size_t begin, size_t end, const ImVec4& color) {
        if (end <= begin) {
            return;
        }
        if (has_span) {
            ImGui::SameLine(0.0f, 0.0f);
        }
        draw_code_span(line.substr(begin, end - begin), color);
        has_span = true;
    };

    constexpr ImVec4 keyword_color{0.48f, 0.66f, 1.0f, 1.0f};
    constexpr ImVec4 type_color{0.40f, 0.88f, 0.95f, 1.0f};
    constexpr ImVec4 literal_color{0.90f, 0.72f, 0.35f, 1.0f};
    constexpr ImVec4 string_color{0.95f, 0.62f, 0.38f, 1.0f};
    constexpr ImVec4 comment_color{0.45f, 0.70f, 0.45f, 1.0f};
    constexpr ImVec4 preprocessor_color{0.78f, 0.60f, 0.95f, 1.0f};
    constexpr ImVec4 punctuation_color{0.74f, 0.76f, 0.80f, 1.0f};

    size_t i = 0;
    if (!in_block_comment) {
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos && line[first] == '#') {
            draw_plain(0, first);
            draw_colored(first, line.size(), preprocessor_color);
            return;
        }
    }

    while (i < line.size()) {
        if (in_block_comment) {
            const size_t end = line.find("*/", i);
            if (end == std::string_view::npos) {
                draw_colored(i, line.size(), comment_color);
                i = line.size();
            } else {
                draw_colored(i, end + 2, comment_color);
                i = end + 2;
                in_block_comment = false;
            }
            continue;
        }

        if (line.compare(i, 2, "//") == 0) {
            draw_colored(i, line.size(), comment_color);
            break;
        }
        if (line.compare(i, 2, "/*") == 0) {
            const size_t end = line.find("*/", i + 2);
            if (end == std::string_view::npos) {
                draw_colored(i, line.size(), comment_color);
                in_block_comment = true;
                break;
            }
            draw_colored(i, end + 2, comment_color);
            i = end + 2;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            const size_t begin = i++;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
                ++i;
            }
            draw_plain(begin, i);
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            const size_t begin = i++;
            bool escaped = false;
            while (i < line.size()) {
                const char ch = line[i++];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (ch == '\\') {
                    escaped = true;
                    continue;
                }
                if (ch == quote) {
                    break;
                }
            }
            draw_colored(begin, i, string_color);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            const size_t begin = i++;
            while (i < line.size()) {
                const char ch = line[i];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '.' && ch != '\'') {
                    break;
                }
                ++i;
            }
            draw_colored(begin, i, literal_color);
            continue;
        }
        if (is_code_identifier_start(line[i])) {
            const size_t begin = i++;
            while (i < line.size() && is_code_identifier_body(line[i])) {
                ++i;
            }
            const std::string_view token = line.substr(begin, i - begin);
            if (is_cpp_keyword(token)) {
                draw_colored(begin, i, keyword_color);
            } else if (is_cpp_type_word(token)) {
                draw_colored(begin, i, type_color);
            } else if (is_cpp_literal_word(token)) {
                draw_colored(begin, i, literal_color);
            } else {
                draw_plain(begin, i);
            }
            continue;
        }

        draw_colored(i, i + 1, punctuation_color);
        ++i;
    }

    if (!has_span) {
        ImGui::TextUnformatted("");
    }
}

inline void draw_cpp_highlighted_code(std::string_view code) {
    bool in_block_comment = false;
    size_t begin = 0;
    while (begin < code.size()) {
        size_t end = code.find('\n', begin);
        if (end == std::string_view::npos) {
            end = code.size();
        }
        draw_cpp_highlighted_line(code.substr(begin, end - begin), in_block_comment);
        begin = end < code.size() ? end + 1 : end;
    }
    if (code.empty() || (!code.empty() && code.back() == '\n')) {
        ImGui::TextUnformatted("");
    }
}

} // namespace checker::gui::detail

#endif // CHECKER_GUI_CODE_VIEW_H
