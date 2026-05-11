#include "ai_actions.h"
#include "npc.h"
#include "map.h"
#include "map_iterator.h"
#include "game.h"
#include "item.h"
#include "line.h"
#include "messages.h"
#include "translations.h"
#include "debug.h"
#include "npc_weapon_cache.h"

#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <fstream>
#include "item_category.h"

namespace ai_actions {

void ai_log( const std::string &message )
{
    std::ofstream log_file( "ai_debug.log", std::ios_base::app );
    if( log_file.is_open() ) {
        log_file << message << std::endl;
    }
}

auto execute_command( npc &n, const std::string &action, const std::string &target ) -> std::string
{
    if( action == "pick_up" ) {
        static const std::unordered_set<std::string> known_categories = {
            "guns", "magazines", "ammo", "weapons", "tools", "tools_entry", "tools_workshop",
            "tools_cooking", "tools_chemistry", "tools_farming", "deployables", "electronics",
            "clothing", "food", "cooking_ingredients", "drugs", "books", "spellbooks", "mods",
            "mutagen", "bionics", "veh_parts", "other", "fuel", "seeds", "chems", "battery",
            "spare_parts", "scrap_metal", "scrap_electronics", "scrap_fabric", "scrap_wood",
            "scrap_plastic", "scrap_ceramics", "scrap_glass", "valuables", "rocks", "soil",
            "container", "artifacts", "maps"
        };

        if( known_categories.count( target ) > 0 ) {
            ai_log( string_format( "[CATEGORY_MATCH] Matched category '%s'", target ) );
            
            map &m = get_map();
            const tripoint &pos = n.pos();
            std::vector<std::string> items_to_pick;
            int total_items_checked = 0;

            for( const tripoint &p : m.points_in_radius( pos, 10 ) ) {
                if( n.sees( p ) ) {
                    map_stack items = m.i_at( p );
                    for( auto it = items.begin(); it != items.end(); ++it ) {
                        total_items_checked++;
                        item *item_ptr = *it;
                        std::string item_cat = item_ptr->get_category().get_id().str();
                        std::string item_id = item_ptr->typeId().str();
                        
                        if( item_cat == target ) {
                            ai_log( string_format( "[CATEGORY_MATCH] %s -> Extracted nearby item name '%s'", target, item_id ) );
                            items_to_pick.push_back( item_id );
                        }
                    }
                }
            }
            ai_log( string_format( "[CATEGORY_MATCH] %s -> Nearby items checked: %d", target, total_items_checked ) );

            if( !items_to_pick.empty() ) {
                std::string result;
                for( const std::string &id : items_to_pick ) {
                    ai_log( string_format( "[EXECUTOR] Passing '%s' to pickup executor", id ) );
                    std::string res = pick_up( n, id );
                    if( result.empty() ) {
                        result = res;
                    } else {
                        result += "\n" + res;
                    }
                }
                return result;
            } else {
                ai_log( string_format( "[CATEGORY_MATCH] No items extracted for category '%s'", target ) );
            }
        }

        ai_log( string_format( "[CATEGORY_MATCH] Fallback activated for '%s'", target ) );
        return pick_up( n, target );
    }

    if( action == "resupply" ) {
        ai_log( string_format( "[RESUPPLY] %s triggered deterministic resupply", n.name ) );
        return execute_resupply( n );
    }

    return string_format( _( "Unknown action: %s" ), action );
}

auto pick_up( npc &n, const std::string &item_id ) -> std::string
{
    map &m = get_map();
    const tripoint &pos = n.pos();
    
    ai_log( string_format( "[EXECUTOR] AI [%s] attempting to pick up: %s", n.name, item_id ) );

    int items_checked = 0;
    int tiles_checked = 0;

    // Use radius 10 and NPC's own FOV
    for( const tripoint &p : m.points_in_radius( pos, 10 ) ) {
        tiles_checked++;
        // Use Character::sees to check if NPC can actually see the tile/item
        if( n.sees( p ) ) {
            map_stack items = m.i_at( p );
            for( auto it = items.begin(); it != items.end(); ++it ) {
                items_checked++;
                item *item_ptr = *it;
                const std::string &this_id = item_ptr->typeId().str();
                
                if( this_id == item_id ) {
                    // Check capacity
                    if( !n.can_pick_volume( *item_ptr ) ) {
                        std::string err = string_format( _( "I can't carry the %s, no space left." ), item_ptr->tname() );
                        ai_log( string_format( "[EXECUTOR] AI Error: %s", err ) );
                        return err;
                    }
                    if( !n.can_pick_weight( *item_ptr ) ) {
                        std::string err = string_format( _( "I can't carry the %s, too heavy." ), item_ptr->tname() );
                        ai_log( string_format( "[EXECUTOR] AI Error: %s", err ) );
                        return err;
                    }

                    std::string item_name = item_ptr->tname();
                    
                    // Remove from map
                    detached_ptr<item> picked = m.i_rem( p, item_ptr );
                    
                    if( picked ) {
                        n.i_add( std::move( picked ) );
                        n.has_new_items = true;
                        ai_log( string_format( "[EXECUTOR] %s picked up %s.", n.name, item_name ) );
                        return string_format( _( "Successfully picked up %s." ), item_name );
                    } else {
                        ai_log( string_format( "[ERROR] AI Error: map::i_rem returned null for %s", item_id ) );
                    }
                }
            }
        }
    }
    
    ai_log( string_format( "[EXECUTOR] AI Search Summary: Checked %d tiles, %d items. No match for '%s'.", tiles_checked, items_checked, item_id ) );
    return string_format( _( "Could not find item with ID '%s' in my sight (radius 10)." ), item_id );
}

// ─── execute_resupply ─────────────────────────────────────────────────────────

namespace {

/// A floor item that matches the active resupply plan, with cached distance.
struct resupply_candidate {
    tripoint pos;
    item    *ptr;
    int      dist;
};

} // anonymous namespace

auto execute_resupply( npc &n ) -> std::string
{
    // ── 1. Compute what is needed ─────────────────────────────────────────
    const auto plan_opt = npc_weapon_cache::compute_resupply_plan( n );
    if( !plan_opt ) {
        ai_log( string_format( "[RESUPPLY] %s has no ranged weapon / weapon not in cache", n.name ) );
        return _( "No ranged weapon equipped." );
    }
    const auto &plan = *plan_opt;
    if( plan.is_satisfied ) {
        ai_log( string_format( "[RESUPPLY] %s is already fully stocked", n.name ) );
        return _( "Already fully stocked." );
    }

    // ── 2. Build O(1) lookup sets from the plan ───────────────────────────
    std::unordered_set<itype_id> wanted_mags;
    std::unordered_set<itype_id> wanted_ammo;
    int mags_needed = 0;
    int ammo_needed = 0;

    if( plan.magazines ) {
        mags_needed = plan.magazines->missing;
        for( const auto &id : plan.magazines->compatible_ids ) {
            wanted_mags.insert( id );
        }
    }
    if( plan.ammo ) {
        ammo_needed = plan.ammo->missing;
        for( const auto &id : plan.ammo->compatible_ids ) {
            wanted_ammo.insert( id );
        }
    }

    ai_log( string_format( "[RESUPPLY] %s needs %d mag(s) and %d round(s) for %s",
                           n.name, mags_needed, ammo_needed, plan.weapon_id.str() ) );

    // ── 3. Scan nearby tiles, collect matching candidates ─────────────────
    map    &m   = get_map();
    const tripoint &pos = n.pos();
    constexpr int SCAN_RADIUS = 10;

    std::vector<resupply_candidate> candidates;
    for( const tripoint &p : m.points_in_radius( pos, SCAN_RADIUS ) ) {
        if( !n.sees( p ) ) {
            continue;
        }
        map_stack tile_items = m.i_at( p );
        for( auto it = tile_items.begin(); it != tile_items.end(); ++it ) {
            item *iptr = *it;
            const itype_id &tid = iptr->typeId();
            // Fast reverse-index check — skips 99%+ of items
            if( !npc_weapon_cache::is_resupply_item( tid ) ) {
                continue;
            }
            if( !wanted_mags.contains( tid ) && !wanted_ammo.contains( tid ) ) {
                continue;
            }
            candidates.push_back( resupply_candidate{
                .pos  = p,
                .ptr  = iptr,
                .dist = rl_dist( pos, p ),
            } );
        }
    }

    // ── 4. Sort closest first ─────────────────────────────────────────────
    std::ranges::sort( candidates, {}, &resupply_candidate::dist );

    // ── 5. Pick up until shortages are satisfied ──────────────────────────
    int mags_picked  = 0;
    int ammo_picked  = 0;

    for( auto &cand : candidates ) {
        // ── 5a. RE-COMPUTE LIVE PLAN ─────────────────────────────────────
        // Strictly follow "live state" requirement: recalculate needs after each pickup.
        const auto live_plan_opt = npc_weapon_cache::compute_resupply_plan( n );
        if( !live_plan_opt || live_plan_opt->is_satisfied ) {
            ai_log( string_format( "[RESUPPLY] %s is now satisfied, stopping pickup loop.", n.name ) );
            break;
        }
        const auto &live_plan = *live_plan_opt;

        // Check if THIS specific candidate is still needed
        const itype_id &tid = cand.ptr->typeId();
        bool still_needed = false;
        bool is_mag = false;

        if( live_plan.magazines && live_plan.magazines->missing > 0 ) {
            for( const auto &id : live_plan.magazines->compatible_ids ) {
                if( id == tid ) {
                    still_needed = true;
                    is_mag = true;
                    break;
                }
            }
        }
        if( !still_needed && live_plan.ammo && live_plan.ammo->missing > 0 ) {
            for( const auto &id : live_plan.ammo->compatible_ids ) {
                if( id == tid ) {
                    still_needed = true;
                    is_mag = false;
                    break;
                }
            }
        }

        if( !still_needed ) {
            continue;
        }

        // ── 5b. Execute pickup ───────────────────────────────────────────
        if( !n.can_pick_volume( *cand.ptr ) ) {
            continue;
        }
        if( !n.can_pick_weight( *cand.ptr ) ) {
            continue;
        }

        const int         charges   = cand.ptr->charges;
        const std::string item_name = cand.ptr->tname();

        auto picked = m.i_rem( cand.pos, cand.ptr );
        if( !picked ) {
            continue;
        }

        n.i_add( std::move( picked ) );
        n.has_new_items = true;

        if( is_mag ) {
            ++mags_picked;
            ai_log( string_format( "[RESUPPLY] Picked magazine: %s", item_name ) );
        } else {
            ammo_picked += charges;
            ai_log( string_format( "[RESUPPLY] Picked ammo: %s x%d", item_name, charges ) );
        }
    }

    const auto final_result = string_format(
        _( "Resupply complete: picked up %d magazine(s) and %d round(s)." ),
        mags_picked, ammo_picked );
    ai_log( string_format( "[RESUPPLY] %s — %s", n.name, final_result ) );
    return final_result;
}

} // namespace ai_actions
