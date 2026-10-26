#pragma once

#include "Skin.hpp"
#include "IO/DefaultsFile.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

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
                const auto id = skinFileIO::pathToUtf8(path.filename());
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
    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        return sms::io::DefaultsFile::configPath("SMS-AnvilleSampler");
    }
    [[nodiscard]] static std::string read(const std::filesystem::path& path) noexcept
    {
        try {
            auto id = sms::io::DefaultsFile::get(path, "skin");
            return SkinCatalog::safeId(id) ? std::move(id) : std::string{};
        } catch (...) { return {}; }
    }
    [[nodiscard]] static bool write(const std::filesystem::path& path, std::string_view id) noexcept
    {
        return SkinCatalog::safeId(id) && sms::io::DefaultsFile::set(path, "skin", id);
    }
};

} // namespace sms::ui
