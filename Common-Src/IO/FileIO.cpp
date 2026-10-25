#include "FileIO.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# define NOMINMAX
# include <windows.h>
# include <shlobj.h>
# include <atomic>
# include <memory>
#else
# include <cerrno>
# include <unistd.h>
#endif

namespace sms::io {
namespace {

// Also clean up after allocation exceptions or failed publication.
struct TemporaryFile {
    std::filesystem::path path;
    bool ownsPath = false;
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool close() noexcept
    {
        if (handle == INVALID_HANDLE_VALUE) return true;
        const bool ok = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        return ok;
    }
#else
    int descriptor = -1;
    bool close() noexcept
    {
        if (descriptor < 0) return true;
        const bool ok = ::close(descriptor) == 0;
        descriptor = -1;
        return ok;
    }
#endif
    ~TemporaryFile()
    {
        (void)close();
        if (ownsPath && !path.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
};

} // namespace

#if defined(_WIN32)
std::filesystem::path moduleBinaryPath() noexcept
{
    try {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&moduleBinaryPath), &module)) return {};
        for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
            std::vector<wchar_t> filename(capacity);
            const DWORD length = GetModuleFileNameW(module, filename.data(), capacity);
            if (length == 0) return {};
            if (length < capacity) return std::filesystem::path(
                std::wstring(filename.data(), length));
        }
    } catch (...) {}
    return {};
}
#endif

std::filesystem::path userConfigDirectory() noexcept
{
    try {
#if defined(_WIN32)
        PWSTR folder = nullptr;
        const auto result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(folder, &CoTaskMemFree);
        if (FAILED(result) || folder == nullptr) return {};
        const std::filesystem::path root(folder);
        return root.is_absolute() ? root : std::filesystem::path{};
#else
# if !defined(__APPLE__)
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
            const auto root = pathFromUtf8(xdg);
            if (root.is_absolute()) return root;
        }
# endif
        const char* home = std::getenv("HOME");
        if (!home || !*home) return {};
        const auto root = pathFromUtf8(home);
        if (!root.is_absolute()) return {};
# if defined(__APPLE__)
        return root / "Library" / "Application Support";
# else
        return root / ".config";
# endif
#endif
    } catch (...) { return {}; }
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path,
                                  const std::size_t maxBytes) noexcept
{
    try {
        if (path.empty() || maxBytes >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return {};
        std::vector<std::uint8_t> bytes;
#if defined(_WIN32)
        // Readers must allow replacement while another caller writes the file.
        TemporaryFile input;
        input.handle = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input.handle == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(input.handle, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > maxBytes) return {};
        bytes.resize(static_cast<std::size_t>(size.QuadPart) + 1);
        DWORD count = 0;
        if (!ReadFile(input.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
            count > maxBytes) return {};
        bytes.resize(count);
#else
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto size = input.tellg();
        if (size < 0 || static_cast<std::uintmax_t>(size) > maxBytes) return {};
        bytes.resize(static_cast<std::size_t>(size) + 1);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.bad() || !input.eof() || input.gcount() > static_cast<std::streamsize>(maxBytes))
            return {};
        bytes.resize(static_cast<std::size_t>(input.gcount()));
#endif
        return bytes;
    } catch (...) { return {}; }
}

bool writeAtomically(const std::filesystem::path& path, const std::string_view contents) noexcept
{
    try {
        if (path.empty() || path.filename().empty()) return false;
        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;
        }
        TemporaryFile output;
#if defined(_WIN32)
        static std::atomic<unsigned long long> sequence{0};
        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            output.path = path;
            output.path += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
            output.handle = CreateFileW(output.path.c_str(), GENERIC_WRITE | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output.handle != INVALID_HANDLE_VALUE) {
                output.ownsPath = true;
                break;
            }
            const auto failure = GetLastError();
            // Never remove a file that this writer did not create.
            output.path.clear();
            if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS)
                return false;
        }
        if (output.handle == INVALID_HANDLE_VALUE) return false;
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                contents.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD count = 0;
            if (!WriteFile(output.handle, contents.data() + offset, chunk, &count, nullptr) || count == 0)
                return false;
            offset += count;
        }
        // Windows 10+ POSIX rename semantics keep existing readers on the old
        // complete file while new readers open the new one. Legacy rename can
        // fail when another caller holds the destination open.
        const auto destination = std::filesystem::absolute(path).native();
        const auto nameBytes = destination.size() * sizeof(wchar_t);
        const auto bufferBytes = sizeof(FILE_RENAME_INFO) + nameBytes;
        const std::unique_ptr<FILE_RENAME_INFO, decltype(&std::free)> rename(
            static_cast<FILE_RENAME_INFO*>(std::calloc(1, bufferBytes)), &std::free);
        if (!rename || bufferBytes > std::numeric_limits<DWORD>::max()) return false;
        rename->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        rename->FileNameLength = static_cast<DWORD>(nameBytes);
        std::memcpy(rename->FileName, destination.data(), nameBytes);
        if (!SetFileInformationByHandle(output.handle, FileRenameInfoEx,
                rename.get(), static_cast<DWORD>(bufferBytes))) return false;
#else
        auto temporary = path.native() + ".tmp-XXXXXX";
        output.descriptor = ::mkstemp(temporary.data());
        if (output.descriptor < 0) return false;
        // If constructing the native path throws, still remove the created file.
        try { output.path = temporary; }
        catch (...) { ::unlink(temporary.c_str()); throw; }
        output.ownsPath = true;
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const auto count = ::write(output.descriptor, contents.data() + offset, contents.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            offset += static_cast<std::size_t>(count);
        }
        if (!output.close()) return false;
        std::filesystem::rename(output.path, path, error);
        if (error) return false;
#endif
        output.path.clear();
        return true;
    } catch (...) { return false; }
}

} // namespace sms::io
