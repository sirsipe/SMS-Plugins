#pragma once

#include "SkinFileIO.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace sms::ui {

/** Framework-neutral, immutable-by-convention skin snapshot. UI thread only. */
class Skin {
public:
    using Entries = std::map<std::string, std::string, std::less<>>;
    static constexpr std::size_t maxManifestBytes = 64 * 1024;
    static constexpr std::size_t maxTextBytes = 512;
    Entries colors, metrics, strings, images;
    std::string name;

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    /** Resolve native module paths for Windows, where DPF exposes ANSI metadata. */
    [[nodiscard]] static std::filesystem::path directoryForNativeBinary(
        const std::filesystem::path& binary, const std::filesystem::path& singleFileSkinName) noexcept
    {
        try {
            if (binary.empty()) return {};
            std::error_code error;
            if (!std::filesystem::is_regular_file(binary, error) || error) return {};
            const auto matches = [](const std::filesystem::path& component, std::string_view expected) {
                auto name = skinFileIO::pathToUtf8(component);
                for (char& byte : name)
                    if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + ('a' - 'A'));
                return name == expected;
            };
            const auto parent = binary.parent_path();
            if (matches(parent.extension(), ".lv2")) return parent / "skin";
            const auto contents = parent.parent_path();
            if (matches(contents.filename(), "contents") && matches(contents.parent_path().extension(), ".vst3"))
                return contents.parent_path() / "skin";
            if (singleFileSkinName.empty()) return {};
            return parent / singleFileSkinName;
        } catch (...) { return {}; }
    }

    /** DPF may identify a bundle directory or a single-file plugin as its bundle path. */
    [[nodiscard]] static std::filesystem::path directoryForPlugin(
        const char* bundlePath, const char* binaryPath, const char* singleFileSkinName) noexcept
    {
        try {
            if (bundlePath != nullptr && bundlePath[0] != '\0') {
                const auto bundle = skinFileIO::pathFromUtf8(bundlePath);
                std::error_code error;
                const auto status = std::filesystem::status(bundle, error);
                if (error) return {};
                if (std::filesystem::is_directory(status)) return bundle / "skin";
                if (!std::filesystem::is_regular_file(status)) return {};
                if (singleFileSkinName == nullptr || singleFileSkinName[0] == '\0') return {};
                return bundle.parent_path() / skinFileIO::pathFromUtf8(singleFileSkinName);
            }
            if (binaryPath == nullptr || binaryPath[0] == '\0' ||
                singleFileSkinName == nullptr || singleFileSkinName[0] == '\0') return {};
            return skinFileIO::pathFromUtf8(binaryPath).parent_path() /
                skinFileIO::pathFromUtf8(singleFileSkinName);
        } catch (...) {
            return {};
        }
    }

    [[nodiscard]] static bool parseColor(const std::string_view value,
                                         std::array<std::uint8_t, 4>& rgba) noexcept
    {
        if ((value.size() != 7 && value.size() != 9) || value.front() != '#')
            return false;
        std::array<std::uint8_t, 4> parsed{0, 0, 0, 255};
        const auto hex = [](const char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < (value.size() - 1) / 2; ++i) {
            const int high = hex(value[1 + i * 2]), low = hex(value[2 + i * 2]);
            if (high < 0 || low < 0) return false;
            parsed[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
        rgba = parsed;
        return true;
    }

    [[nodiscard]] static bool parseMetric(std::string_view value, float& result) noexcept
    {
        if (value.empty()) return false;
        if (value.front() == '+') {
            value.remove_prefix(1);
            if (!value.empty() && (value.front() == '-' || value.front() == '+')) return false;
        }
        if (value.empty()) return false;
        // libc++ streams accept hexadecimal floats; metrics allow decimals only.
        for (const char byte : value)
            if (!((byte >= '0' && byte <= '9') || byte == '.' || byte == '-' ||
                  byte == '+' || byte == 'e' || byte == 'E')) return false;
        try {
            // Xcode 15 libc++ lacks floating-point from_chars. A classic-locale
            // stream preserves decimal parsing without changing the host locale.
            std::istringstream input{std::string(value)};
            input.imbue(std::locale::classic());
            float parsed = 0;
            input >> std::noskipws >> parsed;
            if (input.bad() || !input.eof() || !std::isfinite(parsed)) return false;
            // libc++ reports ERANGE for representable subnormals as well.
            if (input.fail() && std::fpclassify(parsed) != FP_SUBNORMAL) return false;
            // Some libraries silently round underflow to zero; retain rejection
            // of nonzero values below the representable range.
            if (parsed == 0) {
                for (const char byte : value) {
                    if (byte == 'e' || byte == 'E') break;
                    if (byte >= '1' && byte <= '9') return false;
                }
            }
            result = parsed;
            return true;
        } catch (...) { return false; }
    }

    /** Returned pointer remains valid while this snapshot is alive and unmodified. */
    [[nodiscard]] const char* text(const char* defaultLiteral) const noexcept
    {
        if (defaultLiteral == nullptr) return "";
        const auto found = strings.find(defaultLiteral);
        return found == strings.end() ? defaultLiteral : found->second.c_str();
    }

    /** Translate printf text only when every conversion (including %%) stays identical.
        The returned pointer has the same lifetime as text(); use text() for plain labels. */
    [[nodiscard]] const char* format(const char* defaultPrintfPattern) const noexcept
    {
        if (defaultPrintfPattern == nullptr) return "";
        const auto found = strings.find(defaultPrintfPattern);
        if (found == strings.end()) return defaultPrintfPattern;
        std::string_view original{defaultPrintfPattern}, translated{found->second};
        for (;;) {
            std::string_view originalToken, translatedToken;
            if (!nextFormatToken(original, originalToken) ||
                !nextFormatToken(translated, translatedToken) || originalToken != translatedToken)
                return defaultPrintfPattern;
            if (originalToken.empty()) return found->second.c_str();
        }
    }

    /** Canonical file path validated when loaded; empty means use the built-in image. */
    [[nodiscard]] const std::string& imagePath(const std::string_view key) const noexcept
    {
        static const std::string empty;
        const auto found = images.find(key);
        return found == images.end() ? empty : found->second;
    }

    /** Missing, malformed or unsupported manifests yield an entirely empty snapshot. */
    [[nodiscard]] static Skin load(const std::filesystem::path& directory) noexcept
    {
        try {
            if (directory.empty()) return {};
            std::ifstream input(directory / "skin.ini", std::ios::binary);
            if (!input) return {};
            std::string contents(maxManifestBytes + 1, '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            const auto count = input.gcount();
            if (input.bad() || count > static_cast<std::streamsize>(maxManifestBytes)) return {};
            contents.resize(static_cast<std::size_t>(count));

            Skin result;
            bool hasVersion = false;
            bool hasName = false;
            std::string section;
            std::size_t position = 0;
            while (position < contents.size()) {
                const auto end = contents.find('\n', position);
                auto line = std::string_view(contents).substr(position,
                    end == std::string::npos ? contents.size() - position : end - position);
                position = end == std::string::npos ? contents.size() : end + 1;
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                line = trim(line);
                if (line.empty() || line.front() == '#' || line.front() == ';') continue;
                if (line.front() == '[') {
                    if (line.size() < 3 || line.back() != ']' ||
                        line.find(']', 1) != line.size() - 1) return {};
                    section = trim(line.substr(1, line.size() - 2));
                    if (section.empty()) return {};
                    continue;
                }
                if (section != "skin" && section != "colors" && section != "metrics" &&
                    section != "strings" && section != "images") {
                    if (section.empty()) return {};
                    continue;
                }
                const auto equals = line.find('=');
                if (equals == std::string_view::npos) return {};
                const auto key = trim(line.substr(0, equals));
                const auto value = trim(line.substr(equals + 1));
                if (key.empty()) return {};
                if (section == "skin") {
                    if (key == "version") {
                        if (hasVersion || value != "1") return {};
                        hasVersion = true;
                    } else if (key == "name") {
                        if (hasName || value.empty() || !validText(value)) return {};
                        result.name = value;
                        hasName = true;
                    }
                } else if (section == "colors") {
                    std::array<std::uint8_t, 4> rgba{};
                    if (validText(key) && parseColor(value, rgba))
                        result.colors[std::string(key)] = value;
                } else if (section == "metrics") {
                    float metric = 0;
                    if (validText(key) && parseMetric(value, metric))
                        result.metrics[std::string(key)] = value;
                } else if (section == "strings") {
                    if (validText(key) && validText(value))
                        result.strings[std::string(key)] = value;
                } else if ((key == "background" || key == "logo") && validText(value)) {
                    const auto path = confinedImage(directory, value);
                    if (!path.empty()) result.images[std::string(key)] = path;
                }
            }
            if (!hasVersion) return {};
            result.valid_ = true;
            return result;
        } catch (...) {
            return {};
        }
    }

private:
    bool valid_ = false;

    // Consume one complete fixed printf conversion without allocations. An empty token
    // means end of input; false means unsupported or malformed syntax.
    static bool nextFormatToken(std::string_view& remaining, std::string_view& token) noexcept
    {
        token = {};
        const auto percent = remaining.find('%');
        if (percent == std::string_view::npos) {
            remaining = {};
            return true;
        }
        remaining.remove_prefix(percent);
        const auto input = remaining;
        std::size_t offset = 1;
        if (offset < input.size() && input[offset] == '%') {
            token = input.substr(0, 2);
            remaining.remove_prefix(2);
            return true;
        }
        while (offset < input.size() && std::string_view{"-+ #0"}.find(input[offset]) !=
               std::string_view::npos) ++offset;
        const auto digit = [](const char c) { return c >= '0' && c <= '9'; };
        while (offset < input.size() && digit(input[offset])) ++offset;
        if (offset < input.size() && input[offset] == '.') {
            ++offset;
            while (offset < input.size() && digit(input[offset])) ++offset;
        }
        const auto lengthStart = offset;
        if (offset < input.size() && std::string_view{"hljztL"}.find(input[offset]) !=
            std::string_view::npos) {
            const char first = input[offset++];
            if ((first == 'h' || first == 'l') && offset < input.size() &&
                input[offset] == first) ++offset;
        }
        const auto length = input.substr(lengthStart, offset - lengthStart);
        if (offset == input.size()) return false;
        const char conversion = input[offset++];
        const bool integer = std::string_view{"diouxX"}.find(conversion) != std::string_view::npos;
        const bool floating = std::string_view{"fFeEgGaA"}.find(conversion) != std::string_view::npos;
        const bool character = conversion == 'c' || conversion == 's';
        if (!(integer || floating || character || conversion == 'p')) return false;
        if ((integer && length == "L") ||
            (floating && !(length.empty() || length == "l" || length == "L")) ||
            (character && !(length.empty() || length == "l")) ||
            (conversion == 'p' && !length.empty())) return false;
        token = input.substr(0, offset);
        remaining.remove_prefix(offset);
        return true;
    }

    static std::string_view trim(std::string_view value) noexcept
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        return value;
    }

    static bool validText(const std::string_view value) noexcept
    {
        if (value.size() > maxTextBytes) return false;
        for (std::size_t i = 0; i < value.size();) {
            const auto first = static_cast<unsigned char>(value[i++]);
            std::uint32_t code = first;
            unsigned trailing = 0;
            std::uint32_t minimum = 0;
            if (first >= 0xc2 && first <= 0xdf) { trailing = 1; code = first & 0x1f; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { trailing = 2; code = first & 0x0f; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { trailing = 3; code = first & 7; minimum = 0x10000; }
            else if (first >= 0x80) return false;
            while (trailing-- > 0) {
                if (i == value.size()) return false;
                const auto next = static_cast<unsigned char>(value[i++]);
                if ((next & 0xc0) != 0x80) return false;
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
                code < 0x20 || (code >= 0x7f && code <= 0x9f)) return false;
        }
        return true;
    }

    static std::string confinedImage(const std::filesystem::path& directory,
                                     const std::string_view value)
    {
        if (value.empty()) return {};
        const auto relative = skinFileIO::pathFromUtf8(value);
        if (relative.is_absolute() || relative.has_root_path()) return {};
        for (const auto& part : relative)
            if (part == "..") return {};
        std::error_code error;
        const auto root = std::filesystem::canonical(directory, error);
        if (error) return {};
        const auto path = std::filesystem::canonical(root / relative, error);
        if (error || !std::filesystem::is_regular_file(path, error) || error) return {};
        auto candidate = path.begin();
        for (const auto& part : root) {
            if (candidate == path.end() || *candidate != part) return {};
            ++candidate;
        }
        return skinFileIO::pathToUtf8(path);
    }
};

} // namespace sms::ui
