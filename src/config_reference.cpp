#include "config.h"
#include <atomic>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace fs = std::filesystem;
namespace {
constexpr std::string_view beginMarker = "-- BEGIN RELAY SETTINGS";
constexpr std::string_view endMarker = "-- END RELAY SETTINGS";

bool read(const fs::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    text.assign(std::istreambuf_iterator<char>(file), {});
    return !file.bad();
}

std::string_view lineAt(std::string_view text, size_t at, size_t& next) {
    auto end = text.find('\n', at);
    next = end == text.npos ? text.size() : end + 1;
    if (end == text.npos) end = text.size();
    if (end > at && text[end - 1] == '\r') --end;
    return text.substr(at, end - at);
}

std::string refreshed(const std::string& text, std::string& error) {
    const auto newline = text.find('\n');
    const bool crlf = newline != text.npos && newline > 0 && text[newline - 1] == '\r';
    const std::string eol = crlf ? "\r\n" : "\n";
    auto reference = Config::reference();
    if (crlf) for (size_t at = 0; (at = reference.find('\n', at)) != reference.npos; at += 2)
        reference.insert(at, 1, '\r');
    size_t prefix = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    // Lua's file loader accepts a BOM and a first-line shell interpreter comment.
    if (prefix < text.size() && text[prefix] == '#') {
        if (newline == text.npos) return text + eol + reference;
        prefix = newline + 1;
    }
    size_t begin = prefix, end = prefix;
    for (size_t at = prefix; at < text.size();) {
        size_t next;
        const auto line = lineAt(text, at, next);
        if (line == beginMarker) {
            begin = at;
            for (at = next; at < text.size(); at = next) {
                const auto inside = lineAt(text, at, next);
                if (inside == endMarker) { end = next; break; }
                if (!inside.starts_with("--") || inside.starts_with("--[")) break;
            }
            if (end == prefix) {
                error = "Cannot refresh settings comments; remove the damaged BEGIN/END RELAY SETTINGS block and reopen config";
                return {};
            }
            break;
        }
        // Look only through leading line comments. Never search Lua strings/code.
        if ((!line.empty() && !line.starts_with("--")) || line.starts_with("--[")) break;
        at = next;
    }
    return text.substr(0, begin) + reference + text.substr(end);
}
}

std::string Config::refreshReference(const fs::path& file) {
    constexpr auto readError = "Cannot read init.lua; check its permissions and reopen config";
    constexpr auto writeError = "Cannot refresh init.lua; check permissions and disk space, then reopen config";
    std::error_code ec;
    const bool exists = fs::exists(file, ec);
    if (ec) return readError;
    std::string original;
    if (exists && !read(file, original)) return readError;
    if (original.find('\0') != original.npos)
        return "Cannot refresh init.lua in this encoding; save it as UTF-8 and reopen config";
    std::string error;
    const auto updated = refreshed(exists ? original : "\n-- Your overrides; save this file to apply settings\nreturn {}\n", error);
    if (!error.empty()) return error;
    if (exists && updated == original) return {};
    if (!file.parent_path().empty()) fs::create_directories(file.parent_path(), ec);
    if (ec || updated.size() > std::numeric_limits<DWORD>::max()) return writeError;

    static std::atomic<unsigned> sequence = 0;
    auto temp = file;
    temp += L".relay-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence) + L".tmp";
    HANDLE output = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return writeError;
    DWORD written = 0;
    const bool saved = WriteFile(output, updated.data(), DWORD(updated.size()), &written, nullptr) &&
        written == updated.size() && FlushFileBuffers(output);
    CloseHandle(output);
    if (!saved) { DeleteFileW(temp.c_str()); return writeError; }

    bool replaced = false;
    if (exists) {
        std::string current;
        if (!read(file, current) || current != original) {
            DeleteFileW(temp.c_str());
            return "init.lua changed during refresh; finish saving it, then reopen config";
        }
        auto backup = file;
        backup += L".relay-backup";
        replaced = ReplaceFileW(file.c_str(), temp.c_str(), backup.c_str(), 0, nullptr, nullptr);
        if (!replaced && !fs::exists(file, ec))
            MoveFileExW(backup.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH);
    } else {
        // Do not replace a file that an editor created after the initial read.
        replaced = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH);
    }
    if (!replaced) { DeleteFileW(temp.c_str()); return writeError; }
    return {};
}
