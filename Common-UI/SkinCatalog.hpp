#pragma once

#include "Skin.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <unistd.h>

namespace sms::ui {

/** Startup-only filesystem catalog. IDs are immediate collection folder names. */
class SkinCatalog {
public:
    struct Entry { std::string id, name; std::filesystem::path directory; };
    bool forced = false;
    std::vector<Entry> entries{{"", "Default", {}}};

    [[nodiscard]] bool selectable() const noexcept { return !forced && entries.size() > 1; }
    [[nodiscard]] std::size_t selectedIndex(std::string_view id) const noexcept
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].id == id) return i;
        return 0;
    }
    [[nodiscard]] Skin load(std::size_t index) const noexcept
    {
        if (forced) return Skin::load(forcedDirectory_);
        return index < entries.size() ? Skin::load(entries[index].directory) : Skin{};
    }
    [[nodiscard]] static std::filesystem::path collectionDirectory(
        const std::filesystem::path& forcedDirectory)
    {
        if (forcedDirectory.empty()) return {};
        if (forcedDirectory.filename() == "skin") return forcedDirectory.parent_path() / "skins";
        auto result = forcedDirectory;
        if (result.extension() == ".skin") result.replace_extension(".skins");
        else return {};
        return result;
    }
    [[nodiscard]] static bool safeId(std::string_view id) noexcept
    {
        if (id.size() > Skin::maxTextBytes || id == "." || id == "..") return false;
        for (unsigned char c : id)
            if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false;
        return true;
    }
    [[nodiscard]] static SkinCatalog discover(const std::filesystem::path& forcedDirectory) noexcept
    {
        SkinCatalog result;
        try {
            std::error_code error;
            if (!forcedDirectory.empty() && std::filesystem::is_directory(forcedDirectory, error)) {
                result.forced = true;
                result.forcedDirectory_ = forcedDirectory;
                return result;
            }
            const auto collection = collectionDirectory(forcedDirectory);
            if (collection.empty()) return result;
            const auto root = std::filesystem::canonical(collection, error);
            if (error || !std::filesystem::is_directory(root, error) || error) return result;
            std::vector<Entry> candidates;
            std::filesystem::directory_iterator iterator(root, error), end;
            while (!error && iterator != end) {
                const auto path = iterator->path();
                const auto id = path.filename().string();
                std::error_code candidateError;
                const auto canonical = std::filesystem::canonical(path, candidateError);
                if (!candidateError && !id.empty() && safeId(id) &&
                    std::filesystem::is_directory(canonical, candidateError) && !candidateError &&
                    confined(root, canonical)) {
                    candidates.push_back({id, {}, canonical});
                }
                iterator.increment(error);
            }
            std::sort(candidates.begin(), candidates.end(),
                [](const Entry& a, const Entry& b) { return a.id < b.id; });
            for (auto& candidate : candidates) {
                const auto manifest = std::filesystem::canonical(candidate.directory / "skin.ini", error);
                if (error || !confined(candidate.directory, manifest)) continue;
                const auto skin = Skin::load(candidate.directory);
                if (!skin.valid() || skin.name.empty()) continue;
                candidate.name = skin.name;
                result.entries.push_back(std::move(candidate));
            }
            std::sort(result.entries.begin() + 1, result.entries.end(),
                [](const Entry& a, const Entry& b) {
                    return a.name == b.name ? a.id < b.id : a.name < b.name;
                });
        } catch (...) {
            // Preserve forced precedence even if discovering or parsing fails.
        }
        return result;
    }
private:
    std::filesystem::path forcedDirectory_;
    static bool confined(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        auto candidate = path.begin();
        for (const auto& part : root) {
            if (candidate == path.end() || *candidate != part) return false;
            ++candidate;
        }
        return candidate != path.end();
    }
};

/** Per-user choice, independent of host state. All operations are UI-thread only. */
class SkinPreference {
public:
    static constexpr std::size_t maxPreferenceBytes = 1024;
    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        try {
            std::filesystem::path root;
            if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
                root = xdg;
                if (!root.is_absolute()) root.clear();
            }
            if (root.empty()) {
                const char* home = std::getenv("HOME");
                if (!home || !*home || !std::filesystem::path(home).is_absolute()) return {};
                root = std::filesystem::path(home) / ".config";
            }
            return root / "SMS-AnvilSampler" / "selected-skin";
        } catch (...) { return {}; }
    }
    [[nodiscard]] static std::string read(const std::filesystem::path& path) noexcept
    {
        try {
            if (path.empty()) return {};
            std::ifstream input(path, std::ios::binary);
            if (!input) return {};
            std::string contents(maxPreferenceBytes + 1, '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (input.bad() || input.gcount() > static_cast<std::streamsize>(maxPreferenceBytes)) return {};
            contents.resize(static_cast<std::size_t>(input.gcount()));
            constexpr std::string_view header = "SMS-SKIN-PREFERENCE 1\n";
            if (contents.compare(0, header.size(), header) != 0 ||
                contents.size() <= header.size() || contents.back() != '\n') return {};
            const auto id = std::string_view(contents).substr(header.size(), contents.size() - header.size() - 1);
            return SkinCatalog::safeId(id) ? std::string(id) : std::string{};
        } catch (...) { return {}; }
    }
    [[nodiscard]] static bool write(const std::filesystem::path& path, std::string_view id) noexcept
    {
        int descriptor = -1;
        std::string temporary;
        try {
            if (path.empty() || path.filename().empty() || !SkinCatalog::safeId(id)) return false;
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;
            const auto contents = std::string("SMS-SKIN-PREFERENCE 1\n") + std::string(id) + "\n";
            temporary = (path.parent_path() / (path.filename().string() + ".tmp-XXXXXX")).string();
            descriptor = ::mkstemp(temporary.data());
            if (descriptor < 0) return false;
            std::size_t offset = 0;
            while (offset < contents.size()) {
                const auto count = ::write(descriptor, contents.data() + offset, contents.size() - offset);
                if (count <= 0) break;
                offset += static_cast<std::size_t>(count);
            }
            const bool closed = ::close(descriptor) == 0;
            descriptor = -1;
            if (offset == contents.size() && closed) {
                std::filesystem::rename(temporary, path, error);
                if (!error) return true;
            }
        } catch (...) {}
        if (descriptor >= 0) ::close(descriptor);
        if (!temporary.empty()) { std::error_code error; std::filesystem::remove(temporary, error); }
        return false;
    }
};

} // namespace sms::ui
