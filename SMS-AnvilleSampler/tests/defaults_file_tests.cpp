#include "IO/DefaultsFile.hpp"
#include "IO/FileIO.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#if !defined(_WIN32)
# include <sys/wait.h>
# include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
using sms::io::DefaultsFile;

void check(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path directory;
    Fixture()
    {
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory = fs::temp_directory_path() / sms::io::pathFromUtf8(
                "sms-defaults-雪-Ääni-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(directory, error)) return;
            check(!error, "create isolated Unicode directory");
        }
        throw std::runtime_error("unique temporary directory");
    }
    ~Fixture() { std::error_code error; fs::remove_all(directory, error); }
    fs::path path() const { return directory / "defaults.ini"; }
    void write(const std::string& contents) const
    {
        std::ofstream output(path(), std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        check(static_cast<bool>(output), "write fixture bytes");
    }
    std::string contents() const
    {
        const auto bytes = sms::io::readBytes(path(), 128 * 1024);
        return {bytes.begin(), bytes.end()};
    }
};

void parsingAndMerge()
{
    Fixture fixture;
    check(DefaultsFile::read(fixture.path()).empty(), "missing defaults are empty");
    check(DefaultsFile::read({}).empty(), "empty path defaults are empty");
    fixture.write(" # a comment\n; another comment\r\n"
                  " layout = 12 \r\n"
                  "bank_mode=selected\n"
                  "knob.output=KC1;0=1;127=0.5\n"
                  "future.midi.channel=2\n"
                  "skin=雪=metal;#edition\n"
                  "bad key=x\nwithout-equals\n"
                  "bad.quote=\"unterminated\n"
                  "bad.escape=\"unknown\\q\"\n"
                  "layout=8\n"
                  "empty=\n");
    const auto values = DefaultsFile::read(fixture.path());
    check(values.size() == 6, "ignore malformed lines without losing valid siblings");
    check(values.at("layout") == "8", "trim whitespace and let final valid duplicate win");
    check(values.at("knob.output") == "KC1;0=1;127=0.5", "keep embedded semicolons and equals");
    check(values.at("skin") == "雪=metal;#edition", "UTF-8 and inline comment characters survive");
    check(values.at("empty").empty(), "empty value is valid");
    check(DefaultsFile::get(fixture.path(), "missing").empty(), "unknown key has empty value");
    check(DefaultsFile::set(fixture.path(), "layout", "16"), "update one key");
    check(DefaultsFile::get(fixture.path(), "future.midi.channel") == "2", "preserve unknown keys");
    check(DefaultsFile::get(fixture.path(), "knob.output") == "KC1;0=1;127=0.5", "preserve sibling codec");
    for (const std::string value : {" 雪 ", "\"quoted\"", "\"back\\slash", "\\raw", "", "\t padded\t"}) {
        check(DefaultsFile::set(fixture.path(), "skin", value), "write exact skin value");
        check(DefaultsFile::get(fixture.path(), "skin") == value, "exact quoted/unquoted value roundtrip");
    }
    std::string malformed = "layout=12\n";
    malformed += "bad=nul";
    malformed += '\0';
    malformed += "x\nbank_mode=all\n";
    malformed += "bad.large=" + std::string(4097, 'x') + "\nskin=metal\n";
    fixture.write(malformed);
    check(DefaultsFile::read(fixture.path()).size() == 3, "NUL and oversized entries leave siblings intact");
    check(DefaultsFile::set(fixture.path(), "layout", "8"), "merge valid siblings in malformed file");
    check(DefaultsFile::get(fixture.path(), "skin") == "metal", "retain sibling after malformed merge");
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(fixture.directory)) {
        check(entry.path().filename() == "defaults.ini", "no persistent locks or temporary files");
        ++count;
    }
    check(count == 1, "exactly one persistent preferences file");
}

void boundsAndFailures()
{
    Fixture fixture;
    check(DefaultsFile::set(fixture.path(), "layout", "12"), "prepare valid defaults");
    const auto original = fixture.contents();
    for (const std::string& key : std::vector<std::string>{"", "bad key", "bad=key", "bad\nkey", std::string(129, 'k')})
        check(!DefaultsFile::set(fixture.path(), key, "x"), "reject unsafe keys");
    for (const std::string& value : {std::string("x\0y", 3), std::string("x\ny"),
                                    std::string("x\ry"), std::string(4097, 'x')})
        check(!DefaultsFile::set(fixture.path(), "layout", value), "reject injection and oversized values");
    check(fixture.contents() == original, "invalid writes preserve existing bytes");
    check(!DefaultsFile::set({}, "key", "value"), "empty destination fails");
    check(!DefaultsFile::set(fixture.directory, "key", "value"), "directory destination fails");
    check(!DefaultsFile::set(fixture.path() / "defaults.ini", "key", "value"), "file parent fails");
    fixture.write(std::string(64 * 1024 + 1, 'x'));
    check(DefaultsFile::read(fixture.path()).empty(), "oversized file reads factory defaults");
    check(!DefaultsFile::set(fixture.path(), "layout", "8"), "oversized existing file rejects writes");
    check(fixture.contents().size() == 64 * 1024 + 1, "oversized file remains intact");
    std::string entries;
    for (unsigned i = 0; i < 257; ++i) entries += "future." + std::to_string(i) + "=x\n";
    fixture.write(entries);
    check(DefaultsFile::read(fixture.path()).size() == 256, "entry count is bounded");
    check(!DefaultsFile::set(fixture.path(), "layout", "8"), "reject merge that would drop excess keys");
    check(fixture.contents() == entries, "entry count failure preserves original bytes");
    fixture.write({});
    for (unsigned i = 0; i < 15; ++i)
        check(DefaultsFile::set(fixture.path(), "large." + std::to_string(i), std::string(4096, 'v')),
              "maximum value size fits inside file bound");
    const auto full = fixture.contents();
    check(!DefaultsFile::set(fixture.path(), "large.15", std::string(4096, 'v')),
          "reject serialized file exceeding total byte limit");
    check(fixture.contents() == full, "total byte limit failure preserves original bytes");

    check(DefaultsFile::configPath({}).empty(), "reject empty application directory");
    for (const auto name : {".", "..", "../outside", "nested/path", "nested\\path"})
        check(DefaultsFile::configPath(name).empty(), "reject application path traversal");
    const auto config = DefaultsFile::configPath("SMS-AnvilleSampler");
    if (!config.empty()) {
        check(config.filename() == "defaults.ini", "single defaults filename");
        check(config.parent_path().filename() == "SMS-AnvilleSampler", "application config directory");
    }
}

void threadMerges()
{
    Fixture fixture;
    constexpr unsigned workers = 6;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> ok{true};
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done.load()) {
            for (const auto& [key, value] : DefaultsFile::read(fixture.path())) {
                if (key.find("thread.") != 0 || value.empty() ||
                        value.find_first_not_of("0123456789") != std::string::npos) ok = false;
            }
        }
    });
    std::vector<std::thread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] {
            ready.fetch_add(1);
            while (!go.load()) std::this_thread::yield();
            for (unsigned i = 0; i < 24; ++i) {
                const auto key = "thread." + std::to_string(worker);
                if (!DefaultsFile::set(fixture.path(), key, std::to_string(i))) ok = false;
            }
        });
    }
    while (ready.load() != workers) std::this_thread::yield();
    go = true;
    for (auto& thread : threads) thread.join();
    done = true;
    reader.join();
    check(ok, "concurrent thread writes and atomic readers succeed");
    const auto values = DefaultsFile::read(fixture.path());
    check(values.size() == workers, "threads retain each other's different keys");
    for (unsigned worker = 0; worker < workers; ++worker)
        check(values.at("thread." + std::to_string(worker)) == "23", "thread final value retained");
}

#if !defined(_WIN32)
void processMerges()
{
    Fixture fixture;
    constexpr unsigned workers = 4;
    int barrier[2];
    check(::pipe(barrier) == 0, "create process start barrier");
    std::vector<pid_t> children;
    for (unsigned worker = 0; worker < workers; ++worker) {
        const auto child = ::fork();
        check(child >= 0, "fork defaults writer");
        if (child == 0) {
            ::close(barrier[1]);
            char signal;
            if (::read(barrier[0], &signal, 1) != 1) ::_exit(2);
            ::close(barrier[0]);
            for (unsigned i = 0; i < 24; ++i) {
                if (!DefaultsFile::set(fixture.path(), "process." + std::to_string(worker),
                                       std::to_string(i))) ::_exit(3);
            }
            ::_exit(0);
        }
        children.push_back(child);
    }
    ::close(barrier[0]);
    check(::write(barrier[1], "xxxx", workers) == workers, "release concurrent writers");
    ::close(barrier[1]);
    bool ok = true;
    for (const auto child : children) {
        int status = 0;
        if (::waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = false;
    }
    check(ok, "concurrent process writes succeed");
    const auto values = DefaultsFile::read(fixture.path());
    check(values.size() == workers, "processes retain each other's different keys");
    for (unsigned worker = 0; worker < workers; ++worker)
        check(values.at("process." + std::to_string(worker)) == "23", "process final value retained");
}
#endif
} // namespace

int main()
{
    try {
        parsingAndMerge();
        boundsAndFailures();
        threadMerges();
#if !defined(_WIN32)
        processMerges();
#endif
        std::cout << "defaults file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "defaults file tests failed: " << error.what() << '\n';
        return 1;
    }
}
