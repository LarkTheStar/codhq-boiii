#include <std_include.hpp>

#include <loader/component_loader.hpp>

// In case of clangd compilation
#if __has_include("version.hpp")
#include "version.hpp" // IWYU pragma: keep
#else
#ifndef VERSION
#define VERSION "0"
#endif
#ifndef SHORTVERSION
#define SHORTVERSION "0"
#endif
#endif

#include "scheduler.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>

namespace branding {
namespace {
void draw_branding() {
  if (game::com::Com_IsInGame()) {
    return;
  }

  constexpr float X = 4.0f;
  constexpr float Y = 0.0f;
  constexpr float NAME_SCALE = 0.45f;
  constexpr float TAG_SCALE = 0.3f;

  static const uint32_t *font = reinterpret_cast<const uint32_t *>(
      game::select(0x1432D2918, 0x1410E1BE0));

  const float font_height = static_cast<float>(font[2]);
  const float name_baseline = Y + font_height * NAME_SCALE;

  // The launcher's accent colour, so the in-game mark and the launcher read as
  // the same client. The tag underneath says which build is running.
  game::vec4_t name_color = {.r = 0.976f, .g = 0.451f, .b = 0.086f, .a = 1.0f};
  game::vec4_t tag_color = {.r = 0.666f, .g = 0.666f, .b = 0.666f, .a = 0.9f};

  game::render::R_AddCmdDrawText(
      "CodHQ BOIII", std::numeric_limits<int>::max(), font, X, name_baseline,
      NAME_SCALE, NAME_SCALE, 0.0f, &name_color, game::itemTextStyle::NORMAL);

  game::render::R_AddCmdDrawText(
      "v" SHORTVERSION, std::numeric_limits<int>::max(), font, X,
      name_baseline + font_height * TAG_SCALE * 1.4f, TAG_SCALE, TAG_SCALE,
      0.0f, &tag_color, game::itemTextStyle::NORMAL);
}

const char *get_ingame_console_prefix_stub() { return "CodHQ> "; }
} // namespace

struct component final : client_component {
  DEFINE_COMPONENT_NAME("branding");

  void post_unpack() override {
    if (!utils::flags::has_flag("nobranding")) {

      scheduler::loop(draw_branding, scheduler::pipeline::renderer);

      // Change window title prefix
      utils::hook::copy_string(game::select(0x142FC02D8, 0x0), "CodHQ");

      // Change ingame console prefix
      utils::hook::call(game::con::Con_DrawInput.offset(0x90),
                        get_ingame_console_prefix_stub);
    }
  }
};
} // namespace branding

REGISTER_COMPONENT(branding::component)
