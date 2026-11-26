#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace client_binary {
// The Black Ops 3 build this client loads: the executable released Sept 10,
// 2026. The unranked dedicated server is supported_server_checksum.
constexpr uint32_t supported_checksum = 0x6531394;
constexpr uint32_t supported_server_checksum = 0x14C28B4;
constexpr const char *supported_checksum_hex = "0x6531394";
constexpr const char *backup_suffix = ".boiii_backup";

enum class state : uint8_t { supported, incompatible, unreadable };

std::optional<uint32_t> pe_checksum(const std::filesystem::path &file);

state classify(const std::filesystem::path &client_binary);

bool is_process_running();

std::filesystem::path backup_path(const std::filesystem::path &client_binary);

// Downloads the compatible BlackOps3.exe, verifies it, keeps the player's own
// file as <name>.boiii_backup and installs it. Throws on failure, restoring
// the backup. Returns true when the install is done.
bool install(const std::filesystem::path &client_binary,
             bool allow_close_running_game);

// Called before the game starts: does nothing for a supported build, offers
// the download for an incompatible one, and throws for unreadable or cancelled.
void ensure_compatible(const std::filesystem::path &client_binary);
} // namespace client_binary
