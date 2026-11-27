#pragma once

namespace error_help {
std::string explain_module(const std::filesystem::path &module_path);
std::string strip_colors(const std::string &text);

// Non-empty when a level cannot be loaded because its game-mode data
// (mp_common, zm_common, cp_common) is not installed.
std::string explain_missing_mode_for_level(const char *level);

// Whether a game mode's shared data ('mp', 'zm', 'cp') is installed at all.
bool mode_data_installed(const char *mode);

void Com_Error_LuiOutOfMemory(const char *file, int32_t line,
                              game::errorParm code, const char *fmt);
void Sys_Error_LuiOutOfMemory(const char *fmt);
} // namespace error_help
