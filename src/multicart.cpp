#include "multicart.h"

#include <cmath>
#include <string>

#include "console/gfx.h"
#include "game/rally.h"
#include "rc/game.h"
#include "rc32/rally32.h"
#include "version.h"

namespace {
struct Entry {
    const char* name;
    const char* line1;
    const char* line2;
};
const Entry GAMES[3] = {
    {"(3) RALLY", "REAL RALLY: FIVE COUNTRIES, 15 STAGES", "JUMPS, MUD, SNOW, ICE. COCKPIT VIEW."},
    {"(3) RALLY 32", "S3-32 PREVIEW: THE SAME RALLY", "IN REAL 3D ON THE 32-BIT MACHINE."},
    {"S3 RUN", "THE ORIGINAL ARCADE RACER", "BEAT THE CLOCK, PASS THE PACK."},
};
}  // namespace

MultiCart::MultiCart() = default;
MultiCart::~MultiCart() = default;

void MultiCart::init(gs::System& sys) {
    gs::VDP& v = sys.vdp;
    const uint16_t pal0[16] = {0, gs::rgb4(15, 15, 15), gs::rgb4(10, 11, 13), gs::rgb4(15, 13, 0), gs::rgb4(3, 7, 15), gs::rgb4(1, 2, 11),
                               0, 0, 0, 0, 0, 0, 0, 0, 0, gs::rgb4(0, 0, 2)};
    for (int i = 0; i < 16; i++) v.setColor(i, pal0[i]);
    for (int i = 1; i < 16; i++) v.setColor(15 * 16 + i, i == 1 ? gs::rgb4(15, 13, 0) : pal0[i]);
    gs::TileAlloc tiles(v);
    for (int c = 32; c < 128; c++) {
        uint8_t px[64] = {};
        const uint8_t* g = gs::glyph(char(c));
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (g[y * 5 + x] && px[(y + 1) * 8 + x + 2] == 0) px[(y + 1) * 8 + x + 2] = 15;
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (g[y * 5 + x]) px[y * 8 + x + 1] = 1;
        const int t = tiles.alloc(1);
        v.loadTile(t, px);
        fontTile_[c - 32] = t;
    }
    gs::Bitmap logo = gs::textBitmap("S3-16", {4, 1, 5, 0, 1});
    for (int y = 0; y < logo.h; y++)
        for (int x = 0; x < logo.w; x++) {
            uint8_t& p = logo.px[size_t(y) * logo.w + x];
            if (p == 1) p = y < logo.h * 0.4f ? 1 : y < logo.h * 0.7f ? 2 : 4;
        }
    v.A.clear();
    v.B.clear();
    gs::bitmapToPlane(tiles, v.A, (40 - logo.w / 8) / 2, 2, logo, 0);
    v.A.scroll(0, 0);
    v.B.scroll(0, 0);
    for (int y = 0; y < gs::SCREEN_H; y++) v.lineBackdrop[y] = gs::rgb4(0, 0, std::min(8, 2 + y / 32));
    const std::string last = sys.loadBlob("cart.txt");
    sel_ = last == "run" ? 2 : last == "rally32" ? 1 : 0;
    t_ = 0;
}

void MultiCart::draw(gs::System& sys) {
    gs::VDP& v = sys.vdp;
    v.HUD.clear();
    auto put = [&](int col, int row, const std::string& s, int pal) {
        for (size_t i = 0; i < s.size(); i++) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c <= 32 || c >= 128) continue;
            v.HUD.set(col + int(i), row, gs::entry(fontTile_[c - 32], pal));
        }
    };
    auto centre = [&](int row, const std::string& s, int pal) { put(20 - int(s.size()) / 2, row, s, pal); };
    centre(8, "MULTI-CART  -  CHOOSE A GAME", 0);
    for (int i = 0; i < 3; i++) {
        const int row = 10 + i * 5;
        const bool on = i == sel_;
        centre(row, (on ? "> " : "  ") + std::string(GAMES[i].name) + (on ? " <" : "  "), on ? 15 : 0);
        centre(row + 2, GAMES[i].line1, 0);
        centre(row + 3, GAMES[i].line2, 0);
    }
    if (t_ % 60 < 40) centre(24, "PRESS START", 15);
    centre(26, "ESC ON A TITLE SCREEN COMES BACK HERE", 0);
    put(39 - int(std::string(S3_VERSION_STRING).size()), 27, S3_VERSION_STRING, 0);
}

void MultiCart::frame(gs::System& sys) {
    t_++;
    gs::Pad& pad = sys.pad;
    if (pad.pressed(gs::BTN_UP)) sel_ = (sel_ + 2) % 3;
    if (pad.pressed(gs::BTN_DOWN)) sel_ = (sel_ + 1) % 3;
    draw(sys);
    if (t_ > 10 && (pad.pressed(gs::BTN_START) || pad.pressed(gs::BTN_C))) {
        sys.saveBlob("cart.txt", sel_ == 0 ? "rally" : sel_ == 1 ? "rally32" : "run");
        gs::Cart* c;
        if (sel_ == 0) {
            if (!champ_) champ_ = std::make_unique<rc::RallyChamp>();
            c = champ_.get();
        } else if (sel_ == 1) {
            if (!r32_) r32_ = std::make_unique<rc32::Rally32>();
            c = r32_.get();
        } else {
            if (!run_) run_ = std::make_unique<rally::Rally>();
            c = run_.get();
        }
        sys.bootCart(*c);
    }
}
