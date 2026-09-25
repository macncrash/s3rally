// The S3-16 multi-cart: after the boot logo, pick which game to play.
// ESC on a game's title screen comes back here (the console's reset button).
#pragma once
#include <memory>

#include "console/system.h"

namespace rally { class Rally; }
namespace rc { class RallyChamp; }
namespace rc32 { class Rally32; }

class MultiCart : public gs::Cart {
public:
    MultiCart();
    ~MultiCart() override;
    const char* title() const override { return "S3-16 MULTI-CART"; }
    void init(gs::System& sys) override;
    void frame(gs::System& sys) override;

private:
    void draw(gs::System& sys);
    int sel_ = 0;
    int t_ = 0;
    int fontTile_[96] = {};
    std::unique_ptr<rc::RallyChamp> champ_;
    std::unique_ptr<rally::Rally> run_;
    std::unique_ptr<rc32::Rally32> r32_;
};
