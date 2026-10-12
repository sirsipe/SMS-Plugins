#pragma once

namespace midichopper::ui {

enum class EditorAuditionMode { Play, Loop, Off };

[[nodiscard]] constexpr EditorAuditionMode nextEditorAuditionMode(
    const EditorAuditionMode mode) noexcept
{
    switch (mode) {
    case EditorAuditionMode::Play: return EditorAuditionMode::Loop;
    case EditorAuditionMode::Loop: return EditorAuditionMode::Off;
    case EditorAuditionMode::Off: return EditorAuditionMode::Play;
    }
    return EditorAuditionMode::Play;
}

[[nodiscard]] constexpr const char* editorAuditionLabel(const EditorAuditionMode mode) noexcept
{
    return mode == EditorAuditionMode::Loop ? "LOOP ON SELECT" : "PLAY ON SELECT";
}

[[nodiscard]] constexpr bool editorAuditionPressed(const EditorAuditionMode mode) noexcept
{
    return mode != EditorAuditionMode::Off;
}

} // namespace midichopper::ui
