#include "ai_actions.h"
#include "npc.h"
#include "map.h"
#include "map_iterator.h"
#include "game.h"
#include "item.h"
#include "messages.h"
#include "translations.h"
#include "debug.h"

namespace ai_actions {

auto execute_command( npc &n, const std::string &action, const std::string &target ) -> std::string
{
    if( action == "pick_up" ) {
        return pick_up( n, target );
    }
    
    return string_format( _( "Unknown action: %s" ), action );
}

auto pick_up( npc &n, const std::string &item_id ) -> std::string
{
    map &m = get_map();
    const tripoint &pos = n.pos();
    
    add_msg( m_info, _( "AI [%s] attempting to pick up: %s" ), n.name, item_id );

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
                        add_msg( m_info, "AI: %s", err );
                        return err;
                    }
                    if( !n.can_pick_weight( *item_ptr ) ) {
                        std::string err = string_format( _( "I can't carry the %s, too heavy." ), item_ptr->tname() );
                        add_msg( m_info, "AI: %s", err );
                        return err;
                    }

                    std::string item_name = item_ptr->tname();
                    
                    // Remove from map
                    detached_ptr<item> picked = m.i_rem( p, item_ptr );
                    
                    if( picked ) {
                        n.i_add( std::move( picked ) );
                        n.has_new_items = true;
                        add_msg( m_info, _( "%s picked up %s." ), n.name, item_name );
                        return string_format( _( "Successfully picked up %s." ), item_name );
                    } else {
                        add_msg( m_info, "AI Error: map::i_rem returned null for %s", item_id );
                    }
                }
            }
        }
    }
    
    add_msg( m_info, "AI Search Summary: Checked %d tiles, %d items. No match for '%s'.", tiles_checked, items_checked, item_id );
    return string_format( _( "Could not find item with ID '%s' in my sight (radius 10)." ), item_id );
}

} // namespace ai_actions
