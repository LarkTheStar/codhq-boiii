#include <std_include.hpp>

#include <curl/curl.h>

#include "client_binary.hpp"

#include <game/flags.hpp>

#include <utils/cryptography.hpp>
#include <utils/http.hpp>
#include <utils/io.hpp>
#include <utils/progress_ui.hpp>

#include <chrono>
#include <format>
#include <fstream>

#include <tlhelp32.h>

namespace client_binary {
namespace {
constexpr const char *SUPPORTED_CLIENT_PATCH_URL =
    "https://archive.org/download/black-ops-3_20260926/BlackOps3.exe";
constexpr const char *BACKUP_SUPPORTED_CLIENT_PATCH_URL =
    "https://archive.org/download/black-ops-3_20260927/BlackOps3.exe";
constexpr const char *SUPPORTED_CLIENT_PATCH_SHA1 =
    "9D03F81086112113BFB1DD22B8538B9398AC3B9B";

constexpr const char *BACKUP_FILE_SUFFIX = ".boiii_backup";

struct patch_install_cancelled final : std::exception {};

std::string to_lower(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

bool has_expected_client_patch_hash(const std::filesystem::path &file) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream.is_open()) {
    return false;
  }

  return to_lower(utils::cryptography::sha1::compute(stream, true)) ==
         to_lower(SUPPORTED_CLIENT_PATCH_SHA1);
}

std::vector<uint32_t> get_running_client_binary_process_ids() {
  std::vector<uint32_t> pids{};
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return pids;
  }

  auto _ = gsl_lite::finally([&]() { CloseHandle(snapshot); });

  PROCESSENTRY32W process_entry{};
  process_entry.dwSize = sizeof(process_entry);
  const DWORD self_pid = GetCurrentProcessId();

  if (Process32FirstW(snapshot, &process_entry) != 0) {
    while (Process32NextW(snapshot, &process_entry) != 0) {
      if (process_entry.th32ProcessID != self_pid &&
          _wcsicmp(process_entry.szExeFile, L"BlackOps3.exe") == 0) {
        pids.emplace_back(process_entry.th32ProcessID);
      }
    };
  }

  return pids;
}

void close_running_client_binary_processes() {
  const std::vector<uint32_t> pids = get_running_client_binary_process_ids();
  for (const uint32_t pid : pids) {
    const HANDLE process =
        OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
      throw std::runtime_error(
          "Failed to access the running BlackOps3.exe process before "
          "installing the patch.");
    }

    auto _ = gsl_lite::finally([&]() { CloseHandle(process); });

    if (TerminateProcess(process, 0) == 0) {
      const DWORD error = GetLastError();
      throw std::runtime_error(
          std::string("Failed to close the running BlackOps3.exe process "
                      "before installing the patch (error ") +
          std::to_string(error) + ").");
    }

    const DWORD wait_result = WaitForSingleObject(process, 15000);
    if (wait_result != WAIT_OBJECT_0) {
      throw std::runtime_error(
          "BlackOps3.exe did not close in time for the patch install.");
    }
  }
}

std::string get_client_patch_prompt_message(const bool close_running_game) {
  const char *close_message =
      close_running_game
          ? "\n\nBlack Ops 3 is already running. BOIII will close it when "
            "the patch is ready, then continue launch."
          : "";

  return std::string(
             "The installed BlackOps3.exe version is not compatible with "
             "this BOIII build.\n\n"
             "BOIII needs the Black Ops 3 executable released Sept 10, "
             "2026 (PE checksum 0x6531394).\n\n"
             "BOIII can download that compatible BlackOps3.exe now "
             "(106,059,256 bytes), check its SHA-1 and PE checksum, and "
             "install it. Your current file is kept as "
             "BlackOps3.exe.boiii_backup.\n\n"
             "Press OK to download and install it, or Cancel to keep your "
             "current BlackOps3.exe.") +
         close_message;
}

bool prompt_to_install_client_patch(const bool close_running_game) {
  return !game::is_headless() &&
         MessageBoxA(
             nullptr,
             get_client_patch_prompt_message(close_running_game).c_str(),
             "BOIII Patch Installer",
             MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) ==
             IDOK;
}

std::string format_download_size(const size_t bytes) {
  const double kilobytes = static_cast<double>(bytes) / 1024.0;
  const double megabytes = kilobytes / 1024.0;
  const double gigabytes = megabytes / 1024.0;

  if (gigabytes >= 1.0) {
    return std::format("{:.2f} GB", gigabytes);
  }
  if (megabytes >= 1.0) {
    return std::format("{:.2f} MB", megabytes);
  }
  if (kilobytes >= 1.0) {
    return std::format("{:.2f} KB", kilobytes);
  }
  return std::format("{} B", bytes);
}

void install_supported_client_binary(
    const std::string_view &url, const std::filesystem::path &client_binary,
    const bool allow_close_running_client_binary) {
  utils::progress_ui progress(false);
  progress.set_title("BOIII Patch Installer");
  progress.set_line(1, "Downloading compatible BlackOps3.exe...");
  progress.set_line(2, "Preparing download...");
  progress.show(true);

  const std::filesystem::path temp_binary =
      std::filesystem::path(client_binary.string() + ".boiii_download");
  const std::filesystem::path backup_binary = backup_path(client_binary);

  auto cleanup_temp =
      gsl_lite::finally([&]() { utils::io::remove_file(temp_binary); });

  std::ofstream temp_stream(temp_binary, std::ios::binary | std::ios::trunc);
  if (!temp_stream.is_open()) {
    throw std::runtime_error("Failed to create the temporary patch file in "
                             "your game directory.");
  }

  std::chrono::steady_clock::time_point last_progress_update =
      std::chrono::steady_clock::time_point{};
  bool has_total_size = false;
  size_t latest_total_size = 0;
  const int32_t curl_code = utils::http::get_data_stream(
      url, {},
      [&](const size_t downloaded, const size_t total_size) {
        if (progress.is_cancelled()) {
          throw patch_install_cancelled{};
        }

        latest_total_size = total_size;

        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (last_progress_update == std::chrono::steady_clock::time_point{} ||
            (now - last_progress_update) >= std::chrono::milliseconds(125)) {
          if (total_size > 0) {
            if (!has_total_size) {
              progress.set_marquee(false);
              has_total_size = true;
            }

            progress.set_progress(downloaded, total_size);
            progress.set_line(2, "Downloaded " +
                                     format_download_size(downloaded) + " / " +
                                     format_download_size(total_size));
          } else {
            progress.set_line(2,
                              "Downloaded " + format_download_size(downloaded));
          }

          last_progress_update = now;
        }
      },
      [&](const char *data, const size_t size) {
        temp_stream.write(data, static_cast<std::streamsize>(size));
        if (!temp_stream || !temp_stream.is_open()) {
          throw std::runtime_error(
              "Failed while writing the downloaded BlackOps3.exe patch.");
        }
      });

  temp_stream.flush();
  temp_stream.close();

  if (progress.is_cancelled()) {
    throw patch_install_cancelled{};
  }

  if (curl_code != CURLE_OK) {
    throw std::runtime_error("Failed to download the compatible BlackOps3.exe "
                             "patch. Please try again later.");
  }

  progress.set_marquee(false);
  progress.set_progress(latest_total_size > 0 ? latest_total_size : 1,
                        latest_total_size > 0 ? latest_total_size : 1);
  progress.set_line(1, "Verifying downloaded BlackOps3.exe...");
  progress.set_line(2, temp_binary.filename().string());

  const std::optional<uint32_t> downloaded_checksum = pe_checksum(temp_binary);
  if (!has_expected_client_patch_hash(temp_binary) || !downloaded_checksum ||
      *downloaded_checksum != supported_checksum) {
    throw std::runtime_error(
        "The downloaded BlackOps3.exe patch did not match the BOIII-"
        "compatible version that this build expects.");
  }

  if (is_process_running()) {
    if (!allow_close_running_client_binary) {
      throw std::runtime_error(
          "Black Ops 3 is still running, so BOIII cannot replace "
          "BlackOps3.exe.");
    }

    progress.set_line(1, "Closing running BlackOps3.exe...");
    progress.set_line(2, "Waiting for Black Ops 3 to exit...");
    close_running_client_binary_processes();
  }

  utils::io::remove_file(backup_binary);
  const bool moved_original = MoveFileExW(
      client_binary.wstring().c_str(), backup_binary.wstring().c_str(),
      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!moved_original) {
    throw std::runtime_error("Failed to move your current BlackOps3.exe out of "
                             "the way before installing the patch.");
  }

  bool installed = false;
  const auto restore_backup = gsl_lite::finally([&]() {
    if (!installed) {
      MoveFileExW(backup_binary.wstring().c_str(),
                  client_binary.wstring().c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
  });

  progress.set_line(1, "Replacing BlackOps3.exe...");
  progress.set_line(2, client_binary.string());

  if (MoveFileExW(temp_binary.wstring().c_str(),
                  client_binary.wstring().c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    throw std::runtime_error(
        "Failed to replace BlackOps3.exe with the downloaded patch.");
  }

  if (classify(client_binary) != state::supported) {
    throw std::runtime_error("BlackOps3.exe was replaced, but the new file is "
                             "still not compatible with this BOIII build.");
  }

  installed = true;
  progress.set_line(1, "BlackOps3.exe updated successfully.");
  progress.set_line(2, "Your previous file is kept as " +
                           backup_binary.filename().string());
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
}
} // namespace

std::filesystem::path backup_path(const std::filesystem::path &client_binary) {
  return std::filesystem::path(client_binary.string() + BACKUP_FILE_SUFFIX);
}

std::optional<uint32_t> pe_checksum(const std::filesystem::path &file) {
  std::ifstream stream(file, std::ios::binary);
  if (stream.is_open()) {
    IMAGE_DOS_HEADER dos_header{};
    stream.read(reinterpret_cast<char *>(&dos_header), sizeof(dos_header));
    if (stream && dos_header.e_magic == IMAGE_DOS_SIGNATURE) {
      stream.seekg(dos_header.e_lfanew, std::ios::beg);

      uint32_t signature = 0;
      stream.read(reinterpret_cast<char *>(&signature), sizeof(signature));
      if (stream && signature == IMAGE_NT_SIGNATURE) {
        IMAGE_FILE_HEADER file_header{};
        stream.read(reinterpret_cast<char *>(&file_header),
                    sizeof(file_header));
        if (stream) {
          WORD optional_magic = 0;
          stream.read(reinterpret_cast<char *>(&optional_magic),
                      sizeof(optional_magic));
          if (stream) {
            stream.seekg(-static_cast<std::streamoff>(sizeof(optional_magic)),
                         std::ios::cur);

            if (optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
              IMAGE_OPTIONAL_HEADER64 optional_header{};
              stream.read(reinterpret_cast<char *>(&optional_header),
                          sizeof(optional_header));
              if (stream) {
                return optional_header.CheckSum;
              }
            }

            if (optional_magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
              IMAGE_OPTIONAL_HEADER32 optional_header{};
              stream.read(reinterpret_cast<char *>(&optional_header),
                          sizeof(optional_header));
              if (stream) {
                return optional_header.CheckSum;
              }
            }
          }
        }
      }
    }
  }

  return std::nullopt;
}

state classify(const std::filesystem::path &client_binary) {
  const std::optional<uint32_t> checksum = pe_checksum(client_binary);
  if (!checksum.has_value()) {
    return state::unreadable;
  }

  if (*checksum == supported_checksum) {
    return state::supported;
  }

  return state::incompatible;
}

bool is_process_running() {
  return !get_running_client_binary_process_ids().empty();
}

bool install(const std::filesystem::path &client_binary,
             const bool allow_close_running_game) {
  try {
    install_supported_client_binary(SUPPORTED_CLIENT_PATCH_URL, client_binary,
                                    allow_close_running_game);
    return true;
  } catch (const patch_install_cancelled &) {
    throw std::runtime_error(
        "The BlackOps3.exe download was cancelled, so your file was left "
        "exactly as it was. Open Settings => Patches to install it later.");
  } catch (...) {
    install_supported_client_binary(BACKUP_SUPPORTED_CLIENT_PATCH_URL,
                                    client_binary, allow_close_running_game);
    return true;
  }
}

void ensure_compatible(const std::filesystem::path &client_binary) {
  switch (classify(client_binary)) {
  case state::supported:
    return;
  case state::unreadable:
    throw std::runtime_error("BlackOps3.exe could not be read. Check that the "
                             "game folder is complete, then verify the game "
                             "files in Steam.");
  case state::incompatible:
    break;
  }

  const bool close_running_game = is_process_running();
  if (!prompt_to_install_client_patch(close_running_game)) {
    throw std::runtime_error(
        "This BlackOps3.exe is not the game build CodHQ BOIII supports (PE "
        "checksum 0x6531394), so it was left exactly as it was.\n\n"
        "Start again and press OK on the patch prompt, or open Settings => "
        "Patches to install the compatible BlackOps3.exe. In Steam you can "
        "also use Properties > Installed Files > Verify integrity of game "
        "files.");
  }

  install(client_binary, close_running_game);
}
} // namespace client_binary
