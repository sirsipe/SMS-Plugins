#pragma once

#if defined(_WIN32)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <objbase.h>
# include <shellapi.h>
# include <string>
#elif defined(__linux__)
# include <cerrno>
# include <spawn.h>
# include <sys/wait.h>
extern char** environ;
#endif

namespace sms::ui {

// UI/control thread only. The Windows API argument lets tests verify dispatch
// and failure handling without opening a browser in a CI session.
[[nodiscard]] inline bool openBrowser(const char* const url
#if defined(_WIN32)
    , decltype(&::ShellExecuteW) launch = &::ShellExecuteW
#endif
)
{
    if (url == nullptr || *url == '\0')
        return false;
#if defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        url, -1, nullptr, 0);
    if (length == 0)
        return false;
    std::wstring wideUrl(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            url, -1, wideUrl.data(), length) == 0)
        return false;

    // Shell extensions can require COM. Balance our initialization without
    // changing an apartment already selected by the host (RPC_E_CHANGED_MODE).
    const HRESULT initialized = CoInitializeEx(nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto result = launch(nullptr, L"open", wideUrl.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__linux__)
    // Reap the short-lived shell; xdg-open runs in the background. Pass the
    // URL as an argument so it is never interpreted as shell code.
    char* const args[] = {
        const_cast<char*>("sh"), const_cast<char*>("-c"),
        const_cast<char*>("command -v xdg-open >/dev/null 2>&1 || exit 127; "
                          "xdg-open \"$1\" >/dev/null 2>&1 &"),
        const_cast<char*>("sh"), const_cast<char*>(url), nullptr};
    pid_t child = 0;
    if (posix_spawnp(&child, "sh", nullptr, nullptr, args, environ) != 0)
        return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    return false;
#endif
}

} // namespace sms::ui
