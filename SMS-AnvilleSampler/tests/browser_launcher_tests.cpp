#include "BrowserLauncher.hpp"
#include "HelpLinks.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void check(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

#if defined(_WIN32)
std::wstring expectedUrl;
INT_PTR launchResult = 33;
unsigned launchCount = 0;

HINSTANCE WINAPI fakeLaunch(HWND parent, LPCWSTR operation, LPCWSTR url,
    LPCWSTR parameters, LPCWSTR directory, INT show)
{
    ++launchCount;
    check(parent == nullptr && std::wstring(operation) == L"open",
        "use the native open verb");
    check(std::wstring(url) == expectedUrl, "forward the complete Unicode URL");
    check(parameters == nullptr && directory == nullptr && show == SW_SHOWNORMAL,
        "use the default browser without command-line parameters");
    return reinterpret_cast<HINSTANCE>(launchResult);
}

void platformChecks()
{
    for (const auto& url : {std::string(midichopper::ui::kMoreSkinsUrl),
            std::string(midichopper::ui::kIssueUrl),
            midichopper::ui::onlineHelpUrl("main"),
            midichopper::ui::onlineHelpUrl("SMS-AnvilleSampler-v0.9.0-beta.1")}) {
        expectedUrl.assign(url.begin(), url.end());
        const auto previousCount = launchCount;
        check(sms::ui::openBrowser(url.c_str(), fakeLaunch), "dispatch each menu URL");
        check(launchCount == previousCount + 1, "launch each URL once");
    }
    expectedUrl = L"https://example.org/\u96ea?q=one&other=two";
    check(sms::ui::openBrowser("https://example.org/\xE9\x9B\xAA?q=one&other=two",
        fakeLaunch), "convert UTF-8 without interpreting URL metacharacters");
    const auto previousCount = launchCount;
    check(!sms::ui::openBrowser("https://example.org/\xFF", fakeLaunch),
        "reject malformed UTF-8");
    check(launchCount == previousCount, "invalid UTF-8 never reaches the shell API");

    expectedUrl = L"https://example.org";
    for (const INT_PTR error : {0, 2, 5, 31, 32}) {
        launchResult = error;
        check(!sms::ui::openBrowser("https://example.org", fakeLaunch),
            "native launch errors report failure");
    }
    launchResult = 33;
    check(sms::ui::openBrowser("https://example.org", fakeLaunch),
        "native success boundary reports success");
}
#elif defined(__linux__)
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("sms-browser-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string oldPath = std::getenv("PATH") ? std::getenv("PATH") : "";
    bool hadPath = std::getenv("PATH") != nullptr;
    Fixture()
    {
        std::filesystem::create_directory(directory);
        std::filesystem::create_symlink("/bin/sh", directory / "sh");
        check(setenv("PATH", directory.c_str(), 1) == 0, "isolate launcher lookup");
    }
    ~Fixture()
    {
        if (hadPath) setenv("PATH", oldPath.c_str(), 1);
        else unsetenv("PATH");
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

void platformChecks()
{
    Fixture fixture;
    check(!sms::ui::openBrowser(midichopper::ui::kIssueUrl.data()),
        "missing xdg-open reports failure");
    const auto launcher = fixture.directory / "xdg-open";
    {
        std::ofstream script(launcher);
        script << "#!/bin/sh\nprintf '%s' \"$1\" > \"$0.url\"\n";
    }
    std::filesystem::permissions(launcher, std::filesystem::perms::owner_all);
    for (const auto& url : {std::string(midichopper::ui::kMoreSkinsUrl),
            std::string(midichopper::ui::kIssueUrl),
            midichopper::ui::onlineHelpUrl("main"),
            std::string("https://example.org/?a=';\"&b=$(echo BAD)`echo BAD`")}) {
        const auto captured = fixture.directory / "xdg-open.url";
        std::filesystem::remove(captured);
        check(sms::ui::openBrowser(url.c_str()), "dispatch URL through xdg-open");
        std::string received;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            std::ifstream input(captured);
            received.assign(std::istreambuf_iterator<char>(input), {});
            if (received == url) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        check(received == url, "forward the URL literally as one argument");
    }
    std::filesystem::remove(fixture.directory / "sh");
    check(!sms::ui::openBrowser(midichopper::ui::kIssueUrl.data()),
        "missing shell reports failure");
}
#else
void platformChecks()
{
    check(!sms::ui::openBrowser(midichopper::ui::kIssueUrl.data()),
        "unsupported platforms report failure");
}
#endif

} // namespace

int main()
{
    try {
        check(!sms::ui::openBrowser(nullptr), "null URL reports failure");
        check(!sms::ui::openBrowser(""), "empty URL reports failure");
        platformChecks();
        std::cout << "Browser launcher tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
