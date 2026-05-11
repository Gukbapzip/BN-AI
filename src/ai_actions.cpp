#include "ai_actions.h"
#include "npc.h"
#include "map.h"
#include "map_iterator.h"
#include "game.h"
#include "item.h"
#include "messages.h"
#include "translations.h"
#include "debug.h"

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

} // namespace ai_actions
