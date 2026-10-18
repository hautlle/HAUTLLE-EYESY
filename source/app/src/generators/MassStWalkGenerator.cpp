#include "MassStWalkGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
constexpr float GLIDE_SPEED = 1.34112f;   // 3 mph, fixed and independent of knobs/audio

constexpr float EYE_HEIGHT = 1.7f;
constexpr float NEAR_PLANE = 0.25f;
constexpr float FOV_Y = 1.05f;            // ~60 degrees vertical
constexpr float CAM_PITCH = 0.16f;        // look slightly up (radians); lowers the horizon
constexpr float GROUP_INTERVAL = 1.25f;  // leave space between architectural gestures
constexpr float WAVE_INTERVAL = 22.0f;  // travelling accents, only on a subsequent hit
constexpr float WAVE_SPEED = 140.0f;      // roof pulse travels down the street (m/s)
constexpr float GROUND_FLOOR = 4.6f;
constexpr float UPPER_FLOOR = 3.9f;
// Pull each side toward the walk without squeezing building/facade proportions.
// 0 restores v2 spacing; 2.5 narrows the curb-to-curb gap from 22.8 to 17.8 m.
constexpr float STREET_INSET = 2.5f;
} // namespace

// ---------------------------------------------------------------- Lua bridge

void MassStWalkGenerator::registerFunctions(lua_State* L) {
    struct Entry { const char* name; lua_CFunction fn; };
    const Entry entries[] = {
        {"masst_load", &MassStWalkGenerator::trampolineLoad},
        {"masst_reset", &MassStWalkGenerator::trampolineReset},
        {"masst_step", &MassStWalkGenerator::trampolineStep},
        {"masst_draw", &MassStWalkGenerator::trampolineDraw},
        {"masst_is_ready", &MassStWalkGenerator::trampolineIsReady},
        {"masst_stats", &MassStWalkGenerator::trampolineStats},
    };
    for (const Entry& e : entries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, e.fn, 1);
        lua_setglobal(L, e.name);
    }
}

#define MASST_SELF \
    MassStWalkGenerator* self = static_cast<MassStWalkGenerator*>( \
        lua_touserdata(L, lua_upvalueindex(1)))

int MassStWalkGenerator::trampolineLoad(lua_State* L) {
    MASST_SELF;
    const char* path = luaL_checkstring(L, 1);
    const bool ok = self && self->load(path ? path : "");
    lua_pushboolean(L, ok);
    lua_pushinteger(L, self ? static_cast<long>(self->buildings.size()) : 0);
    return 2;
}

int MassStWalkGenerator::trampolineReset(lua_State* L) {
    MASST_SELF;
    if (self) self->reset();
    return 0;
}

int MassStWalkGenerator::trampolineStep(lua_State* L) {
    MASST_SELF;
    if (!self) return 0;
    self->step(static_cast<float>(luaL_optnumber(L, 1, 1.0 / 60.0)),
               static_cast<float>(luaL_optnumber(L, 2, 0.0)),
               static_cast<float>(luaL_optnumber(L, 3, 0.0)),
               static_cast<float>(luaL_optnumber(L, 4, 0.0)),
               static_cast<float>(luaL_optnumber(L, 5, 0.0)),
               lua_toboolean(L, 6) != 0,
               static_cast<float>(luaL_optnumber(L, 7, 0.4)),
               static_cast<float>(luaL_optnumber(L, 8, 0.5)),
               static_cast<float>(luaL_optnumber(L, 9, 0.5)),
               static_cast<float>(luaL_optnumber(L, 10, 0.33)),
               static_cast<float>(luaL_optnumber(L, 11, 0.1)));
    return 0;
}

int MassStWalkGenerator::trampolineDraw(lua_State* L) {
    MASST_SELF;
    if (!self) return 0;
    self->draw(static_cast<float>(luaL_optnumber(L, 1, 1280.0)),
               static_cast<float>(luaL_optnumber(L, 2, 720.0)),
               lua_toboolean(L, 3) != 0,
               lua_toboolean(L, 4) != 0,
               static_cast<int>(luaL_optinteger(L, 5, 12)));
    return 0;
}

int MassStWalkGenerator::trampolineIsReady(lua_State* L) {
    MASST_SELF;
    lua_pushboolean(L, self && self->loaded);
    return 1;
}

// Returns: segmentsDrawn, simAvgMs, simMaxMs, drawAvgMs, drawMaxMs (max resets).
int MassStWalkGenerator::trampolineStats(lua_State* L) {
    MASST_SELF;
    if (!self) return 0;
    lua_pushinteger(L, self->lastSegmentsDrawn);
    lua_pushnumber(L, self->simMsAvg);
    lua_pushnumber(L, self->simMsMax);
    lua_pushnumber(L, self->drawMsAvg);
    lua_pushnumber(L, self->drawMsMax);
    self->simMsMax = 0.0f;
    self->drawMsMax = 0.0f;
    return 5;
}

#undef MASST_SELF

// ---------------------------------------------------------------- helpers

float MassStWalkGenerator::clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float MassStWalkGenerator::lerp(float a, float b, float t) { return a + (b - a) * t; }
float MassStWalkGenerator::response(float dt, float rate) {
    return 1.0f - std::exp(-std::max(0.0f, dt) * std::max(0.0f, rate));
}
uint32_t MassStWalkGenerator::hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float MassStWalkGenerator::hash01(uint32_t x) { return (hash32(x) & 0x00ffffffu) / 16777215.0f; }

ofFloatColor MassStWalkGenerator::hsv(float h, float s, float v, float a) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    s = clamp01(s);
    v = std::max(0.0f, v);
    // A soft shoulder preserves contour contrast on loud hits/additive overlaps.
    if (v > 0.85f) v = 0.85f + 0.15f * (1.0f - std::exp(-(v - 0.85f) / 0.15f));
    const float hh = h * 6.0f;
    const int sector = static_cast<int>(std::floor(hh)) % 6;
    const float f = hh - std::floor(hh);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch (sector) {
        case 0: return ofFloatColor(v, t, p, a);
        case 1: return ofFloatColor(q, v, p, a);
        case 2: return ofFloatColor(p, v, t, a);
        case 3: return ofFloatColor(p, q, v, a);
        case 4: return ofFloatColor(t, p, v, a);
        default: return ofFloatColor(v, p, q, a);
    }
}

// ---------------------------------------------------------------- data loading

void MassStWalkGenerator::addSegment(int building, Kind kind, const P3& a, const P3& b,
                                     int window, uint32_t seed) {
    Segment s;
    s.a = a;
    s.b = b;
    s.building = building;
    s.window = window;
    s.kind = kind;
    s.seed = seed ? seed : hash32(static_cast<uint32_t>(segments.size() + 1) ^ 0x4d415353u);
    segments.push_back(s);
}

MassStWalkGenerator::Style MassStWalkGenerator::classifyStyle(uint32_t osmId, float uMid,
                                                               float vMid) const {
    // The OSM footprint for the Eldridge is named and stable. The other signatures are
    // selected by block position so the mode survives harmless OSM ID churn.
    if (osmId == 218716377u) return ELDRIDGE_701;
    if (uMid > 10.0f && uMid < 27.0f && vMid < -18.0f && vMid > -40.0f) return CLASSICAL_700;
    if (uMid > 27.0f && uMid < 35.0f && vMid < -18.0f && vMid > -40.0f) return ROMANESQUE_704;
    if (uMid > 214.0f && uMid < 228.0f && vMid > 0.0f) return WOODWARD_801;
    if (uMid > 232.0f && uMid < 243.0f && vMid < 0.0f) return SUNFLOWER_804;
    if (uMid > 108.0f && uMid < 129.0f && vMid > 0.0f) return HOUSE_729;
    if (uMid > 424.0f && uMid < 450.0f && vMid > 0.0f) return WEAVERS_901;
    return DEFAULT_STYLE;
}

void MassStWalkGenerator::addArcOnFacade(int building, Kind kind, const P3& a, const P3& b,
                                         float sCenter, float width, float springZ, float rise,
                                         int steps, uint32_t seed) {
    const float du = b.u - a.u;
    const float dv = b.v - a.v;
    const float len = std::sqrt(du * du + dv * dv);
    if (len < 0.01f) return;
    const float eu = du / len;
    const float ev = dv / len;
    const float r = width * 0.5f;
    P3 prev;
    bool have = false;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float ang = 3.14159265358979323846f * (1.0f - t);
        const float ss = sCenter + std::cos(ang) * r;
        const float zz = springZ + std::sin(ang) * rise;
        P3 cur{a.u + eu * ss, a.v + ev * ss, zz};
        if (have) addSegment(building, kind, prev, cur, -1, seed + static_cast<uint32_t>(i));
        prev = cur;
        have = true;
    }
}

void MassStWalkGenerator::addRect(int building, int window, Kind kind, float u0, float v0,
                                  float u1, float v1, float z0, float z1) {
    P3 a{u0, v0, z0}, b{u1, v1, z0}, c{u1, v1, z1}, d{u0, v0, z1};
    const P3 corners[5] = {a, b, c, d, a};
    const uint32_t base = hash32(static_cast<uint32_t>(window + 1) ^
                                 static_cast<uint32_t>((building + 17) * 0x9e3779b9u));
    for (int i = 0; i < 4; ++i)
        addSegment(building, kind, corners[i], corners[i + 1], window, base + i);
}

// Historically-informed facade grammar. It is intentionally not a photogrammetric
// reconstruction: the footprint remains OSM, but the street face gets the rhythms that
// make downtown Lawrence read as downtown Lawrence -- tall storefront glass, narrow upper
// windows, cornices, parapets, awnings, and occasional ornamental arches/brackets.
void MassStWalkGenerator::addFacade(int bi, const P3& a, const P3& b, int levels, bool storefront) {
    const float du = b.u - a.u;
    const float dv = b.v - a.v;
    const float len = std::sqrt(du * du + dv * dv);
    if (len < 2.5f) return;
    const float eu = du / len;
    const float ev = dv / len;
    const float h = buildings[bi].height;
    const uint32_t seed = buildings[bi].seed;
    const int variant = static_cast<int>(hash32(seed ^ 0xFACEu) % 4u);

    auto at = [&](float ss, float zz = 0.0f) {
        return P3{a.u + eu * ss, a.v + ev * ss, zz};
    };
    auto rectAlong = [&](float s0, float s1, float z0, float z1, Kind kind) {
        const P3 p0 = at(s0, z0);
        const P3 p1 = at(s1, z1);
        addRect(bi, windowCount++, kind, p0.u, p0.v, p1.u, p1.v, z0, z1);
    };

    // Strong horizontal datum at the cornice, plus a second line on many historic facades.
    addSegment(bi, ROOF, at(0.0f, h - 0.55f), at(len, h - 0.55f), -1, seed ^ 0xC011u);
    if (variant == 0 || variant == 3 || buildings[bi].style != DEFAULT_STYLE)
        addSegment(bi, DETAIL, at(0.0f, h - 0.95f), at(len, h - 0.95f), -1, seed ^ 0xC012u);

    if (storefront) {
        // Sign/transom band. Mass Street's ground floor reads as glass + transom + sign area,
        // not simply a blank extruded wall.
        addSegment(bi, DETAIL, at(0.0f, 3.85f), at(len, 3.85f), -1, seed ^ 0x5100u);
        if (variant == 1 || variant == 3)
            addSegment(bi, DETAIL, at(0.0f, 3.35f), at(len, 3.35f), -1, seed ^ 0x5101u);

        const int bays = std::max(1, static_cast<int>(std::round(len / (4.1f + 0.5f * variant))));
        const float bayW = len / bays;
        const int doorBay = static_cast<int>(hash32(seed ^ 0xD00Du) % static_cast<uint32_t>(bays));
        for (int i = 0; i < bays; ++i) {
            float s0 = i * bayW + 0.28f;
            const float s1 = (i + 1) * bayW - 0.28f;
            if (i == doorBay && bayW > 2.4f) {
                const float doorW = std::min(1.25f, bayW * 0.34f);
                rectAlong(s0 + 0.12f, s0 + 0.12f + doorW, 0.05f, 2.65f, STORE);
                s0 += 0.12f + doorW + 0.24f;
            }
            if (s1 - s0 > 0.75f) rectAlong(s0, s1, 0.62f, 3.28f, STORE);
        }

        // Projecting cloth/metal awnings on a deterministic subset. The front edge is pulled
        // toward the road so the facade stops feeling like a texture pasted to a box.
        if (buildings[bi].style != SUNFLOWER_804 && buildings[bi].style != WEAVERS_901 &&
            buildings[bi].style != ELDRIDGE_701 && hash01(seed ^ 0xA771u) < 0.48f && len > 4.5f) {
            const float towardStreet = (buildings[bi].vMid > 0.0f) ? -0.72f : 0.72f;
            const P3 l0 = at(0.35f, 3.18f);
            const P3 r0 = at(len - 0.35f, 3.18f);
            P3 l1{l0.u, l0.v + towardStreet, 2.78f};
            P3 r1{r0.u, r0.v + towardStreet, 2.78f};
            addSegment(bi, DETAIL, l0, l1, -1, seed ^ 0xA772u);
            addSegment(bi, DETAIL, r0, r1, -1, seed ^ 0xA773u);
            addSegment(bi, DETAIL, l1, r1, -1, seed ^ 0xA774u);
        }
    } else {
        const int n0 = std::max(1, static_cast<int>(len / 4.0f));
        for (int i = 0; i < n0; ++i) {
            const float c = (i + 0.5f) * len / n0;
            rectAlong(c - 0.58f, c + 0.58f, 0.95f, 2.58f, WINDOW);
        }
    }

    // Upper-floor windows. Variants alter rhythm and proportion without inventing heavy detail.
    // Weavers is a deliberate exception: its broad upper facade reads as panel/sign mass,
    // so rows of generic windows would make it less recognizable, not more.
    if (buildings[bi].style != WEAVERS_901) {
        const float nominal = (variant == 2 ? 3.15f : (variant == 1 ? 2.35f : 2.65f));
        const int n = std::max(1, static_cast<int>((len - 0.7f) / nominal));
        const float spacing = len / n;
        const float winW = std::min(1.12f, spacing * (variant == 2 ? 0.48f : 0.58f));
        for (int k = 1; k < levels; ++k) {
            const float floorH = (h - GROUND_FLOOR) / std::max(1, levels - 1);
            const float zb = GROUND_FLOOR + (k - 1) * floorH;
            const float z0 = zb + 0.78f;
            const float z1 = std::min(zb + floorH - 0.62f, h - 0.78f);
            if (z1 - z0 < 0.60f) continue;
            for (int i = 0; i < n; ++i) {
                const float c = (i + 0.5f) * spacing;
                rectAlong(c - winW * 0.5f, c + winW * 0.5f, z0, z1, WINDOW);
                if (variant == 1 && k == levels - 1 && spacing > 1.8f)
                    addArcOnFacade(bi, DETAIL, a, b, c, winW, z1 - 0.02f, 0.33f, 4,
                                   seed ^ static_cast<uint32_t>(0xA000 + i + k * 31));
            }
        }
    }

    addFacadeCharacter(bi, a, b, storefront, levels);
}

void MassStWalkGenerator::addFacadeCharacter(int bi, const P3& a, const P3& b,
                                              bool storefront, int levels) {
    const Building& bd = buildings[bi];
    const float du = b.u - a.u;
    const float dv = b.v - a.v;
    const float len = std::sqrt(du * du + dv * dv);
    if (len < 2.5f) return;
    const float eu = du / len, ev = dv / len;
    const uint32_t seed = bd.seed;
    auto at = [&](float ss, float zz) { return P3{a.u + eu * ss, a.v + ev * ss, zz}; };

    // Generic parapet/bracket vocabulary for historic commercial blocks.
    if (bd.style == DEFAULT_STYLE) {
        const float r = hash01(seed ^ 0xBADAu);
        if (r < 0.32f && len > 5.0f) {
            const float s0 = len * 0.28f, s1 = len * 0.72f;
            const float top = bd.height + 0.48f;
            addSegment(bi, ROOF, at(0.0f, bd.height), at(s0, bd.height), -1, seed ^ 0xB001u);
            addSegment(bi, ROOF, at(s0, bd.height), at(s0, top), -1, seed ^ 0xB002u);
            addSegment(bi, ROOF, at(s0, top), at(s1, top), -1, seed ^ 0xB003u);
            addSegment(bi, ROOF, at(s1, top), at(s1, bd.height), -1, seed ^ 0xB004u);
            addSegment(bi, ROOF, at(s1, bd.height), at(len, bd.height), -1, seed ^ 0xB005u);
        } else if (r < 0.62f) {
            const int brackets = std::max(2, static_cast<int>(len / 1.6f));
            for (int i = 0; i <= brackets; ++i) {
                const float ss = len * i / brackets;
                addSegment(bi, DETAIL, at(ss, bd.height - 0.45f), at(ss, bd.height - 0.78f),
                           -1, seed ^ static_cast<uint32_t>(0xB100 + i));
            }
        }
        return;
    }

    if (bd.style == ELDRIDGE_701) {
        // Five-story mass, rusticated/arched first level, strong belt courses and parapet panels.
        addSegment(bi, DETAIL, at(0, 4.45f), at(len, 4.45f), -1, seed ^ 0xE001u);
        addSegment(bi, DETAIL, at(0, bd.height - 3.85f), at(len, bd.height - 3.85f), -1, seed ^ 0xE002u);
        const int arches = std::max(3, static_cast<int>(std::round(len / 4.4f)));
        const float bay = len / arches;
        for (int i = 0; i < arches; ++i) {
            const float c = (i + 0.5f) * bay;
            const float w = std::min(2.9f, bay * 0.66f);
            addSegment(bi, DETAIL, at(c - w * 0.5f, 0.15f), at(c - w * 0.5f, 2.65f), -1,
                       seed ^ static_cast<uint32_t>(0xE100 + i * 3));
            addSegment(bi, DETAIL, at(c + w * 0.5f, 0.15f), at(c + w * 0.5f, 2.65f), -1,
                       seed ^ static_cast<uint32_t>(0xE101 + i * 3));
            addArcOnFacade(bi, DETAIL, a, b, c, w, 2.65f, 1.15f, 6,
                           seed ^ static_cast<uint32_t>(0xE200 + i * 8));
        }
        const int panels = std::max(3, static_cast<int>(len / 2.5f));
        for (int i = 0; i < panels; ++i) {
            const float s0 = i * len / panels + 0.18f;
            const float s1 = (i + 1) * len / panels - 0.18f;
            if (s1 > s0) addSegment(bi, DETAIL, at(s0, bd.height - 0.72f), at(s1, bd.height - 0.72f),
                                    -1, seed ^ static_cast<uint32_t>(0xE300 + i));
        }
        if (storefront && len > 7.0f) {
            // Projecting hotel blade-sign silhouette: no literal text, just the landmark gesture.
            const float ss = len * 0.78f;
            const float towardStreet = (bd.vMid > 0.0f) ? -1.15f : 1.15f;
            P3 a0 = at(ss, 6.1f), a1 = at(ss, 11.4f);
            P3 f0{a0.u, a0.v + towardStreet, 6.1f};
            P3 f1{a1.u, a1.v + towardStreet, 11.4f};
            addSegment(bi, DETAIL, a0, f0, -1, seed ^ 0xE410u);
            addSegment(bi, DETAIL, a1, f1, -1, seed ^ 0xE411u);
            addSegment(bi, DETAIL, f0, f1, -1, seed ^ 0xE412u);
        }
    } else if (bd.style == CLASSICAL_700) {
        if (!storefront) return;
        // 700 Mass: central portico/pediment rather than another anonymous storefront grid.
        const float c = len * 0.5f;
        const float span = std::min(4.8f, len * 0.42f);
        const float l = c - span * 0.5f, r = c + span * 0.5f;
        addSegment(bi, DETAIL, at(l, 0.1f), at(l, 3.55f), -1, seed ^ 0x7001u);
        addSegment(bi, DETAIL, at(r, 0.1f), at(r, 3.55f), -1, seed ^ 0x7002u);
        addSegment(bi, DETAIL, at(l - 0.45f, 3.55f), at(r + 0.45f, 3.55f), -1, seed ^ 0x7003u);
        addSegment(bi, DETAIL, at(l - 0.45f, 3.55f), at(c, 4.25f), -1, seed ^ 0x7004u);
        addSegment(bi, DETAIL, at(c, 4.25f), at(r + 0.45f, 3.55f), -1, seed ^ 0x7005u);
    } else if (bd.style == ROMANESQUE_704) {
        if (!storefront) return;
        // 704 Mass: rusticated Romanesque facade with a large arched storefront and
        // three strongly arched upper openings. These curves break the OSM-box language immediately.
        const float c = len * 0.56f;
        const float groundW = std::min(len * 0.72f, 5.6f);
        addArcOnFacade(bi, DETAIL, a, b, c, groundW, 2.45f, 1.25f, 8, seed ^ 0x7040u);
        const int upper = 3;
        for (int i = 0; i < upper; ++i) {
            const float cc = (i + 0.5f) * len / upper;
            const float ww = std::min(1.45f, len / upper * 0.56f);
            addArcOnFacade(bi, DETAIL, a, b, cc, ww, bd.height - 2.05f, 0.62f, 6,
                           seed ^ static_cast<uint32_t>(0x7050 + i * 8));
        }
        addSegment(bi, DETAIL, at(0.0f, 4.28f), at(len, 4.28f), -1, seed ^ 0x7060u);
    } else if (bd.style == WOODWARD_801) {
        // Tile skirt/cantilever line and dense bracket rhythm at the rounded corner block.
        addSegment(bi, ROOF, at(0, bd.height - 0.35f), at(len, bd.height - 0.35f), -1, seed ^ 0x8010u);
        const int n = std::max(3, static_cast<int>(len / 1.2f));
        for (int i = 0; i <= n; ++i) {
            const float ss = len * i / n;
            addSegment(bi, DETAIL, at(ss, bd.height - 0.36f), at(ss, bd.height - 0.86f), -1,
                       seed ^ static_cast<uint32_t>(0x8011 + i));
        }
        if (storefront && len > 5.5f) {
            // The OSM footprint is square here, but the real second-story corner is rounded
            // and the first-floor entrance is angled. Overlay those two gestures without
            // replacing the footprint topology.
            const bool aNorth = a.u < b.u;
            const P3 corner = aNorth ? a : b;
            const float dirU = aNorth ? 1.0f : -1.0f;
            const float intoSide = (bd.vMid > 0.0f) ? 1.0f : -1.0f;
            const float r = 1.85f;
            for (int layer = 0; layer < 2; ++layer) {
                const float zz = layer == 0 ? 4.62f : bd.height - 0.18f;
                P3 prev{corner.u + dirU * r, corner.v, zz};
                for (int j = 1; j <= 7; ++j) {
                    const float t = j / 7.0f;
                    const float ang = t * 1.57079632679f;
                    P3 cur{corner.u + dirU * r * std::cos(ang),
                           corner.v + intoSide * r * std::sin(ang), zz};
                    addSegment(bi, DETAIL, prev, cur, -1,
                               seed ^ static_cast<uint32_t>(0x8100 + layer * 16 + j));
                    prev = cur;
                }
            }
            P3 f0{corner.u + dirU * 1.05f, corner.v, 0.08f};
            P3 f1{corner.u, corner.v + intoSide * 1.05f, 0.08f};
            P3 t0{f0.u, f0.v, 3.25f}, t1{f1.u, f1.v, 3.25f};
            addSegment(bi, DETAIL, f0, f1, -1, seed ^ 0x8120u);
            addSegment(bi, DETAIL, t0, t1, -1, seed ^ 0x8121u);
            addSegment(bi, DETAIL, f0, t0, -1, seed ^ 0x8122u);
            addSegment(bi, DETAIL, f1, t1, -1, seed ^ 0x8123u);
        }
    } else if (bd.style == SUNFLOWER_804) {
        // Ridenour & Baker / Barteldes: third-story Palladian center and fire-escape gesture.
        const float c = len * 0.5f;
        const float z0 = bd.height - 3.35f;
        const float z1 = bd.height - 1.10f;
        addSegment(bi, DETAIL, at(c - 1.12f, z0), at(c - 1.12f, z1), -1, seed ^ 0x8041u);
        addSegment(bi, DETAIL, at(c + 1.12f, z0), at(c + 1.12f, z1), -1, seed ^ 0x8042u);
        addArcOnFacade(bi, DETAIL, a, b, c, 2.24f, z1, 0.92f, 7, seed ^ 0x8043u);
        // Sparse diagonal fire escape / ladder; unmistakable without becoming literal CAD.
        if (len > 5.5f) {
            float x0 = len * 0.72f, x1 = len * 0.88f;
            addSegment(bi, DETAIL, at(x0, 4.0f), at(x1, bd.height - 0.8f), -1, seed ^ 0x8044u);
            addSegment(bi, DETAIL, at(x0 + 0.55f, 4.0f), at(x1 + 0.55f, bd.height - 0.8f), -1,
                       seed ^ 0x8045u);
            for (int i = 0; i < 5; ++i) {
                const float t = (i + 1) / 6.0f;
                const float s0 = lerp(x0, x1, t);
                const float zz = lerp(4.0f, bd.height - 0.8f, t);
                addSegment(bi, DETAIL, at(s0, zz), at(s0 + 0.55f, zz), -1,
                           seed ^ static_cast<uint32_t>(0x8050 + i));
            }
        }
        const int corniceBrackets = std::max(4, static_cast<int>(len / 1.25f));
        for (int i = 0; i <= corniceBrackets; ++i) {
            const float ss = len * i / corniceBrackets;
            addSegment(bi, DETAIL, at(ss, bd.height - 0.38f), at(ss, bd.height - 0.82f), -1,
                       seed ^ static_cast<uint32_t>(0x8058 + i));
        }
        if (storefront && len > 4.8f) {
            // Current 804/Sunflower identity: a striped projecting awning below the historic upper facade.
            const float towardStreet = (bd.vMid > 0.0f) ? -0.82f : 0.82f;
            const float left = 0.35f, right = len - 0.35f;
            P3 al = at(left, 3.18f), ar = at(right, 3.18f);
            P3 fl{al.u, al.v + towardStreet, 2.74f};
            P3 fr{ar.u, ar.v + towardStreet, 2.74f};
            addSegment(bi, DETAIL, al, fl, -1, seed ^ 0x8060u);
            addSegment(bi, DETAIL, ar, fr, -1, seed ^ 0x8061u);
            addSegment(bi, DETAIL, fl, fr, -1, seed ^ 0x8062u);
            for (int i = 1; i < 7; ++i) {
                const float t = i / 7.0f;
                P3 top = at(lerp(left, right, t), 3.18f);
                P3 front{top.u, top.v + towardStreet, 2.74f};
                addSegment(bi, DETAIL, top, front, -1, seed ^ static_cast<uint32_t>(0x8070 + i));
            }
        }
    } else if (bd.style == HOUSE_729) {
        if (!storefront) return;
        // House Building zone: stepped/castellated roofline + plaque-like panel.
        const float top = bd.height + 0.62f;
        const float s0 = len * 0.30f, s1 = len * 0.70f;
        addSegment(bi, ROOF, at(0, bd.height), at(s0, bd.height), -1, seed ^ 0x7290u);
        addSegment(bi, ROOF, at(s0, bd.height), at(s0, top), -1, seed ^ 0x7291u);
        addSegment(bi, ROOF, at(s0, top), at(s1, top), -1, seed ^ 0x7292u);
        addSegment(bi, ROOF, at(s1, top), at(s1, bd.height), -1, seed ^ 0x7293u);
        addSegment(bi, ROOF, at(s1, bd.height), at(len, bd.height), -1, seed ^ 0x7294u);
        if (len > 4.0f) {
            addSegment(bi, DETAIL, at(len * 0.37f, bd.height - 1.25f),
                       at(len * 0.63f, bd.height - 1.25f), -1, seed ^ 0x7295u);
        }
    } else if (bd.style == WEAVERS_901) {
        if (!storefront) return;
        // Weavers' present-day facade is deliberately blunt: broad upper panels and a long canopy.
        // That blank mass is more recognizable than fake rows of Victorian windows here.
        const float towardStreet = (bd.vMid > 0.0f) ? -0.90f : 0.90f;
        const float panelTop = bd.height - 0.55f;
        const float panelBot = std::min(panelTop - 1.0f, 4.45f);
        addSegment(bi, DETAIL, at(0.2f, panelBot), at(len - 0.2f, panelBot), -1, seed ^ 0x9010u);
        addSegment(bi, DETAIL, at(0.2f, panelTop), at(len - 0.2f, panelTop), -1, seed ^ 0x9011u);
        const int strips = std::max(3, static_cast<int>(len / 4.0f));
        for (int i = 0; i <= strips; ++i) {
            const float ss = lerp(0.3f, len - 0.3f, static_cast<float>(i) / strips);
            addSegment(bi, DETAIL, at(ss, panelBot), at(ss, panelTop), -1,
                       seed ^ static_cast<uint32_t>(0x9020 + i));
        }
        P3 l = at(0.25f, 3.25f), r = at(len - 0.25f, 3.25f);
        P3 lf{l.u, l.v + towardStreet, 3.02f}, rf{r.u, r.v + towardStreet, 3.02f};
        addSegment(bi, DETAIL, l, lf, -1, seed ^ 0x9030u);
        addSegment(bi, DETAIL, r, rf, -1, seed ^ 0x9031u);
        addSegment(bi, DETAIL, lf, rf, -1, seed ^ 0x9032u);
    }
}

void MassStWalkGenerator::buildStreetDetails() {
    streetSegments.clear();
    auto addStreet = [&](const P3& a, const P3& b, uint32_t seed, Kind kind = STREET) {
        Segment s;
        s.a = a; s.b = b; s.building = -1; s.window = -1; s.kind = kind; s.seed = seed;
        streetSegments.push_back(s);
    };

    // Sidewalk/curb rails: these establish a real street section and make the facades feel
    // attached to a place instead of floating on the sides of a vector tunnel.
    const float rails[] = {-14.2f, -11.4f, 11.4f, 14.2f};
    for (int i = 0; i < 4; ++i)
        addStreet(P3{0.0f, rails[i], 0.02f}, P3{streetLength, rails[i], 0.02f},
                  0x510000u + static_cast<uint32_t>(i));

    // Diagonal parking stall marks. Mass Street's diagonal parking is a strong downtown cue.
    for (float u = 10.0f; u < streetLength - 7.0f; u += 7.0f) {
        bool tooClose = false;
        for (float c : crossings) if (std::fabs(u - c) < 10.0f) { tooClose = true; break; }
        if (tooClose) continue;
        addStreet(P3{u - 1.8f, -10.9f, 0.025f}, P3{u + 2.4f, -5.2f, 0.025f},
                  hash32(static_cast<uint32_t>(u * 10) ^ 0xA11Eu));
        addStreet(P3{u - 1.8f, 10.9f, 0.025f}, P3{u + 2.4f, 5.2f, 0.025f},
                  hash32(static_cast<uint32_t>(u * 10) ^ 0xB22Fu));
    }

    // Intersections and simple zebra crosswalk bars.
    for (size_t ci = 0; ci < crossings.size(); ++ci) {
        const float c = crossings[ci];
        addStreet(P3{c, -45.0f, 0.03f}, P3{c, 45.0f, 0.03f},
                  0xC000u + static_cast<uint32_t>(ci));
        for (int j = -3; j <= 3; ++j) {
            const float uu = c + j * 0.72f;
            addStreet(P3{uu, -10.8f, 0.035f}, P3{uu, -3.6f, 0.035f},
                      0xC100u + static_cast<uint32_t>(ci * 20 + j + 4));
            addStreet(P3{uu, 3.6f, 0.035f}, P3{uu, 10.8f, 0.035f},
                      0xC200u + static_cast<uint32_t>(ci * 20 + j + 4));
        }
    }

    // Sparse downtown light standards, offset from storefronts so they read in perspective.
    int lampIndex = 0;
    for (float u = 25.0f; u < streetLength - 12.0f; u += 34.0f) {
        for (int side : {-1, 1}) {
            const float v = side * 12.65f;
            const uint32_t sd = hash32(0x1A4Du ^ static_cast<uint32_t>(lampIndex++));
            addStreet(P3{u, v, 0.0f}, P3{u, v, 4.25f}, sd);
            addStreet(P3{u - 0.55f, v, 4.25f}, P3{u + 0.55f, v, 4.25f}, sd ^ 1u, DETAIL);
            addStreet(P3{u - 0.20f, v, 4.25f}, P3{u, v, 4.58f}, sd ^ 2u, DETAIL);
            addStreet(P3{u, v, 4.58f}, P3{u + 0.20f, v, 4.25f}, sd ^ 3u, DETAIL);
        }
    }
}

bool MassStWalkGenerator::load(const std::string& path) {
    if (loaded && path == loadedPath) return true;

    std::ifstream in(path);
    if (!in) {
        ofLogNotice("BRIDGE") << "masst_load(): cannot open " << path;
        return false;
    }

    struct RawBuilding {
        uint32_t id = 0;
        float height = 9.0f;
        int levels = 2;
        bool estimated = true;
        std::vector<P3> ring;
        std::vector<int> flags;
    };
    std::vector<RawBuilding> raw;
    std::vector<float> fileCrossings;
    float length = 0.0f;
    std::string line;
    bool header = false;

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "MASST") {
            header = true;
        } else if (tag == "length") {
            ss >> length;
        } else if (tag == "cross") {
            float c = 0.0f;
            while (ss >> c) fileCrossings.push_back(c);
        } else if (tag == "building") {
            RawBuilding b;
            long long id = 0;
            int count = 0, est = 0;
            ss >> id >> b.height >> b.levels >> count >> est;
            b.estimated = est != 0;
            b.id = static_cast<uint32_t>(id & 0xffffffff);
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ps(line);
                P3 p;
                int flag = 0;
                ps >> p.u >> p.v >> flag;
                b.ring.push_back(p);
                b.flags.push_back(flag);
            }
            if (b.ring.size() >= 3) raw.push_back(b);
        } else if (tag == "end") {
            break;
        }
    }

    if (!header || raw.empty() || length < 10.0f) {
        ofLogNotice("BRIDGE") << "masst_load(): bad or empty dataset " << path;
        return false;
    }

    // Back-of-lot buildings (sheds by the alley) don't get storefronts:
    // find each side's frontage line and drop facade flags well behind it.
    std::vector<float> frontL, frontR;
    std::vector<float> minAbsV(raw.size());
    std::vector<int> side(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        float m = 1.0e9f, sumV = 0.0f;
        for (const P3& p : raw[i].ring) {
            m = std::min(m, std::fabs(p.v));
            sumV += p.v;
        }
        minAbsV[i] = m;
        side[i] = sumV >= 0.0f ? 1 : 0;
        (side[i] ? frontR : frontL).push_back(m);
    }
    auto percentile = [](std::vector<float> v, float q) {
        if (v.empty()) return 15.0f;
        std::sort(v.begin(), v.end());
        return v[static_cast<size_t>(q * (v.size() - 1))];
    };
    const float frontLineL = percentile(frontL, 0.3f);
    const float frontLineR = percentile(frontR, 0.3f);

    buildings.clear();
    segments.clear();
    streetSegments.clear();
    crossings = fileCrossings;
    windowCount = 0;
    streetLength = length;

    int backCount = 0;
    int estimatedCount = 0;
    int signatureCount = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const RawBuilding& rb = raw[i];
        const bool back = minAbsV[i] > (side[i] ? frontLineR : frontLineL) + 6.0f;
        if (back) ++backCount;
        if (rb.estimated) ++estimatedCount;

        Building b;
        b.osmId = rb.id;
        b.backLot = back;
        b.frontAbsV = std::max(0.0f, minAbsV[i] - STREET_INSET);
        b.height = std::max(3.5f, rb.height);
        b.levels = std::max(1, rb.levels);
        b.seed = hash32(rb.id);
        b.uMin = 1.0e9f;
        b.uMax = -1.0e9f;
        b.vMid = 0.0f;
        for (const P3& p : rb.ring) {
            b.uMin = std::min(b.uMin, p.u);
            b.uMax = std::max(b.uMax, p.u);
            b.vMid += p.v;
        }
        b.vMid /= std::max<size_t>(1, rb.ring.size());
        b.uMid = (b.uMin + b.uMax) * 0.5f;
        b.style = classifyStyle(rb.id, b.uMid, b.vMid);
        if (b.style != DEFAULT_STYLE) ++signatureCount;

        // OSM only has actual height/storey information for a tiny fraction of this extract.
        // Correct the landmarks whose silhouette is essential to recognizing the walk.
        switch (b.style) {
            case ELDRIDGE_701:
                b.levels = 5;
                b.height = 20.4f;
                break;
            case CLASSICAL_700:
                b.levels = 3;
                b.height = 12.6f;
                break;
            case ROMANESQUE_704:
                b.levels = 2;
                b.height = 8.8f;
                break;
            case WOODWARD_801:
                b.levels = 2;
                b.height = 8.8f;
                break;
            case SUNFLOWER_804:
                b.levels = 3;
                b.height = 12.9f;
                break;
            case HOUSE_729:
                b.levels = 2;
                b.height = 8.9f;
                break;
            case WEAVERS_901:
                b.levels = 3;
                b.height = 12.2f;
                break;
            default:
                break;
        }

        b.segFirst = static_cast<int>(segments.size());
        const int bi = static_cast<int>(buildings.size());
        buildings.push_back(b);

        const size_t n = rb.ring.size();
        for (size_t k = 0; k < n; ++k) {
            const P3& p0 = rb.ring[k];
            const P3& q0 = rb.ring[(k + 1) % n];
            const uint32_t sd = hash32(b.seed ^ static_cast<uint32_t>(k * 0x9e3779b9u));
            addSegment(bi, STRUCT, P3{p0.u, p0.v, 0.0f}, P3{q0.u, q0.v, 0.0f}, -1, sd);
            addSegment(bi, ROOF, P3{p0.u, p0.v, buildings[bi].height},
                       P3{q0.u, q0.v, buildings[bi].height}, -1, sd ^ 0x1111u);
            addSegment(bi, STRUCT, P3{p0.u, p0.v, 0.0f},
                       P3{p0.u, p0.v, buildings[bi].height}, -1, sd ^ 0x2222u);
        }
        if (!back) {
            for (size_t k = 0; k < n; ++k) {
                if (rb.flags[k] == 0) continue;
                addFacade(bi, rb.ring[k], rb.ring[(k + 1) % n], buildings[bi].levels,
                          rb.flags[k] == 1);
            }
        }
        buildings[bi].segCount = static_cast<int>(segments.size()) - buildings[bi].segFirst;
    }

    buildStreetDetails();

    // Apply after classification and facade generation so landmark selection and
    // all architectural dimensions retain their original OSM-derived geometry.
    for (Building& b : buildings) {
        const float shift = b.vMid >= 0.0f ? -STREET_INSET : STREET_INSET;
        for (int s = b.segFirst; s < b.segFirst + b.segCount; ++s) {
            segments[s].a.v += shift;
            segments[s].b.v += shift;
        }
        b.vMid += shift;
    }
    // Street endpoints are all outside the inset: translate both sides equally,
    // including cross-street lines that span the centre of the corridor.
    for (Segment& s : streetSegments) {
        s.a.v += s.a.v >= 0.0f ? -STREET_INSET : STREET_INSET;
        s.b.v += s.b.v >= 0.0f ? -STREET_INSET : STREET_INSET;
    }

    loaded = true;
    loadedPath = path;
    reset();
    ofLogNotice("BRIDGE") << "masst_load() MASST WALK v4: " << buildings.size() << " buildings, "
                          << segments.size() << " building segments + " << streetSegments.size()
                          << " street segments, " << windowCount << " windows, "
                          << estimatedCount << " OSM-estimated heights, " << signatureCount
                          << " signature masses, length " << streetLength << " m";
    return true;
}

// ---------------------------------------------------------------- simulation

void MassStWalkGenerator::reset() {
    camU = 0.0f;
    elapsed = 0.0f;
    lowEnv = midEnv = highEnv = energyEnv = 0.0f;
    slowLow = midSlow = 0.0f;
    previousTrig = false;
    hitCooldown = 0.0f;
    hitArmed = true;
    lowPeak = 0.0f;
    prevLowEnv = 0.0f;
    for (PulseEvent& hit : hits) {
        hit.time = -100.0f;
        hit.amplitude = hit.origin = 0.0f;
        hit.wave = false;
        hit.selected.clear();
    }
    groupRotation = 0;
    groupsSinceWave = 0;
    lastWaveTime = 0.0f;
    frameCounter = 0;
    rng = 0x4D415353u;
    hitNext = 0;
    litDrift = 0.0f;
    tearLife = 0.0f;
}

void MassStWalkGenerator::step(float dt, float low, float mid, float high, float energy,
                               bool trigFired, float knob1, float knob2, float knob3,
                               float knob4, float knob5) {
    if (!loaded) return;
    const auto t0 = std::chrono::steady_clock::now();
    dt = std::max(0.0f, std::min(0.05f, dt));
    elapsed += dt;
    ++frameCounter;

    // Smooth the two scene macros so knob changes do not snap the atmosphere.
    k1 += (clamp01(knob1) - k1) * response(dt, 5.0f);
    k4 += (clamp01(knob4) - k4) * response(dt, 5.0f);
    k2 = clamp01(knob2); k3 = clamp01(knob3); k5 = clamp01(knob5);

    low = clamp01(low); mid = clamp01(mid); high = clamp01(high); energy = clamp01(energy);
    lowEnv += (low - lowEnv) * response(dt, low > lowEnv ? 14.0f : 4.0f);
    midEnv += (mid - midEnv) * response(dt, mid > midEnv ? 8.0f : 3.0f);
    highEnv += (high - highEnv) * response(dt, high > highEnv ? 18.0f : 6.0f);
    energyEnv += (energy - energyEnv) * response(dt, energy > energyEnv ? 9.0f : 2.4f);
    slowLow += (lowEnv - slowLow) * response(dt, 1.4f);
    midSlow += (midEnv - midSlow) * response(dt, 0.7f);

    // Level centreline glide: fixed speed, no gait, sway or changing orientation.
    camU += GLIDE_SPEED * dt;
    if (camU >= streetLength) {
        camU -= streetLength;
        for (PulseEvent& hit : hits) hit.origin -= streetLength;
    }

    // Kick / TRIG requests an architectural event; the scheduler leaves breathing room.
    hitCooldown = std::max(0.0f, hitCooldown - dt);
    const bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;
    // Re-arm only after the bass falls back, so one kick = one pulse.
    const float novelty = lowEnv - slowLow;
    lowPeak = std::max(lowPeak, lowEnv);
    if (novelty < 0.035f || lowEnv < lowPeak * 0.65f) hitArmed = true;
    const bool rising = lowEnv > prevLowEnv + 0.004f;    // only on the attack
    prevLowEnv = lowEnv;
    const bool kick = hitArmed && rising && novelty > 0.07f + 0.04f * energyEnv;
    if ((kick || trigEdge) && hitCooldown <= 0.0f && k2 > 0.01f) {
        hitArmed = false;
        lowPeak = lowEnv;
        const float strength = trigEdge ? 0.8f : clamp01(novelty * 5.0f);
        launchPulse(strength);
    }

    // Lit-window pattern drifts slowly; mids change how many are lit.
    litDrift += dt * (0.004f + 0.02f * midSlow);

    // Glitch tears (K5), more likely on kicks.
    tearLife -= dt;
    if (tearLife <= 0.0f && k5 > 0.01f) {
        const float chance = k5 * (0.02f + 0.10f * lowEnv);
        rng = hash32(rng + frameCounter);
        if ((rng & 0xffff) / 65535.0f < chance) {
            const float y0 = hash01(rng ^ 0x11u);
            tearY0 = y0;
            tearY1 = y0 + 0.02f + 0.10f * hash01(rng ^ 0x22u);
            tearShift = (hash01(rng ^ 0x33u) - 0.5f) * 2.0f * (20.0f + 120.0f * k5);
            tearLife = 0.05f + 0.15f * hash01(rng ^ 0x44u);
        }
    }

    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    simMsAvg += (ms - simMsAvg) * 0.05f;
    simMsMax = std::max(simMsMax, ms);
}

// Building selections are interleaved along the active block, on both sides.
// A given building stays in its group until the pool changes as we glide past.
void MassStWalkGenerator::launchPulse(float strength) {
    PulseEvent& hit = hits[hitNext];
    hit.selected.clear();
    hit.wave = groupsSinceWave >= 8 && elapsed - lastWaveTime >= WAVE_INTERVAL;
    if (!hit.wave) {
        struct Candidate { int index; float distance; };
        std::vector<Candidate> candidates;
        const float probe = std::fmod(camU + 8.0f, streetLength);
        auto blockFor = [&](float u) {
            return static_cast<int>(std::upper_bound(crossings.begin(), crossings.end(), u)
                                    - crossings.begin()) - 1;
        };
        const int activeBlock = blockFor(probe);
        const float range = std::min(150.0f, lerp(100.0f, 280.0f, k1) * 0.85f);
        for (size_t i = 0; i < buildings.size(); ++i) {
            const Building& b = buildings[i];
            if (b.backLot || blockFor(b.uMid) != activeBlock) continue;
            float d = b.uMid - camU;
            if (d < 0.0f) d += streetLength;
            if (d < 8.0f || d > range) continue;
            candidates.push_back(Candidate{static_cast<int>(i), d});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.distance == b.distance ? a.index < b.index : a.distance < b.distance;
        });
        if (candidates.empty()) {
            // At an intersection, wait for the next hit instead of pulsing invisible lots.
            hitCooldown = 0.35f;
            return;
        }
        const int groupCount = std::min(4, static_cast<int>(candidates.size()));
        const int group = static_cast<int>(groupRotation % groupCount);
        for (size_t i = group; i < candidates.size(); i += groupCount)
            hit.selected.push_back(candidates[i].index);
        ++groupRotation;
        ++groupsSinceWave;
    } else {
        groupsSinceWave = 0;
        lastWaveTime = elapsed;
    }
    hit.time = elapsed;
    hit.origin = camU;
    hit.amplitude = 0.25f + 0.6f * strength;
    hitCooldown = hit.wave ? 3.5f : GROUP_INTERVAL;
    hitNext = (hitNext + 1) % MAX_HITS;
}

float MassStWalkGenerator::buildingPulse(int building, float tileU) const {
    const Building& b = buildings[building];
    if (b.backLot) return 0.0f;
    float pulse = 0.0f;
    for (const PulseEvent& hit : hits) {
        float delay = 0.0f;
        if (hit.wave) {
            const float lo = b.uMin + tileU, hi = b.uMax + tileU;
            // Waves move forwards; never reflect into the block behind their origin.
            if (hi < hit.origin) continue;
            delay = std::max(0.0f, lo - hit.origin) / WAVE_SPEED;
        } else {
            if (std::find(hit.selected.begin(), hit.selected.end(), building) == hit.selected.end())
                continue;
            // The same dataset is tiled: pulse only the copy ahead of the event origin.
            const float d = b.uMid + tileU - hit.origin;
            if (d < 0.0f || d > 160.0f) continue;
        }
        const float x = elapsed - hit.time - delay;
        if (x <= 0.0f || x > 1.5f) continue;
        pulse += hit.amplitude * (1.0f - std::exp(-x * 30.0f)) * std::exp(-x * 4.5f);
    }
    // Groups share one envelope and start together; no per-building arrival delay.
    return std::min(0.32f, pulse * 0.55f) * k2;
}

// ---------------------------------------------------------------- drawing

void MassStWalkGenerator::draw(float width, float height, bool persistOn, bool persistClear,
                               int persistFade) {
    if (!loaded) return;
    const auto t0 = std::chrono::steady_clock::now();
    const float w = std::max(1.0f, width);
    const float h = std::max(1.0f, height);
    const float focal = (h * 0.5f) / std::tan(FOV_Y * 0.5f);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f + focal * std::tan(CAM_PITCH);

    const float eyeZ = EYE_HEIGHT;
    const float camV = 0.0f;
    const float cyaw = 1.0f, syaw = 0.0f;

    // K1 Depth: nearby, misted silhouettes -> long, clearly exposed facades.
    // The lens and viewpoint stay fixed throughout this macro.
    const float viewDist = lerp(100.0f, 280.0f, k1);
    const float windowDist = lerp(55.0f, 160.0f, k1);
    const float fogPower = lerp(1.9f, 0.9f, k1);
    const float detailGain = lerp(0.35f, 1.0f, k1);
    // K4 Signal Age: fresh green carrier -> warmer, faded, lingering signal.
    const float hue = lerp(0.34f, 0.10f, k4);
    const float ageSaturation = lerp(1.0f, 0.65f, k4);
    const float gain = 0.82f + 0.38f * energyEnv;
    const float litFrac = clamp01(0.10f + 0.55f * k3 * (0.35f + 0.65f * midSlow));
    const float flashChance = highEnv * k3 * 0.30f;
    const float jitterPx = k5 * (0.25f + 2.1f * highEnv);
    const uint32_t flashFrame = frameCounter / 3;
    const uint32_t errorFrame = frameCounter / 5;

    auto& verts = lineMesh.getVertices();
    auto& cols = lineMesh.getColors();
    lineMesh.setMode(OF_PRIMITIVE_LINES);
    verts.clear();
    cols.clear();
    int drawn = 0;

    struct Cam { float depth, side, z; };
    auto toCam = [&](const P3& p, float tileU, float zScale, float hgt, float sideSlip) {
        float z = p.z;
        if (zScale != 0.0f && hgt > 0.001f) z = p.z * (1.0f + zScale * (p.z / hgt));
        const float du = p.u + tileU - camU;
        const float dv = p.v + sideSlip - camV;
        return Cam{du * cyaw + dv * syaw, -du * syaw + dv * cyaw, z - eyeZ};
    };

    auto emit = [&](Cam a, Cam b, float bright, Kind kind, uint32_t seed) {
        // Sparse line loss: more like an unstable field/signal than universal wiggle.
        // Structural street rails are retained so the scene remains navigable at high K5.
        if (kind != STREET && k5 > 0.10f) {
            const float drop = k5 * (0.010f + 0.055f * highEnv);
            if (hash01(seed ^ (errorFrame * 0x6D2B79F5u)) < drop) return;
        }

        if (a.depth < NEAR_PLANE && b.depth < NEAR_PLANE) return;
        if (a.depth < NEAR_PLANE || b.depth < NEAR_PLANE) {
            const float t = (NEAR_PLANE - a.depth) / (b.depth - a.depth);
            const Cam c{NEAR_PLANE, a.side + (b.side - a.side) * t, a.z + (b.z - a.z) * t};
            if (a.depth < NEAR_PLANE) a = c; else b = c;
        }
        float ax = cx + focal * a.side / a.depth;
        float ay = cy - focal * a.z / a.depth;
        float bx = cx + focal * b.side / b.depth;
        float by = cy - focal * b.z / b.depth;
        if ((ax < 0 && bx < 0) || (ax > w && bx > w) || (ay < 0 && by < 0) || (ay > h && by > h)) return;

        // Subpixel facade marks become horizon fuzz. Fade them before they collapse.
        if (kind == WINDOW || kind == STORE || kind == DETAIL) {
            const float span = std::max(std::fabs(bx - ax), std::fabs(by - ay));
            const float legibility = clamp01((span - 1.2f) / 3.8f);
            if (legibility <= 0.0f || bright < 0.008f) return;
            bright *= legibility;
        }

        if (jitterPx > 0.05f) {
            const uint32_t js = hash32(seed ^ frameCounter);
            ax += (hash01(js) - 0.5f) * jitterPx;
            ay += (hash01(js ^ 0x9e37u) - 0.5f) * jitterPx;
            bx += (hash01(js ^ 0x7f4au) - 0.5f) * jitterPx;
            by += (hash01(js ^ 0x1234u) - 0.5f) * jitterPx;
        }
        if (tearLife > 0.0f) {
            const float t0y = tearY0 * h, t1y = tearY1 * h;
            if (ay >= t0y && ay <= t1y) ax += tearShift;
            if (by >= t0y && by <= t1y) bx += tearShift;
        }

        const float fa = std::pow(clamp01(1.0f - a.depth / viewDist), fogPower);
        const float fb = std::pow(clamp01(1.0f - b.depth / viewDist), fogPower);
        float localHue = hue;
        float sat = 0.85f;
        if (kind == STREET) { localHue = hue - 0.025f; sat = 0.48f; }
        else if (kind == DETAIL) { localHue = hue + 0.018f; sat = 0.72f; }
        else if (kind == STORE) { localHue = hue + 0.008f; sat = 0.78f; }

        sat *= ageSaturation;
        if (kind == DETAIL || kind == WINDOW || kind == STORE) bright *= detailGain;
        verts.emplace_back(ax, ay, 0.0f);
        cols.push_back(hsv(localHue, sat, bright * fa * gain));
        verts.emplace_back(bx, by, 0.0f);
        cols.push_back(hsv(localHue, sat, bright * fb * gain));
        ++drawn;

        // HAUTLLE signal echo: a small, sparse chromatic registration error appears on a
        // deterministic subset of architectural detail, increased by Signal Age.
        const float echoGate = k5 * (0.16f + 0.42f * highEnv) + 0.16f * k4;
        const bool echoDetail = kind == DETAIL || kind == STORE || kind == WINDOW;
        if ((echoDetail || (kind != STREET && k5 > 0.65f))
            && hash01(seed ^ 0xA19A3u) < echoGate) {
            const float sign = hash01(seed ^ 0xEC40u) < 0.5f ? -1.0f : 1.0f;
            const float dx = sign * (0.55f + 3.5f * k4 + 2.8f * k5 * (0.25f + highEnv));
            const float ghost = bright * (0.08f + 0.10f * k4 + 0.20f * k5) * (0.55f + 0.45f * highEnv);
            verts.emplace_back(ax + dx, ay, 0.0f);
            cols.push_back(hsv(localHue + 0.46f, 0.62f, ghost * fa * gain));
            verts.emplace_back(bx + dx, by, 0.0f);
            cols.push_back(hsv(localHue + 0.46f, 0.62f, ghost * fb * gain));
            ++drawn;
        }
    };

    // Two copies of the street: this one and the next, so 10th St flows
    // straight into 7th St again with no visible seam.
    for (int tile = 0; tile < 2; ++tile) {
        const float tileU = tile * streetLength;

        // Street furniture first: curb/sidewalk rails, diagonal parking marks,
        // intersections/crosswalks and sparse light standards anchor this as Mass St.
        for (const Segment& seg : streetSegments) {
            const float mn = std::min(seg.a.u, seg.b.u) + tileU - camU;
            const float mx = std::max(seg.a.u, seg.b.u) + tileU - camU;
            if (mx < -3.0f || mn > viewDist) continue;
            const float bright = seg.kind == STREET ? 0.38f : 0.60f;
            emit(toCam(seg.a, tileU, 0.0f, 1.0f, 0.0f),
                 toCam(seg.b, tileU, 0.0f, 1.0f, 0.0f), bright, seg.kind, seg.seed);
        }

        for (const Building& b : buildings) {
            const float bMin = b.uMin + tileU - camU;
            const float bMax = b.uMax + tileU - camU;
            if (bMax < -3.0f || bMin > viewDist) continue;

            const int bi = static_cast<int>(&b - buildings.data());
            const float pulse = buildingPulse(bi, tileU);
            const float detailFade = clamp01((windowDist - bMin) / 24.0f);
            const bool nearForWindows = detailFade > 0.0f;

            // Whole-building phase slip. It lasts only for a few frames and affects a small
            // subset of buildings, so architecture briefly loses registration with itself.
            float sideSlip = 0.0f;
            if (k5 > 0.08f) {
                const uint32_t sg = hash32(b.seed ^ (errorFrame * 0x9E3779B9u));
                const float gate = k5 * (0.010f + 0.060f * highEnv);
                if (hash01(sg) < gate)
                    sideSlip = (hash01(sg ^ 0x41C64E6Du) - 0.5f) * 1.8f * k5;
            }

            for (int s = b.segFirst; s < b.segFirst + b.segCount; ++s) {
                const Segment& seg = segments[s];
                float bright;
                if (seg.kind == WINDOW || seg.kind == STORE) {
                    if (!nearForWindows) continue;
                    const float hw = hash01(static_cast<uint32_t>(seg.window) * 2654435761u);
                    const float phase = hw + litDrift - std::floor(hw + litDrift);
                    const bool lit = phase < litFrac;
                    bright = lit ? 0.95f : 0.22f;
                    if (seg.kind == STORE) bright = lit ? 1.0f : 0.35f;
                    if (hash01(static_cast<uint32_t>(seg.window) ^ (flashFrame * 0x9E3779B9u)) < flashChance)
                        bright += 0.35f;
                    bright += pulse;
                } else if (seg.kind == ROOF) {
                    bright = 0.80f + pulse * 1.4f;
                } else if (seg.kind == DETAIL) {
                    bright = 0.63f + pulse * 0.6f;
                } else {
                    bright = 0.58f;
                }
                // Keep the transparent wireframe, but let the street-facing contour lead.
                const float intoLot = std::max(0.0f,
                    std::fabs((seg.a.v + seg.b.v) * 0.5f) - b.frontAbsV);
                bright *= lerp(1.0f, 0.18f, clamp01(intoLot / 14.0f));
                if (b.backLot) bright *= 0.35f;
                if (seg.kind == WINDOW || seg.kind == STORE || seg.kind == DETAIL)
                    bright *= detailFade;
                emit(toCam(seg.a, tileU, pulse, b.height, sideSlip),
                     toCam(seg.b, tileU, pulse, b.height, sideSlip), bright, seg.kind, seg.seed);
            }
        }
    }
    lastSegmentsDrawn = drawn;

    ofPushStyle();
    ofDisableDepthTest();
    ofEnableAlphaBlending();
    const int bgAlpha = (persistOn && !persistClear) ? std::max(2, std::min(255, persistFade)) : 255;
    // Signal Age lengthens trails only when the hardware PERSIST switch is on.
    const int agedAlpha = std::max(2, std::min(255,
        static_cast<int>(bgAlpha * lerp(1.7f, 0.3f, k4) + 0.5f)));
    ofSetColor(0, 0, 0, (persistOn && !persistClear) ? agedAlpha : 255);
    ofFill();
    ofDrawRectangle(0, 0, w, h);
    // Additive lines: overlaps glow like a vector monitor, while the sparse registration
    // echoes can bloom into a second carrier under PERSIST.
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofSetColor(255);
    if (!verts.empty()) lineMesh.draw();
    ofPopStyle();

    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    drawMsAvg += (ms - drawMsAvg) * 0.05f;
    drawMsMax = std::max(drawMsMax, ms);
}
