#include "npc_weapon_cache.h"

#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "character.h"
#include "debug.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "npc.h"
#include <vector>

namespace npc_weapon_cache {

namespace {

// ─── private storage ─────────────────────────────────────────────────────────

/// Primary cache: gun itype_id → resupply info.
std::unordered_map<itype_id, weapon_resupply_info> s_cache;

/// Reverse index: magazine/ammo itype_id → true if any gun uses it.
/// Lets is_resupply_item() run in O(1).
std::unordered_set<itype_id> s_resupply_items;

bool s_built = false;

// ─── helpers ─────────────────────────────────────────────────────────────────

/// Collect every ammo itype_id that belongs to any of @p ammo_types.
auto collect_ammo_for_types( const std::set<ammotype> &ammo_types )
    -> std::unordered_set<itype_id>
{
    std::unordered_set<itype_id> result;
    for( const auto *itp : item_controller->all() ) {
        if( itp->ammo && ammo_types.contains( itp->ammo->type ) ) {
            result.insert( itp->get_id() );
        }
    }
    return result;
}

} // anonymous namespace

// ─── public API ──────────────────────────────────────────────────────────────

auto get( const itype_id &weapon_id ) -> const weapon_resupply_info *
{
    if( !s_built ) {
        return nullptr;
    }
    const auto it = s_cache.find( weapon_id );
    return it != s_cache.end() ? &it->second : nullptr;
}

auto is_resupply_item( const itype_id &item_id ) -> bool
{
    return s_built && s_resupply_items.contains( item_id );
}

auto build() -> void
{
    if( s_built ) {
        return;
    }

    s_cache.clear();
    s_resupply_items.clear();

    for( const itype *itp : item_controller->all() ) {
        // Only process ranged weapons (items with a gun slot).
        if( !itp->gun ) {
            continue;
        }
        // Skip weapons that accept no ammo (e.g. pure-melee items with a
        // gun slot used only for modes, crossbows flagged as melee, etc.).
        if( itp->gun->ammo.empty() ) {
            continue;
        }

        const itype_id &gun_id = itp->get_id();
        auto &info = s_cache[gun_id];

        // ── Compatible magazines ──────────────────────────────────────────
        // itype::magazines maps each accepted ammotype → set of magazine ids.
        for( const auto &[atype, mag_set] : itp->magazines ) {
            for( const itype_id &mag_id : mag_set ) {
                info.compatible_magazines.insert( mag_id );
                s_resupply_items.insert( mag_id );
            }
        }
        info.uses_detachable_magazine = !info.compatible_magazines.empty();

        // ── Compatible ammo ───────────────────────────────────────────────
        auto ammo_ids = collect_ammo_for_types( itp->gun->ammo );
        for( const itype_id &aid : ammo_ids ) {
            info.compatible_ammo.insert( aid );
            s_resupply_items.insert( aid );
        }
    }

    s_built = true;
    DebugLog( DL::Info, DC::NPC )
        << "[npc_weapon_cache] Built: "
        << s_cache.size() << " ranged weapons, "
        << s_resupply_items.size() << " distinct resupply items.";
}

auto reset() -> void
{
    s_cache.clear();
    s_resupply_items.clear();
    s_built = false;
}

// ─── Resupply plan ───────────────────────────────────────────────────────────

auto compute_resupply_plan( const npc &n ) -> std::optional<resupply_plan>
{
    if( !s_built ) {
        return std::nullopt;
    }

    // ── 1. Identify the equipped ranged weapon ───────────────────────────
    const item &weapon = n.primary_weapon();
    if( !weapon.is_gun() ) {
        return std::nullopt;
    }
    const itype_id weapon_id = weapon.typeId();
    const weapon_resupply_info *info = get( weapon_id );
    if( !info ) {
        return std::nullopt;
    }

    // ── 1a. Calculate dynamic thresholds ─────────────────────────────────
    // For detachable magazines, we scavenge up to 6 mags.
    // For integral magazines (like internal tube), mags count is irrelevant (0).
    const int surplus_mags = info->uses_detachable_magazine ? 6 : 0;
    const int capacity = weapon.ammo_capacity();
    const int surplus_ammo = std::max( 100, capacity * 10 );

    resupply_plan plan;
    plan.weapon_id = weapon_id;

    // ── 2. Calculate remaining weight capacity ───────────────────────────
    units::mass free_weight = n.weight_capacity() - n.weight_carried();
    if( free_weight < units::mass{} ) {
        free_weight = units::mass{};
    }

    // ── 3. Count magazines and apply weight cap ──────────────────────────
    if( info->uses_detachable_magazine ) {
        auto mag_shortage = resupply_shortage{};
        for( const auto &id : info->compatible_magazines ) {
            mag_shortage.compatible_ids.push_back( id );
        }

        int held_mags = 0;
        for( const auto &mag_id : info->compatible_magazines ) {
            held_mags += n.amount_of( mag_id );
        }
        const bool has_loaded_mag = weapon.magazine_current() != nullptr;
        const int effective_mags = held_mags + ( has_loaded_mag ? 1 : 0 );

        // Shortage is calculated against the SURPLUS target
        mag_shortage.missing = std::max( 0, surplus_mags - effective_mags );

        // Cap by weight
        if( mag_shortage.missing > 0 && !info->compatible_magazines.empty() ) {
            const itype_id &mid = *info->compatible_magazines.begin();
            if( !mid.is_empty() ) {
                const itype &itp = mid.obj();
                
                // ── Magazine Weight Heuristic ────────────────────────────
                // Primary: Real item weight
                units::mass m_weight = itp.weight;

                // Fallback / Approximation: (ammo_weight * capacity * 1.1)
                if( m_weight <= units::mass{} ) {
                    units::mass ammo_w = units::mass{};
                    if( !info->compatible_ammo.empty() ) {
                        const itype_id &aid = *info->compatible_ammo.begin();
                        if( !aid.is_empty() ) {
                            ammo_w = aid.obj().weight;
                        }
                    }
                    // Get magazine capacity (falls back to weapon capacity if magazine-specific data missing)
                    int mag_cap = itp.magazine ? itp.magazine->capacity : weapon.ammo_capacity();
                    m_weight = ammo_w * mag_cap * 1.1;
                }

                if( m_weight > units::mass{} ) {
                    int affordable = static_cast<int>( free_weight.value() / m_weight.value() );
                    mag_shortage.missing = std::min( mag_shortage.missing, affordable );
                    // Deduct weight of magazines we intend to pick up
                    free_weight -= m_weight * mag_shortage.missing;
                }
            }
        }
        plan.magazines = std::move( mag_shortage );
    }

    if( free_weight < units::mass{} ) {
        free_weight = units::mass{};
    }

    // ── 4. Count ammo rounds and apply weight cap ────────────────────────
    {
        auto ammo_shortage = resupply_shortage{};
        for( const auto &id : info->compatible_ammo ) {
            ammo_shortage.compatible_ids.push_back( id );
        }

        int held_rounds = 0;
        for( const auto &ammo_id : info->compatible_ammo ) {
            held_rounds += n.charges_of( ammo_id );
        }
        const int loaded_rounds = weapon.ammo_remaining();
        const int effective_rounds = held_rounds + loaded_rounds;

        // Shortage is calculated against the SURPLUS target
        ammo_shortage.missing = std::max( 0, surplus_ammo - effective_rounds );

        // Cap by weight
        if( ammo_shortage.missing > 0 && !info->compatible_ammo.empty() ) {
            const itype_id &aid = *info->compatible_ammo.begin();
            if( !aid.is_empty() ) {
                const itype &itp = aid.obj();
                if( itp.weight > units::mass{} ) {
                    int affordable = static_cast<int>( free_weight.value() / itp.weight.value() );
                    ammo_shortage.missing = std::min( ammo_shortage.missing, affordable );
                }
            }
        }
        plan.ammo = std::move( ammo_shortage );
    }

    // A plan is satisfied if no more items can/should be picked up 
    // given current weight constraints and hoarding targets.
    const bool mags_done = !plan.magazines || plan.magazines->missing == 0;
    const bool ammo_done = !plan.ammo      || plan.ammo->missing == 0;
    plan.is_satisfied    = mags_done && ammo_done;

    return plan;
}

} // namespace npc_weapon_cache
