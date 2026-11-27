#include <std_include.hpp>

#include <loader/component_loader.hpp>

#include "com.hpp"

#include "command.hpp"
#include "error_help.hpp"
#include "scheduler.hpp"

#include <utils/concurrency.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace com {
namespace {
using LoadTasks = std::vector<LoadTask>;
utils::concurrency::container<LoadTasks> com_loadlevelfastfiles_tasks;
utils::concurrency::container<LoadTasks> com_unloadlevelfastfiles_tasks;

utils::hook::detour Com_LoadLevelFastFiles_hook;
void Com_LoadLevelFastFiles_stub(const char *level) {
  // Without the mode's common zone the engine walks into a null asset and
  // dies with an access violation instead of saying what is missing, so stop
  // with the explanation here.
  if (const std::string help =
          error_help::explain_missing_mode_for_level(level);
      !help.empty()) {
    printf("[Com] %s\n", error_help::strip_colors(help).c_str());
    game::com::Com_Error_(__FILE__, __LINE__, game::errorParm::DROP, "\x15%s",
                          help.c_str());
    return;
  }

  Com_LoadLevelFastFiles_hook.invoke(level);
  if (level != nullptr && level[0] != NULL) {
    com_loadlevelfastfiles_tasks.access([level](LoadTasks &tasks) {
      for (const LoadTask &func : tasks) {
        func(level);
      }
    });
  }
}

void execute_unload_callbacks(const char *level) {
  if (level != nullptr && level[0] != NULL) {
    com_unloadlevelfastfiles_tasks.access([level](LoadTasks &tasks) {
      for (const LoadTask &func : tasks) {
        func(level);
      }
    });
  }
}

constexpr uint32_t LEVEL_ZONE_ALLOC_FLAGS_MASK = 0x64;
utils::hook::detour DB_LoadXAssets_hook;
void DB_LoadXAssets_stub(game::db::xzone::XZoneInfo *zoneInfo,
                         uint32_t zoneCount, qboolean sync,
                         qboolean suppressSync) {
  bool is_level_fastfile_unload = false;
  if (zoneInfo != nullptr) {
    bool alloc_flags_include_level_zone =
        /* All zones*/ zoneInfo->allocFlags == 0 ||
        /* Only level zone */ (zoneInfo->allocFlags &
                               LEVEL_ZONE_ALLOC_FLAGS_MASK) ==
            LEVEL_ZONE_ALLOC_FLAGS_MASK;
    /*
      Usuallly, free flags are set to one of:
      - 0x2008A00
      - 0x2008000
      - 0x2000900
      However, this varies widely depending on the timing of the level zone
      unload, and is not required to uniquely identify a level zone unload
      operation, so we do not check free flags here.
    */
    is_level_fastfile_unload = zoneInfo->name == nullptr &&
                               alloc_flags_include_level_zone && zoneCount == 1;
  }
  DB_LoadXAssets_hook.invoke(zoneInfo, zoneCount, sync, suppressSync);
  if (is_level_fastfile_unload) {
    const char *level = *game::g_mapname;
    if (level[0] != NULL) {
      execute_unload_callbacks(level);
    }
  }
}

using command_function = void(__fastcall *)();
command_function engine_map_command = nullptr;
command_function engine_devmap_command = nullptr;

// Refuse to start a level whose game-mode pack is not installed. Without it the
// engine either walks into a null asset while loading the fast files or faults
// in the lobby code before it gets that far; both end the match attempt in a
// crash instead of an explanation.
// The registered 'map'/'devmap' commands are what a typed level change actually
// runs on this engine build (its command executor does not go through the
// detour that was tried first), so these function pointers are the single place
// the check has to sit.
void run_level_command(const command_function original) {
  if (error_help::refuse_missing_mode_for_level(command::params{}.get(1),
                                                "Com")) {
    return;
  }
  if (original != nullptr) {
    original();
  }
}

void map_command_stub() { run_level_command(engine_map_command); }
void devmap_command_stub() { run_level_command(engine_devmap_command); }

// Moves the engine's own 'map' and 'devmap' commands behind the check. They are
// registered during engine init, so this runs a few seconds in.
void guard_level_commands() {
  for (game::cmd::cmd_function_s *function = *game::cmd::cmd_functions;
       function != nullptr; function = function->next) {
    if (function->name == nullptr) {
      continue;
    }

    const std::string name = utils::string::to_lower(function->name);
    const bool is_map = name == "map";
    const bool is_devmap = name == "devmap";
    if (!is_map && !is_devmap) {
      continue;
    }

    const command_function current =
        reinterpret_cast<command_function>(function->function);
    const command_function stub =
        is_map ? map_command_stub : devmap_command_stub;
    if (current == stub) {
      continue;
    }

    command_function &original =
        is_map ? engine_map_command : engine_devmap_command;
    original = current;
    utils::hook::set<game::cmd::xcommand_t>(
        &function->function, reinterpret_cast<game::cmd::xcommand_t>(stub));
  }
}
} // namespace

void on_level_load(const LoadTask &callback) {
  com_loadlevelfastfiles_tasks.access(
      [&callback](LoadTasks &tasks) { tasks.emplace_back(callback); });
}

void on_level_unload(const LoadTask &callback) {
  com_unloadlevelfastfiles_tasks.access(
      [&callback](LoadTasks &tasks) { tasks.emplace_back(callback); });
}

class component final : public generic_component {
  DEFINE_COMPONENT_NAME("com");

public:
  void post_unpack() override {
    /*
      Com_UnloadLevelFastFiles and Com_TryUnloadLevelFastFiles
      are often inlined - hooking these functions will not allow callback
      execution on all instances of fastfile unload.

      Instead, we can detect fastfile unload by hooking DB_LoadXAssets and
      checking if the given `XZoneInfo` will trigger a level fastfile zone
      unload.
    */
    DB_LoadXAssets_hook.create(game::db::load::DB_LoadXAssets.get(),
                               DB_LoadXAssets_stub);
    Com_LoadLevelFastFiles_hook.create(game::com::Com_LoadLevelFastFiles.get(),
                                       Com_LoadLevelFastFiles_stub);

    scheduler::once([] { guard_level_commands(); }, scheduler::pipeline::main,
                    std::chrono::milliseconds(5000));
  }
};
} // namespace com

REGISTER_COMPONENT(com::component)
