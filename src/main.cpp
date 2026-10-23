#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <unordered_set>

#include "Decorator.hpp"

using namespace geode::prelude;
namespace ad = autodecor;

namespace {

uint64_t freshSeed() {
    static std::mt19937_64 gen{std::random_device{}()};
    return (gen() % 2147483646ull) + 1;
}

std::vector<int> parseIntList(std::string const& s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try { v.push_back(std::stoi(tok)); } catch (...) {}
    }
    return v;
}

void notify(std::string const& msg, NotificationIcon icon) {
    Notification::create(msg, icon, 2.5f)->show();
}

ad::Params readParams(uint64_t seed) {
    auto* m = Mod::get();
    ad::Params p;
    p.seed      = seed;
    p.style     = ad::styleFromString(m->getSettingValue<std::string>("style"));
    p.detail    = (int)m->getSettingValue<int64_t>("detail");
    p.thickness = (float)m->getSettingValue<double>("thickness");
    p.fadeSteps = (int)m->getSettingValue<int64_t>("fade-steps");
    p.maxPieces = (size_t)m->getSettingValue<int64_t>("max-pieces");
    p.frames    = m->getSettingValue<bool>("feat-frames");
    p.spires    = m->getSettingValue<bool>("feat-spires");
    p.dots      = m->getSettingValue<bool>("feat-dots");
    p.streaks   = m->getSettingValue<bool>("feat-streaks");
    return p;
}

void setChannel(GameObject* obj, int channel) {
    if (channel <= 0) return;
    if (obj->m_baseColor)   obj->m_baseColor->m_colorID = channel;
    if (obj->m_detailColor) obj->m_detailColor->m_colorID = channel;
}

} // namespace

class $modify(AutoDecorUI, EditorUI) {
    struct Fields {
        std::vector<Ref<GameObject>> lastBatch; // objects of the last generation (for Undo / Reroll)
        std::vector<ad::Cell> lastCells;        // structure the last generation was made from
    };

    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        auto* mod = Mod::get();
        float x = (float)mod->getSettingValue<int64_t>("ui-x");
        float y = (float)mod->getSettingValue<int64_t>("ui-y");

        auto* menu = CCMenu::create();
        menu->setID("menu"_spr);
        menu->setPosition({0.f, 0.f});

        struct Def { char const* label; SEL_MenuHandler cb; char const* id; };
        Def defs[] = {
            {"Decor",  menu_selector(AutoDecorUI::onDecor),  "decor-button"},
            {"Reroll", menu_selector(AutoDecorUI::onReroll), "reroll-button"},
            {"Undo",   menu_selector(AutoDecorUI::onUndoDecor), "undo-button"},
            {"Pal",    menu_selector(AutoDecorUI::onPickPalette), "palette-button"},
        };
        int i = 0;
        for (auto const& d : defs) {
            auto* spr = ButtonSprite::create(d.label);
            spr->setScale(.5f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, d.cb);
            btn->setID(fmt::format("{}/{}", Mod::get()->getID(), d.id)); // mod-prefixed node ID
            btn->setPosition({x, y - 26.f * (float)i++});
            menu->addChild(btn);
        }
        this->addChild(menu, 100);
        return true;
    }

    // ---- structure = cells covered by the selected objects
    std::vector<ad::Cell> collectCells() {
        std::vector<ad::Cell> cells;
        std::unordered_set<uint64_t> seen;
        auto* sel = this->getSelectedObjects();
        if (!sel) return cells;

        auto add = [&](int cx, int cy) {
            uint64_t k = (uint64_t(uint32_t(cx)) << 32) | uint32_t(cy);
            if (cells.size() < 40000 && seen.insert(k).second) cells.push_back({cx, cy});
        };
        for (auto* obj : CCArrayExt<GameObject*>(sel)) {
            if (!obj || obj->m_isTrigger) continue;
            auto pos = obj->getRealPosition();
            int x0 = (int)std::floor(pos.x / ad::kCell), x1 = x0;
            int y0 = (int)std::floor(pos.y / ad::kCell), y1 = y0;
            auto const& r = obj->getObjectRect();
            if (r.size.width > 2.f && r.size.height > 2.f) {
                x0 = std::min(x0, (int)std::floor((r.getMinX() + 1.f) / ad::kCell));
                x1 = std::max(x1, (int)std::floor((r.getMaxX() - 1.f) / ad::kCell));
                y0 = std::min(y0, (int)std::floor((r.getMinY() + 1.f) / ad::kCell));
                y1 = std::max(y1, (int)std::floor((r.getMaxY() - 1.f) / ad::kCell));
            }
            for (int cx = x0; cx <= x1; ++cx)
                for (int cy = y0; cy <= y1; ++cy) add(cx, cy);
        }
        return cells;
    }

    size_t clearBatch() {
        size_t n = 0;
        this->deselectAll();
        for (auto& o : m_fields->lastBatch) {
            if (o && o->getParent()) { m_editorLayer->removeObject(o, true); ++n; }
        }
        m_fields->lastBatch.clear();
        return n;
    }

    void generateFrom(std::vector<ad::Cell> const& cells) {
        auto* mod = Mod::get();
        uint64_t seed = (uint64_t)mod->getSettingValue<int64_t>("seed");
        if (seed == 0) seed = freshSeed();

        auto pieces = ad::generate(cells, readParams(seed));
        if (pieces.empty()) return notify("Nothing generated", NotificationIcon::Warning);

        int  pieceID  = (int)mod->getSettingValue<int64_t>("piece-id");
        bool noTouch  = mod->getSettingValue<bool>("no-touch");
        int  mainCh   = (int)mod->getSettingValue<int64_t>("color-main");
        int  accentCh = (int)mod->getSettingValue<int64_t>("color-accent");
        int  edLayer  = (int)mod->getSettingValue<int64_t>("editor-layer");
        auto fadeCh   = parseIntList(mod->getSettingValue<std::string>("fade-channels"));

        auto* lel = m_editorLayer;
        auto* created = CCArray::create();

        for (auto const& pc : pieces) {
            auto* obj = lel->createObject(pieceID, {pc.x, pc.y}, true); // noUndo = true, we track the batch ourselves
            if (!obj) continue;

            // Never place gameplay hazards, whatever ID the user configured.
            if (obj->m_objectType == GameObjectType::Hazard) {
                lel->removeObject(obj, true);
                this->clearBatch();
                return notify(fmt::format("ID {} is a hazard. Pick a decoration ID (Pal button).", pieceID),
                              NotificationIcon::Error);
            }

            obj->updateCustomScaleX(pc.w / ad::kCell);
            obj->updateCustomScaleY(pc.h / ad::kCell);
            if (pc.rot != 0.f) obj->setRotation(pc.rot);
            if (noTouch) obj->m_isNoTouch = true;
            if (edLayer > 0) obj->m_editorLayer = (short)edLayer;

            int ch = mainCh;
            if (pc.role == ad::Role::Accent) ch = accentCh;
            else if (pc.role == ad::Role::Glow)
                ch = fadeCh.empty() ? accentCh : fadeCh[std::min<size_t>((size_t)pc.fade, fadeCh.size() - 1)];
            setChannel(obj, ch);

            created->addObject(obj);
            m_fields->lastBatch.push_back(obj);
        }
        lel->updateObjectColors(created);

        notify(fmt::format("Decor: {} objects, seed {}", created->count(), seed), NotificationIcon::Success);
    }

    // ---- buttons
    void onDecor(CCObject*) {
        auto cells = this->collectCells();
        if (cells.empty()) return notify("Select the blocks to decorate first", NotificationIcon::Warning);
        m_fields->lastCells = cells;
        this->generateFrom(cells);
    }

    void onReroll(CCObject*) {
        if (m_fields->lastCells.empty())
            return notify("Press Decor first", NotificationIcon::Warning);
        this->clearBatch();
        this->generateFrom(m_fields->lastCells);
    }

    void onUndoDecor(CCObject*) {
        size_t n = this->clearBatch();
        notify(fmt::format("Removed {} decor objects", n), NotificationIcon::Info);
    }

    void onPickPalette(CCObject*) {
        auto* sel = this->getSelectedObjects();
        if (!sel || sel->count() == 0)
            return notify("Select ONE deco object (e.g. an Outline block), then press Pal", NotificationIcon::Warning);
        auto* obj = static_cast<GameObject*>(sel->objectAtIndex(0));
        Mod::get()->setSettingValue<int64_t>("piece-id", obj->m_objectID);
        notify(fmt::format("Piece object ID = {}", obj->m_objectID), NotificationIcon::Success);
    }
};
