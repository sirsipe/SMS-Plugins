#include "DefaultsFile.hpp"
#include "FileIO.hpp"

#include <cstdint>
#include <fstream>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# define NOMINMAX
# include <windows.h>
#else
# include <cerrno>
# include <fcntl.h>
# include <sys/file.h>
# include <unistd.h>
#endif

namespace sms::io {
namespace {
constexpr std::size_t maxFileBytes = 64 * 1024;
constexpr std::size_t maxEntries = 256;
constexpr std::size_t maxKeyBytes = 128;
constexpr std::size_t maxValueBytes = 4096;

bool whitespace(const char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && whitespace(text.front())) text.remove_prefix(1);
    while (!text.empty() && whitespace(text.back())) text.remove_suffix(1);
    return text;
}

bool validKey(const std::string_view key) noexcept
{
    if (key.empty() || key.size() > maxKeyBytes) return false;
    for (const char c : key) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') continue;
        return false;
    }
    return true;
}

bool validValue(const std::string_view value) noexcept
{
    return value.size() <= maxValueBytes && value.find_first_of(
        std::string_view("\0\r\n", 3)) == std::string_view::npos;
}

bool decodeValue(std::string_view encoded, std::string& value)
{
    encoded = trim(encoded);
    value.clear();
    if (!encoded.empty() && encoded.front() == '"') {
        if (encoded.size() < 2 || encoded.back() != '"') return false;
        encoded.remove_prefix(1);
        encoded.remove_suffix(1);
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            char c = encoded[i];
            if (c == '\\') {
                if (++i == encoded.size()) return false;
                c = encoded[i];
                if (c != '\\' && c != '"') return false;
            } else if (c == '"') return false;
            value += c;
            if (value.size() > maxValueBytes) return false;
        }
    } else value.assign(encoded);
    return validValue(value);
}

// Unlike readBytes(), distinguish an empty file from an unreadable existing file
// so a failed merge can never replace existing preferences with an empty map.
bool readContents(const std::filesystem::path& path, std::string& contents)
{
    if (path.empty() || path.filename().empty()) return false;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (status.type() == std::filesystem::file_type::not_found &&
            (!error || error == std::errc::no_such_file_or_directory)) return true;
    if (error || !std::filesystem::is_regular_file(path, error) || error) return false;
#if defined(_WIN32)
    struct InputFile {
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~InputFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } input;
    // Native sharing allows another writer to atomically replace the pathname
    // while this reader continues reading the previous complete file.
    input.handle = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.handle, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > maxFileBytes) return false;
    contents.resize(maxFileBytes + 1);
    DWORD count = 0;
    if (!ReadFile(input.handle, contents.data(), static_cast<DWORD>(contents.size()), &count, nullptr) ||
            count > maxFileBytes) return false;
    contents.resize(count);
#else
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    contents.resize(maxFileBytes + 1);
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.bad() || !input.eof() || input.gcount() > static_cast<std::streamsize>(maxFileBytes))
        return false;
    contents.resize(static_cast<std::size_t>(input.gcount()));
#endif
    return true;
}

bool parse(const std::string_view contents, DefaultsFile::Values& values)
{
    std::size_t offset = 0;
    bool complete = true;
    while (offset < contents.size()) {
        const auto newline = contents.find('\n', offset);
        const auto end = newline == std::string_view::npos ? contents.size() : newline;
        const auto line = trim(contents.substr(offset, end - offset));
        offset = end + 1;
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = trim(line.substr(0, equals));
        if (!validKey(key)) continue;
        std::string value;
        if (!decodeValue(line.substr(equals + 1), value)) continue;
        const auto existing = values.find(key);
        if (existing != values.end()) existing->second = std::move(value);
        else if (values.size() < maxEntries) values.emplace(key, std::move(value));
        else complete = false;
    }
    return complete;
}

void appendValue(std::string& contents, const std::string_view value)
{
    const bool quote = !value.empty() &&
        (whitespace(value.front()) || whitespace(value.back()) || value.front() == '"');
    if (!quote) { contents += value; return; }
    contents += '"';
    for (const char c : value) {
        if (c == '\\' || c == '"') contents += '\\';
        contents += c;
    }
    contents += '"';
}

// Lock a stable object: locking the preferences inode would stop protecting
// writers as soon as atomic replacement publishes a new inode.
class DirectoryLock {
public:
    explicit DirectoryLock(const std::filesystem::path& directory)
    {
#if defined(_WIN32)
        auto native = std::filesystem::canonical(directory).native();
        // Windows file paths are normally case-insensitive. Work with native
        // Unicode rather than narrowing the mutex identity to the active ACP.
        if (!native.empty()) {
            const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                native.data(), static_cast<int>(native.size()), nullptr, 0, nullptr, nullptr, 0);
            if (length <= 0) return;
            std::wstring folded(static_cast<std::size_t>(length), L'\0');
            if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                    native.data(), static_cast<int>(native.size()), folded.data(), length,
                    nullptr, nullptr, 0)) return;
            native = std::move(folded);
        }
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (const wchar_t c : native) {
            hash ^= static_cast<std::uint16_t>(c);
            hash *= UINT64_C(1099511628211);
        }
        const auto name = L"Global\\SMS-DefaultsFile-v1-" + std::to_wstring(hash);
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) return;
        const DWORD result = WaitForSingleObject(handle_, INFINITE);
        locked_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
#else
        descriptor_ = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor_ < 0) return;
        int result;
        do { result = ::flock(descriptor_, LOCK_EX); } while (result < 0 && errno == EINTR);
        locked_ = result == 0;
#endif
    }
    ~DirectoryLock()
    {
#if defined(_WIN32)
        if (locked_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
#else
        if (descriptor_ >= 0) ::close(descriptor_);
#endif
    }
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
    [[nodiscard]] bool locked() const noexcept { return locked_; }
private:
    bool locked_ = false;
#if defined(_WIN32)
    HANDLE handle_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};
} // namespace

std::filesystem::path DefaultsFile::configPath(const std::string_view appName) noexcept
{
    try {
        if (appName.empty() || appName == "." || appName == ".." ||
            appName.find_first_of(std::string_view("/\\\0", 3)) != std::string_view::npos)
            return {};
        const auto directory = userConfigDirectory();
        if (directory.empty()) return {};
        const auto app = pathFromUtf8(appName);
        if (app.has_root_path() || app.filename() != app) return {};
        return directory / app / "defaults.ini";
    } catch (...) { return {}; }
}

DefaultsFile::Values DefaultsFile::read(const std::filesystem::path& path) noexcept
{
    try {
        std::string contents;
        if (!readContents(path, contents)) return {};
        Values values;
        (void)parse(contents, values);
        return values;
    } catch (...) { return {}; }
}

std::string DefaultsFile::get(const std::filesystem::path& path, const std::string_view key) noexcept
{
    try {
        const auto values = read(path);
        const auto found = values.find(key);
        return found == values.end() ? std::string{} : found->second;
    } catch (...) { return {}; }
}

bool DefaultsFile::set(const std::filesystem::path& path, const std::string_view key,
                       const std::string_view value) noexcept
{
    try {
        if (path.empty() || path.filename().empty() || !validKey(key) || !validValue(value))
            return false;
        const auto absolute = std::filesystem::absolute(path);
        std::error_code error;
        std::filesystem::create_directories(absolute.parent_path(), error);
        if (error) return false;
        const DirectoryLock lock(absolute.parent_path());
        if (!lock.locked()) return false;
        std::string contents;
        if (!readContents(absolute, contents)) return false;
        Values values;
        if (!parse(contents, values)) return false;
        if (values.size() == maxEntries && values.find(key) == values.end()) return false;
        values.insert_or_assign(std::string(key), std::string(value));
        contents.clear();
        for (const auto& [name, setting] : values) {
            contents += name;
            contents += '=';
            appendValue(contents, setting);
            contents += '\n';
            if (contents.size() > maxFileBytes) return false;
        }
        return writeAtomically(absolute, contents);
    } catch (...) { return false; }
}

} // namespace sms::io
