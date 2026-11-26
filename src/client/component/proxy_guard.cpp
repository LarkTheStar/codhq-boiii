#include <std_include.hpp>

#include <loader/component_loader.hpp>

#include <game/game.hpp>

#include <utils/io.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <tlhelp32.h>
#include <winsvc.h>

#pragma comment(lib, "advapi32.lib")

// Software that sits between the game and the network (a proxying DLL dropped
// next to the exe, WinDivert, GoodbyeDPI, a layered service provider) makes
// BOIII fail in ways that look like our bugs. This finds it once at startup and
// says exactly what to remove. It never patches around it and never writes to
// the game folder.

namespace proxy_guard {
namespace {
constexpr const wchar_t *PROXY_DLLS[] = {
    L"winmm.dll",     L"version.dll",     L"d3d9.dll",     L"dxgi.dll",
    L"d3d11.dll",     L"dinput8.dll",     L"dsound.dll",   L"xinput1_3.dll",
    L"xinput1_4.dll", L"xinput9_1_0.dll", L"winhttp.dll",  L"wininet.dll",
    L"wsock32.dll",   L"ws2_32.dll",      L"iphlpapi.dll", L"mswsock.dll",
    L"dbghelp.dll",   L"winspool.drv",    L"secur32.dll",  L"cryptbase.dll",
    L"WinDivert.dll",
};

constexpr const wchar_t *WINDIVERT_SERVICES[] = {
    L"WinDivert",    L"WinDivert1.0", L"WinDivert1.1", L"WinDivert1.2",
    L"WinDivert1.3", L"WinDivert1.4", L"WinDivert1.5", L"WinDivert1.6",
};

constexpr const wchar_t *PROXY_SERVICES[] = {L"GoodbyeDPI", L"goodbyedpi",
                                             L"GoodbyeDPIService"};

std::string to_utf8(const std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

bool iequals(const std::wstring_view left, const std::wstring_view right) {
  return left.size() == right.size() &&
         std::equal(
             left.begin(), left.end(), right.begin(),
             [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

std::wstring lower_wstring(std::wstring text) {
  std::ranges::transform(text, text.begin(),
                         [](wchar_t c) { return towlower(c); });
  return text;
}

std::wstring system_directory() {
  wchar_t buffer[MAX_PATH]{};
  const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
  return std::wstring(buffer, length);
}

bool is_in_windows(const std::filesystem::path &path) {
  const std::wstring system = lower_wstring(system_directory());
  return !system.empty() &&
         lower_wstring(path.wstring()).starts_with(system + L"\\");
}

bool service_running(SC_HANDLE manager, const wchar_t *name) {
  const SC_HANDLE service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS);
  if (!service) {
    return false;
  }
  SERVICE_STATUS status{};
  const bool running = QueryServiceStatus(service, &status) != 0 &&
                       status.dwCurrentState != SERVICE_STOPPED;
  CloseServiceHandle(service);
  return running;
}

void check_services(std::vector<std::string> &findings) {
  const SC_HANDLE manager =
      OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) {
    return;
  }
  for (const wchar_t *name : WINDIVERT_SERVICES) {
    if (service_running(manager, name)) {
      findings.emplace_back(std::format(
          "the WinDivert service '{}' is running (packet redirection)",
          to_utf8(name)));
      break;
    }
  }
  for (const wchar_t *name : PROXY_SERVICES) {
    if (service_running(manager, name)) {
      findings.emplace_back(std::format(
          "the proxy service '{}' is running (GoodbyeDPI-style redirection)",
          to_utf8(name)));
      break;
    }
  }
  CloseServiceHandle(manager);
}

void check_driver_files(std::vector<std::string> &findings) {
  WIN32_FIND_DATAW data{};
  const std::wstring pattern =
      system_directory() + L"\\drivers\\WinDivert*.sys";
  const HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) {
    return;
  }
  findings.emplace_back(std::format(
      "the WinDivert driver '{}' is installed in the system driver folder",
      to_utf8(data.cFileName)));
  FindClose(find);
}

void check_processes(std::vector<std::string> &findings) {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry) != 0) {
    do {
      const std::wstring name = lower_wstring(entry.szExeFile);
      if (name.find(L"goodbyedpi") != std::wstring::npos) {
        findings.emplace_back(
            std::format("the process '{}' is running (GoodbyeDPI)",
                        to_utf8(entry.szExeFile)));
        break;
      }
    } while (Process32NextW(snapshot, &entry) != 0);
  }
  CloseHandle(snapshot);
}

void check_game_folder(std::vector<std::string> &findings) {
  const std::filesystem::path folder = game::get_game_path();
  for (const wchar_t *name : PROXY_DLLS) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(folder / name, ec)) {
      findings.emplace_back(std::format(
          "'{}' is next to BlackOps3.exe, so it is loaded instead of the "
          "Windows copy",
          to_utf8(name)));
    }
  }
}

void check_loaded_modules(std::vector<std::string> &findings) {
  const HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Module32FirstW(snapshot, &entry) != 0) {
    do {
      const std::filesystem::path path = entry.szExePath;
      if (is_in_windows(path)) {
        continue;
      }
      for (const wchar_t *name : PROXY_DLLS) {
        if (iequals(entry.szModule, name)) {
          findings.emplace_back(
              std::format("the proxy DLL '{}' is loaded into the game",
                          to_utf8(path.wstring())));
          break;
        }
      }
    } while (Module32NextW(snapshot, &entry) != 0);
  }
  CloseHandle(snapshot);
}

// A layered service provider shows up in the Winsock catalog with its own DLL
// path; Windows' own entries all live in the system folder.
void check_winsock_catalog(std::vector<std::string> &findings) {
  HKEY catalog{};
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"SYSTEM\\CurrentControlSet\\Services\\Winsock2\\Parameters\\"
          L"Protocol_Catalog9",
          0, KEY_READ, &catalog) != ERROR_SUCCESS) {
    return;
  }

  const auto _ = gsl_lite::finally([&] { RegCloseKey(catalog); });

  for (DWORD index = 0;; ++index) {
    wchar_t name[128]{};
    DWORD name_length = static_cast<DWORD>(std::size(name));
    const LSTATUS status = RegEnumKeyExW(catalog, index, name, &name_length,
                                         nullptr, nullptr, nullptr, nullptr);
    if (status == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (status != ERROR_SUCCESS) {
      continue;
    }

    HKEY entry{};
    if (RegOpenKeyExW(catalog, name, 0, KEY_READ, &entry) != ERROR_SUCCESS) {
      continue;
    }

    DWORD type = 0;
    DWORD size = 0;
    const bool described = RegQueryValueExW(entry, nullptr, nullptr, &type,
                                            nullptr, &size) == ERROR_SUCCESS &&
                           type == REG_BINARY && size >= 2 * sizeof(wchar_t);
    std::vector<BYTE> data(described ? size : 0);
    if (described && RegQueryValueExW(entry, nullptr, nullptr, &type,
                                      data.data(), &size) == ERROR_SUCCESS) {
      const auto *text = reinterpret_cast<const wchar_t *>(data.data());
      const size_t characters = size / sizeof(wchar_t);
      size_t position = 0;
      while (position + 3 < characters) {
        const wchar_t drive = text[position];
        const bool starts_path = ((drive >= L'A' && drive <= L'Z') ||
                                  (drive >= L'a' && drive <= L'z')) &&
                                 text[position + 1] == L':' &&
                                 text[position + 2] == L'\\';
        if (starts_path) {
          size_t end = position;
          while (end < characters && text[end] != L'\0') {
            ++end;
          }
          const std::wstring_view candidate(text + position, end - position);
          if (candidate.size() > 4 &&
              (candidate.ends_with(L".dll") || candidate.ends_with(L".DLL")) &&
              !is_in_windows(std::filesystem::path(candidate))) {
            findings.emplace_back(std::format("the Winsock entry '{}' is not a "
                                              "Windows DLL (a layered service "
                                              "provider is installed)",
                                              to_utf8(candidate)));
            RegCloseKey(entry);
            return;
          }
          position = end;
          continue;
        }
        ++position;
      }
    }
    RegCloseKey(entry);
  }
}

void report() {
  std::vector<std::string> findings{};
  check_game_folder(findings);
  check_loaded_modules(findings);
  check_services(findings);
  check_driver_files(findings);
  check_processes(findings);
  check_winsock_catalog(findings);

  // One line per culprit, no duplicates (a DLL can be found both on disk and
  // loaded).
  std::ranges::sort(findings);
  findings.erase(std::unique(findings.begin(), findings.end()), findings.end());
  if (findings.empty()) {
    return;
  }

  std::string message =
      "^1[ CodHQ BOIII ]^7 Something between the game and the network is "
      "redirecting it:\n";
  for (const std::string &finding : findings) {
    message += "  - " + finding + "\n";
  }
  message +=
      "Remove or stop the item(s) above and start again. CodHQ BOIII will not "
      "patch around them, and it does not touch BlackOps3.exe.\n";

  OutputDebugStringA(message.c_str());
  printf("%s", message.c_str());
}

std::once_flag reported;
} // namespace

struct component final : client_component {
  DEFINE_COMPONENT_NAME("proxy_guard");

  void post_unpack() override {
    if (game::is_server() || game::is_headless()) {
      return;
    }
    std::call_once(reported, report);
  }
};
} // namespace proxy_guard

REGISTER_COMPONENT(proxy_guard::component)
