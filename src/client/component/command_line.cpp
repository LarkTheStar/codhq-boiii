#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "scheduler.hpp"

#include <game/game.hpp>

#include <utils/string.hpp>

namespace command_line {
namespace {
std::vector<std::string> get_startup_set_commands() {
  const std::string command_line = GetCommandLineA();

  std::vector<std::string> commands{};
  bool in_quotes = false;
  size_t start = std::string::npos;

  for (size_t i = 0; i < command_line.size(); ++i) {
    const char character = command_line[i];

    if (character == '"') {
      in_quotes = !in_quotes;
      continue;
    }

    if (in_quotes || character != '+') {
      continue;
    }

    if (start != std::string::npos) {
      commands.emplace_back(command_line.substr(start, i - start));
    }

    start = i + 1;
  }

  if (start != std::string::npos) {
    commands.emplace_back(command_line.substr(start));
  }

  return commands;
}

std::string get_startup_dvar_buffer() {
  std::string buffer{};

  for (const auto &command_line : get_startup_set_commands()) {
    const auto separator = command_line.find_first_of(" \t");
    if (separator == std::string::npos) {
      continue;
    }

    const auto command =
        utils::string::to_lower(command_line.substr(0, separator));
    if (command != "set" && command != "seta") {
      continue;
    }

    buffer.append("set ");
    buffer.append(command_line.substr(separator + 1));
    buffer.append("\n");
  }

  return buffer;
}

// The engine only applies +set for a whitelist of dvars, and it does that
// before the config file is read, so a +set on the command line lost against
// config.cfg. Re-apply them after the config, which is what the command line
// intends.
void apply_startup_dvars() {
  const auto buffer = get_startup_dvar_buffer();
  if (buffer.empty()) {
    return;
  }

  game::cbuf::Cbuf_ExecuteBuffer(game::LOCAL_CLIENT_0,
                                 game::ControllerIndex_t::CONTROLLER_INDEX_0,
                                 buffer.c_str());
}
} // namespace

class component final : public generic_component {
  DEFINE_COMPONENT_NAME("command_line");

public:
  void post_unpack() override {
    // The client reads its config before dvars_loaded; the dedicated server has
    // no config file, so it gets the same pass on the main pipeline.
    scheduler::once(apply_startup_dvars,
                    game::is_server() ? scheduler::pipeline::main
                                      : scheduler::pipeline::dvars_loaded);
  }
};
} // namespace command_line

REGISTER_COMPONENT(command_line::component)
