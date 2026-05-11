#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "type_id.h"

/// @file npc_weapon_cache.h
/// Startup-built read-only cache mapping ranged weapon types to the
/// magazines and ammo item types that are compatible with them.
///
/// Build once via npc_weapon_cache::build() (called from init.cpp after
/// item_controller->finalize()).  All runtime NPC resupply logic queries
/// this cache instead of walking item metadata on every tick.

class npc;

namespace npc_weapon_cache {

/// Data stored per weapon type.
struct weapon_resupply_info {
    /// All magazine itype IDs that fit this weapon (across every ammo type
    /// the weapon accepts, including ammo-belt variants).
    std::unordered_set<itype_id> compatible_magazines;

    /// All ammo itype IDs that can be fired by this weapon, either
    /// directly (integral magazine / tube magazine) or via a compatible mag.
    std::unordered_set<itype_id> compatible_ammo;

    /// True if the weapon accepts detachable magazines (vs. integral only).
    bool uses_detachable_magazine = false;
};

// ─── Resupply plan ───────────────────────────────────────────────────────────

/// Per-category shortage: which item IDs are wanted and by how many units.
struct resupply_shortage {
    /// Every compatible itype_id the NPC could use to fill this slot.
    std::vector<itype_id> compatible_ids;
    /// How many more units (items for magazines, charges for ammo) are needed.
    int missing = 0;
};

/// Full resupply plan produced for one NPC.
struct resupply_plan {
    /// itype_id of the currently equipped ranged weapon.
    itype_id weapon_id;
    /// Magazine shortage (empty when weapon has only integral magazine).
    std::optional<resupply_shortage> magazines;
    /// Ammo shortage (always present for ranged weapons).
    std::optional<resupply_shortage> ammo;
    /// True when both magazines and ammo are fully stocked.
    bool is_satisfied = false;
};

/// Compute a resupply plan for @p n based on its equipped weapon and
/// current inventory.  Does NOT modify any game state — pure query.
/// Returns nullopt when the NPC is not carrying a ranged weapon or
/// the weapon is not in the cache.
auto compute_resupply_plan( const npc &n ) -> std::optional<resupply_plan>; // *NOPAD*

/// Returns the resupply info for @p weapon_id, or nullptr if the weapon is
/// not a ranged weapon (or the cache hasn't been built yet).
auto get( const itype_id &weapon_id ) -> const weapon_resupply_info *; // *NOPAD*

/// Returns true if @p item_id is a magazine or ammo that is useful for
/// at least one gun the NPC could equip.  Useful for proximity scans.
auto is_resupply_item( const itype_id &item_id ) -> bool;

/// Build the cache.  Must be called exactly once, after
/// item_controller->finalize() has completed.
auto build() -> void;

/// Tear down (called from DynamicDataLoader::unload_data so the cache is
/// rebuilt when a new world is loaded with different mods).
auto reset() -> void;

} // namespace npc_weapon_cache
