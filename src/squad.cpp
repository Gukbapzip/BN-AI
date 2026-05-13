#include "squad.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "character_id.h"
#include "creature.h"
#include "debug.h"
#include "game.h"
#include "json.h"
#include "messages.h"
#include "npc.h"
#include "point.h"
#include "translations.h"

// ── Conversion helpers ────────────────────────────────────────────────────
auto squad_id_to_name( squad_id id ) -> std::string
{
    static const std::map<squad_id, std::string> names = {
        { squad_id::SQ_NONE, "" },
        { squad_id::SQ_ALPHA, "Alpha" },
        { squad_id::SQ_BRAVO, "Bravo" },
        { squad_id::SQ_CHARLIE, "Charlie" },
        { squad_id::SQ_DELTA, "Delta" },
        { squad_id::SQ_ECHO, "Echo" },
        { squad_id::SQ_FOXTROT, "Foxtrot" },
        { squad_id::SQ_GOLF, "Golf" },
        { squad_id::SQ_HOTEL, "Hotel" },
        { squad_id::SQ_INDIA, "India" },
        { squad_id::SQ_JULIETT, "Juliett" },
        { squad_id::SQ_KILO, "Kilo" },
        { squad_id::SQ_LIMA, "Lima" },
        { squad_id::SQ_MIKE, "Mike" },  
        { squad_id::SQ_NOVEMBER, "November" },
        { squad_id::SQ_OSCAR, "Oscar" },
        { squad_id::SQ_PAPA, "Papa" },
        { squad_id::SQ_QUEBEC, "Quebec" },
        { squad_id::SQ_ROMEO, "Romeo" },
        { squad_id::SQ_SIERRA, "Sierra" },
        { squad_id::SQ_TANGO, "Tango" },
        { squad_id::SQ_UNIFORM, "Uniform" },
        { squad_id::SQ_VICTOR, "Victor" },
        { squad_id::SQ_WHISKEY, "Whiskey" },
        { squad_id::SQ_XRAY, "Xray" },
        { squad_id::SQ_YANKEE, "Yankee" },
        { squad_id::SQ_ZULU, "Zulu" },
    };
    const auto it = names.find( id );
    if( it != names.end() ) {
        return it->second;
    }
    return "???";
}

auto tactical_mode_to_name( tactical_mode mode ) -> std::string
{
    static const std::map<tactical_mode, std::string> names = {
        { tactical_mode::IDLE, "Idle" },
        { tactical_mode::FOLLOW, "Follow" },
        { tactical_mode::AGGRESSIVE, "Aggressive" },
        { tactical_mode::DEFENSIVE, "Defensive" },
        { tactical_mode::STORM, "Storm" },
    };
    const auto it = names.find( mode );
    if( it != names.end() ) {
        return it->second;
    }
    return "???";
}

auto squad_formation_to_name( squad_formation f ) -> std::string
{
    static const std::map<squad_formation, std::string> names = {
        { squad_formation::LINE, "Line" },
        { squad_formation::WEDGE, "Wedge" },
        { squad_formation::COLUMN, "Column" },
    };
    const auto it = names.find( f );
    if( it != names.end() ) {
        return it->second;
    }
    return "???";
}

// ── Squad implementation ──────────────────────────────────────────────────

Squad::Squad( squad_id id )
    : id_( id )
{
}

auto Squad::get_id() const -> squad_id
{
    return id_;
}

auto Squad::get_name() const -> std::string
{
    return squad_id_to_name( id_ );
}

void Squad::add_member( character_id who )
{
    if( has_member( who ) ) {
        return;
    }
    members_.emplace_back( who );
}

void Squad::remove_member( character_id who )
{
    members_.erase( std::remove( members_.begin(), members_.end(), who ),
                    members_.end() );
    if( leader_ == who ) {
        leader_ = character_id{};
    }
}

auto Squad::has_member( character_id who ) const -> bool
{
    return std::find( members_.begin(), members_.end(), who ) != members_.end();
}

auto Squad::get_member_ids() const -> const std::vector<character_id> &
{
    return members_;
}

auto Squad::get_member_ptr( size_t idx ) const -> npc *
{
    if( idx >= members_.size() ) {
        return nullptr;
    }
    return g->critter_by_id<npc>( members_[idx] );
}

auto Squad::get_loaded_members() const -> std::vector<npc *>
{
    std::vector<npc *> result;
    for( const character_id &id : members_ ) {
        npc *const who = g->critter_by_id<npc>( id );
        if( who != nullptr ) {
            result.push_back( who );
        }
    }
    return result;
}

void Squad::set_leader( character_id who )
{
    if( !has_member( who ) ) {
        add_member( who );
    }
    leader_ = who;
}

void Squad::clear_leader()
{
    leader_ = character_id{};
}

auto Squad::get_leader_id() const -> character_id
{
    return leader_;
}

auto Squad::get_leader_ptr() const -> npc *
{
    if( !leader_.is_valid() ) {
        return nullptr;
    }
    return g->critter_by_id<npc>( leader_ );
}

void Squad::set_tactical_mode( tactical_mode mode )
{
    mode_ = mode;
}

auto Squad::get_tactical_mode() const -> tactical_mode
{
    return mode_;
}

void Squad::set_formation( squad_formation f )
{
    formation_ = f;
}

auto Squad::get_formation() const -> squad_formation
{
    return formation_;
}

auto Squad::get_formation_offset( character_id n_id,
                                  const tripoint &leader_facing_dir ) const -> tripoint
{
    // Find the index of this member in the squad (excluding the leader).
    size_t member_idx = 0;
    bool found = false;
    for( size_t i = 0; i < members_.size(); ++i ) {
        if( members_[i] == n_id ) {
            member_idx = i;
            found = true;
            break;
        }
    }
    if( !found ) {
        return tripoint_zero;
    }

    // Normalize facing direction to a unit step in the cardinal/ordinal grid.
    const auto sign = []( int v ) -> int {
        return ( v > 0 ) ? 1 : ( ( v < 0 ) ? -1 : 0 );
    };

    const int fx = sign( leader_facing_dir.x );
    const int fy = sign( leader_facing_dir.y );

    // Compute perpendicular basis vectors (right and forward).
    //   forward = (fx, fy)
    //   right   = (-fy, fx)
    const int rx = -fy;
    const int ry =  fx;

    // Spacing in tiles between squad members.
    constexpr int lateral_spacing = 2;
    constexpr int depth_spacing   = 1;

    switch( formation_ ) {
        case squad_formation::LINE: {
            // Line abreast: even = left, odd = right, alternating outward.
            const int side = ( member_idx % 2 == 0 ) ? -1 : 1;
            const int rank = static_cast<int>( member_idx / 2 ) + 1;
            return tripoint{
                rx * side * lateral_spacing * rank,
                ry * side * lateral_spacing * rank,
                0
            };
        }

        case squad_formation::WEDGE: {
            // Inverted-V behind leader.
            const int rank = static_cast<int>( member_idx / 2 ) + 1;
            if( member_idx % 2 == 0 ) {
                // Left side: behind-left
                return tripoint{
                    -fx * depth_spacing * rank + rx * ( -lateral_spacing * rank ),
                    -fy * depth_spacing * rank + ry * ( -lateral_spacing * rank ),
                    0
                };
            } else {
                // Right side: behind-right
                return tripoint{
                    -fx * depth_spacing * rank + rx * ( lateral_spacing * rank ),
                    -fy * depth_spacing * rank + ry * ( lateral_spacing * rank ),
                    0
                };
            }
        }

        case squad_formation::COLUMN:
        default: {
            // Single file.
            const int rank = static_cast<int>( member_idx ) + 1;
            return tripoint{
                -fx * depth_spacing * rank,
                -fy * depth_spacing * rank,
                0
            };
        }
    }
}

auto Squad::get_status_summary() const -> std::string
{
    const std::vector<npc *> loaded = get_loaded_members();
    const int alive_count = static_cast<int>( std::count_if( loaded.begin(), loaded.end(),
    []( const npc * m ) {
        return m != nullptr && !m->is_dead();
    } ) );
    return get_name() + " [" + tactical_mode_to_name( mode_ ) + "/" +
           squad_formation_to_name( formation_ ) + "] (" +
           std::to_string( alive_count ) + " alive)";
}

void Squad::cleanup_dead()
{
    const auto dead_pred = []( const character_id &id ) {
        npc *const who = g->critter_by_id<npc>( id );
        return who == nullptr || who->is_dead();
    };
    members_.erase( std::remove_if( members_.begin(), members_.end(), dead_pred ),
                    members_.end() );

    if( leader_.is_valid() ) {
        npc *const leader_ptr = g->critter_by_id<npc>( leader_ );
        if( leader_ptr == nullptr || leader_ptr->is_dead() ) {
            leader_ = character_id{};
        }
    }
}

// ── Persistence: Squad ───────────────────────────────────────────────────

void Squad::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "id", static_cast<int>( id_ ) );
    jsout.member( "mode", static_cast<int>( mode_ ) );
    jsout.member( "formation", static_cast<int>( formation_ ) );

    jsout.member( "members" );
    jsout.start_array();
    for( const character_id &cid : members_ ) {
        jsout.write( cid );
    }
    jsout.end_array();

    jsout.member( "leader" );
    jsout.write( leader_ );

    jsout.end_object();
}

void Squad::deserialize( const JsonObject &jo )
{
    id_ = static_cast<squad_id>( jo.get_int( "id", 0 ) );
    mode_ = static_cast<tactical_mode>( jo.get_int( "mode", 0 ) );
    formation_ = static_cast<squad_formation>( jo.get_int( "formation", 0 ) );

    members_.clear();
    if( jo.has_array( "members" ) ) {
        JsonArray ja = jo.get_array( "members" );
        while( ja.has_more() ) {
            character_id cid;
            ja.read_next( cid );
            if( cid.is_valid() ) {
                members_.push_back( cid );
            }
        }
    }

    // Read the leader as a raw value from the leader member.
    if( jo.has_member( "leader" ) ) {
        JsonIn *jsin = jo.get_raw( "leader" );
        leader_.deserialize( *jsin );
    }
}

// ── SquadManager implementation ───────────────────────────────────────────

auto SquadManager::get() -> SquadManager &
{
    static SquadManager instance;
    return instance;
}

auto SquadManager::get_all_squads() -> std::map<squad_id, Squad> &
{
    return squads_;
}

auto SquadManager::get_squad( squad_id id ) -> Squad &
{
    const auto it = squads_.find( id );
    if( it != squads_.end() ) {
        return it->second;
    }
    auto [new_it, _] = squads_.emplace( id, Squad( id ) );
    return new_it->second;
}

auto SquadManager::get_squad_for( character_id who ) -> Squad *
{
    const auto it = npc_to_squad_map_.find( who );
    if( it != npc_to_squad_map_.end() && it->second != squad_id::SQ_NONE ) {
        const auto squad_it = squads_.find( it->second );
        if( squad_it != squads_.end() ) {
            return &squad_it->second;
        }
    }
    return nullptr;
}

auto SquadManager::get_squad_for( character_id who ) const -> const Squad *
{
    const auto it = npc_to_squad_map_.find( who );
    if( it != npc_to_squad_map_.end() && it->second != squad_id::SQ_NONE ) {
        const auto squad_it = squads_.find( it->second );
        if( squad_it != squads_.end() ) {
            return &squad_it->second;
        }
    }
    return nullptr;
}

void SquadManager::assign_npc_to_squad( npc &who, squad_id id )
{
    const character_id cid = who.getID();

    // Remove from previous squad first.
    remove_npc_from_squad( who );

    if( id == squad_id::SQ_NONE ) {
        who.current_squad = squad_id::SQ_NONE;
        who.is_squad_leader = false;
        return;
    }

    Squad &squad = get_squad( id );
    squad.add_member( cid );
    npc_to_squad_map_[cid] = id;

    who.current_squad = id;
    who.is_squad_leader = false;
}

void SquadManager::remove_npc_from_squad( npc &who )
{
    const character_id cid = who.getID();
    const auto it = npc_to_squad_map_.find( cid );
    if( it != npc_to_squad_map_.end() ) {
        const squad_id old_id = it->second;
        if( old_id != squad_id::SQ_NONE ) {
            const auto squad_it = squads_.find( old_id );
            if( squad_it != squads_.end() ) {
                squad_it->second.remove_member( cid );
            }
        }
        npc_to_squad_map_.erase( it );
    }

    who.current_squad = squad_id::SQ_NONE;
    who.is_squad_leader = false;
}

void SquadManager::promote_to_leader( npc &who )
{
    const character_id cid = who.getID();
    const auto it = npc_to_squad_map_.find( cid );
    if( it == npc_to_squad_map_.end() || it->second == squad_id::SQ_NONE ) {
        return;
    }

    Squad &squad = get_squad( it->second );
    squad.set_leader( cid );

    who.is_squad_leader = true;

    for( const character_id &mid : squad.get_member_ids() ) {
        if( mid == cid ) {
            continue;
        }
        npc *const other = g->critter_by_id<npc>( mid );
        if( other != nullptr ) {
            other->is_squad_leader = false;
        }
    }
}

void SquadManager::sync_squad_tactics( squad_id id, tactical_mode mode )
{
    if( !is_valid( id ) ) {
        return;
    }
    get_squad( id ).set_tactical_mode( mode );
}

void SquadManager::sync_squad_formation( squad_id id, squad_formation f )
{
    if( !is_valid( id ) ) {
        return;
    }
    get_squad( id ).set_formation( f );
}

auto SquadManager::get_tactical_summary() const -> std::string
{
    std::string result;
    for( const auto &pair : squads_ ) {
        if( pair.first == squad_id::SQ_NONE ) {
            continue;
        }
        const Squad &s = pair.second;
        if( s.get_member_ids().empty() ) {
            continue;
        }
        if( !result.empty() ) {
            result += " | ";
        }
        result += s.get_status_summary();
    }
    if( result.empty() ) {
        result = "No active squads.";
    }
    return result;
}

void SquadManager::cleanup_all()
{
    for( auto &pair : squads_ ) {
        pair.second.cleanup_dead();
    }

    for( auto it = npc_to_squad_map_.begin(); it != npc_to_squad_map_.end(); ) {
        npc *const who = g->critter_by_id<npc>( it->first );
        if( who == nullptr || who->is_dead() ) {
            it = npc_to_squad_map_.erase( it );
        } else {
            ++it;
        }
    }
}

auto SquadManager::get_npc_squad_id( character_id who ) const -> squad_id
{
    const auto it = npc_to_squad_map_.find( who );
    if( it != npc_to_squad_map_.end() ) {
        return it->second;
    }
    return squad_id::SQ_NONE;
}

auto SquadManager::is_valid( squad_id id ) -> bool
{
    return id > squad_id::SQ_NONE && id < squad_id::SQ_END;
}

// ── Persistence: SquadManager ────────────────────────────────────────────

void SquadManager::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "squads" );
    jsout.start_array();
    for( const auto &pair : squads_ ) {
        if( pair.first == squad_id::SQ_NONE ) {
            continue;
        }
        if( pair.second.get_member_ids().empty() ) {
            continue;
        }
        pair.second.serialize( jsout );
    }
    jsout.end_array();

    jsout.member( "npc_to_squad_map" );
    jsout.start_array();
    for( const auto &entry : npc_to_squad_map_ ) {
        if( entry.second == squad_id::SQ_NONE ) {
            continue;
        }
        jsout.start_object();
        jsout.member( "npc_id" );
        jsout.write( entry.first );
        jsout.member( "squad_id", static_cast<int>( entry.second ) );
        jsout.end_object();
    }
    jsout.end_array();

    jsout.end_object();
}

void SquadManager::deserialize( const JsonObject &jo )
{
    squads_.clear();
    npc_to_squad_map_.clear();

    if( jo.has_array( "squads" ) ) {
        JsonArray ja = jo.get_array( "squads" );
        while( ja.has_more() ) {
            Squad s( squad_id::SQ_NONE );
            JsonObject squad_obj = ja.next_object();
            squad_obj.allow_omitted_members();
            s.deserialize( squad_obj );
            if( s.get_id() != squad_id::SQ_NONE ) {
                squads_.emplace( s.get_id(), std::move( s ) );
            }
        }
    }

    if( jo.has_array( "npc_to_squad_map" ) ) {
        JsonArray ja = jo.get_array( "npc_to_squad_map" );
        while( ja.has_more() ) {
            JsonObject entry_obj = ja.next_object();
            entry_obj.allow_omitted_members();
            character_id cid;
            {
                JsonIn *jsin = entry_obj.get_raw( "npc_id" );
                cid.deserialize( *jsin );
            }
            const int sid_tmp = entry_obj.get_int( "squad_id", 0 );
            if( cid.is_valid() ) {
                const squad_id sid = static_cast<squad_id>( sid_tmp );
                if( is_valid( sid ) ) {
                    npc_to_squad_map_[cid] = sid;
                }
            }
        }
    }
}

// ── squad_helpers implementation ─────────────────────────────────────────

namespace squad_helpers
{

auto get_npc_squad_id( const npc &who ) -> squad_id
{
    return SquadManager::get().get_npc_squad_id( who.getID() );
}

auto squad_display_prefix( const npc &who ) -> std::string
{
    const squad_id sid = get_npc_squad_id( who );
    if( sid == squad_id::SQ_NONE ) {
        return {};
    }

    const Squad *sq = SquadManager::get().get_squad_for( who.getID() );
    if( sq == nullptr ) {
        return {};
    }

    const bool is_leader = ( sq->get_leader_id() == who.getID() );
    return "[" + squad_id_to_name( sid ) + ( is_leader ? "*] " : "] " );
}

auto is_valid_squad_leader( const npc &who ) -> bool
{
    if( who.is_dead() || !who.is_player_ally() ) {
        return false;
    }
    const Squad *sq = SquadManager::get().get_squad_for( who.getID() );
    if( sq == nullptr ) {
        return false;
    }
    return sq->get_leader_id() == who.getID();
}

auto handle_leader_death( npc &dead_npc ) -> npc *
{
    Squad *sq = SquadManager::get().get_squad_for( dead_npc.getID() );
    if( sq == nullptr ) {
        return nullptr;
    }

    if( sq->get_leader_id() == dead_npc.getID() ) {
        sq->clear_leader();
    }

    for( npc *member : sq->get_loaded_members() ) {
        if( member != nullptr && !member->is_dead() && member->is_player_ally() &&
            member->getID() != dead_npc.getID() ) {
            sq->set_leader( member->getID() );
            member->is_squad_leader = true;
            add_msg( m_info, _( "[%s] %s is now the squad leader." ),
                     sq->get_name(), member->disp_name() );
            return member;
        }
    }

    add_msg( m_warning, _( "[%s] Squad has no leader remaining." ),
             sq->get_name() );
    return nullptr;
}

void move_to_formation_position( npc &who )
{
    SquadManager &mgr = SquadManager::get();
    Squad *sq = mgr.get_squad_for( who.getID() );
    if( sq == nullptr ) {
        return;
    }

    npc *const leader = sq->get_leader_ptr();
    if( leader == nullptr || leader->is_dead() || leader->getID() == who.getID() ) {
        return;
    }

    // Skip if NPC is already in combat with a target.
    if( who.current_target() != nullptr ) {
        return;
    }

    const tripoint leader_pos = leader->pos();

    // Compute a facing direction for formation orientation.
    // Use the direction from the leader to this NPC as the "forward" vector.
    tripoint facing_dir = leader_pos - who.pos();
    if( facing_dir.x == 0 && facing_dir.y == 0 ) {
        facing_dir = tripoint_north;
    }

    const tripoint offset = sq->get_formation_offset( who.getID(), facing_dir );
    const tripoint target_pos = leader_pos + offset;

    const int dist = rl_dist( who.pos(), target_pos );
    if( dist > 1 ) {
        // move_to is a placeholder — in real usage, this would use
        // the NPC's pathfinding or direct step movement.
        who.move_to( target_pos, false, nullptr );
    }
}

} // namespace squad_helpers
