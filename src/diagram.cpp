#include "app.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <shellapi.h>

static std::string label(const std::string& name) {
    std::string result = "\"";
    for (char ch : name) {
        if (ch == '"' || ch == '\\') result += '\\';
        if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else result += ch;
    }
    return result + "\"";
}

std::string d2_text(const Graph& graph) {
    std::ostringstream result;
    result << "direction: right\n";
    if (graph.empty()) return result.str() + "empty: \"Graph is empty\"\n";
    std::map<std::string, int> ids;
    for (const auto& entry : graph) {
        int id = static_cast<int>(ids.size());
        ids[entry.first] = id;
        result << "n" << id << ": " << label(entry.first) << " {\n"
               << "  width: " << entry.first.size() * 10 + 80 << '\n'
               << "  style.font: mono\n  style.font-size: 16\n}\n";
    }
    for (const auto& entry : graph)
        for (const auto& dep : entry.second)
            result << "n" << ids.at(entry.first) << " -> n" << ids.at(dep) << '\n';
    return result.str();
}

static std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    int slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

static void render(const std::filesystem::path& source, const std::filesystem::path& image) {
    const wchar_t* setting = _wgetenv(L"D2_EXE");
    std::wstring executable = setting ? setting : L"d2.exe";
    if (!setting) {
        const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
        if (local) {
            auto installed = std::filesystem::path(local) / L"Programs" / L"D2" / L"d2.exe";
            if (std::filesystem::is_regular_file(installed)) executable = installed.wstring();
        }
    }
    std::wstring command = quote(executable) + L" --layout=dagre " + quote(source.wstring())
                         + L" " + quote(image.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process))
        throw std::runtime_error("D2 not found. Install D2 or set D2_EXE to d2.exe; D2 source was saved.");
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) throw std::runtime_error("D2 failed with code " + std::to_string(code));
}

std::string save_diagram(const Graph& graph, const std::string& package, bool open_image) {
    std::string name = package;
    for (char& ch : name)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '-' && ch != '_') ch = '_';
    std::filesystem::create_directories("output");
    auto base = std::filesystem::path("output") / name;
    auto source = base;
    source += ".d2";
    auto image = base;
    image += ".svg";
    std::ofstream file(source);
    if (!file) throw std::runtime_error("Cannot write D2 file");
    file << d2_text(graph);
    file.close();
    if (!file) throw std::runtime_error("Cannot save D2 file");
    render(source, image);
    if (open_image) {
        auto full = std::filesystem::absolute(image).wstring();
        auto result = ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("Cannot open SVG image");
    }
    return image.u8string();
}
