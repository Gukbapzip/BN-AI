#include "npc_recipe_cache.h"
#include "recipe_dictionary.h"
#include "recipe.h"
#include "itype.h"
#include "item_factory.h"
#include "string_formatter.h"
#include "debug.h"
#include <unordered_map>
#include <algorithm>

namespace npc_recipe_cache
{

namespace
{

bool s_built = false;
std::unordered_map<itype_id, std::vector<const recipe *>> s_result_index;

} // namespace

void init()
{
    if( s_built ) {
        return;
    }

    s_result_index.clear();

    // Index all standard recipes by their result type.
    // Single un-nested loop: compliant with AGENTS.md.
    for( auto it = recipe_dict.begin(); it != recipe_dict.end(); ++it ) {
        const recipe &r = it->second;
        s_result_index[r.result()].push_back( &r );
    }

    s_built = true;
    DebugLog( DL::Info, DC::NPC ) << "[RECIPE_CACHE] Indexed " << s_result_index.size() << " result types.";
}

void reset()
{
    s_result_index.clear();
    s_built = false;
}

auto get_recipes_by_result( const itype_id &id ) -> const std::vector<const recipe *> &
{
    static const std::vector<const recipe *> empty_list;
    const auto it = s_result_index.find( id );
    if( it != s_result_index.end() ) {
        return it->second;
    }
    return empty_list;
}


// ── Name → itype_id index (built lazily, recipe-index-scoped) ────────────────
namespace {
// Maps normalized name/alias → itype_id. Only covers items with known recipes.
static std::unordered_map<std::string, itype_id> s_name_to_id;
static bool s_name_index_built = false;

auto normalize_query( const std::string &raw ) -> std::string
{
    std::string out;
    out.reserve( raw.size() );
    for( const char c : raw ) {
        out += static_cast<char>( std::isalnum( static_cast<unsigned char>( c ) )
                                      ? std::tolower( static_cast<unsigned char>( c ) )
                                      : '_' );
    }
    // Collapse consecutive underscores and strip leading/trailing
    std::string collapsed;
    collapsed.reserve( out.size() );
    bool prev_under = false;
    for( const char c : out ) {
        if( c == '_' && prev_under ) {
            continue;
        }
        prev_under = ( c == '_' );
        collapsed += c;
    }
    const auto s = collapsed.find_first_not_of( '_' );
    const auto e = collapsed.find_last_not_of( '_' );
    return ( s == std::string::npos ) ? "" : collapsed.substr( s, e - s + 1 );
}

auto build_name_index() -> void
{
    if( s_name_index_built ) {
        return;
    }
    s_name_to_id.clear();
    for( const auto &[id, _] : s_result_index ) {
        // Register raw id
        s_name_to_id[id.str()] = id;
        // Register normalized id form
        s_name_to_id[normalize_query( id.str() )] = id;
        // Register localized name
        std::string loc = item::nname( id );
        std::transform( loc.begin(), loc.end(), loc.begin(), ::tolower );
        s_name_to_id[loc] = id;
        // Register normalized localized name
        s_name_to_id[normalize_query( loc )] = id;
    }
    s_name_index_built = true;
}
} // namespace (name index)

namespace {

/// Split string into lowercase tokens
auto tokenize( const std::string &s ) -> std::vector<std::string>
{
    std::vector<std::string> tokens;
    std::string current;
    for( char c : s ) {
        if( std::isalnum( static_cast<unsigned char>( c ) ) ) {
            current += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        } else if( !current.empty() ) {
            tokens.push_back( current );
            current.clear();
        }
    }
    if( !current.empty() ) {
        tokens.push_back( current );
    }
    return tokens;
}

} // namespace

auto resolve_item_id( const std::string &raw_query ) -> resolve_result
{
    if( !s_built ) {
        return resolve_result{ false, itype_id::NULL_ID(), _( "Recipe cache not initialized." ) };
    }
    build_name_index();

    const std::string query_low = normalize_query( raw_query );
    const auto query_tokens = tokenize( raw_query );
    
    if( query_tokens.empty() ) {
        return resolve_result{ false, itype_id::NULL_ID(), _( "Empty query." ) };
    }

    struct candidate {
        itype_id id;
        int score = 0;
        auto operator<=>( const candidate &rhs ) const {
            if( score != rhs.score ) return score <=> rhs.score;
            // Tie-break: prefer longer (more specific) IDs
            return id.str().size() <=> rhs.id.str().size();
        }
    };
    std::vector<candidate> best_matches;

    for( const auto &[indexed_name, id] : s_name_to_id ) {
        int current_score = 0;
        const auto target_tokens = tokenize( indexed_name );
        
        // 1. Token Overlap (Base: 100 per token)
        int matches = 0;
        for( const auto &qt : query_tokens ) {
            if( std::ranges::any_of( target_tokens, [&]( const std::string & tt ) { return tt == qt; } ) ) {
                matches++;
            }
        }
        
        if( matches == 0 ) continue;
        current_score = matches * 100;

        // 2. Phrase Match Bonus (+40)
        if( indexed_name.find( query_low ) != std::string::npos ) {
            current_score += 40;
        }

        // 3. Specificity Bonus (+30 for exact match)
        if( indexed_name == query_low ) {
            current_score += 30;
        }

        // 4. Generic Penalty (-50 for short/generic IDs)
        if( id.str().size() <= 4 ) {
            current_score -= 50;
        }

        // 5. Rejection Threshold (Strict 120)
        if( current_score >= 120 ) {
            best_matches.push_back( { id, current_score } );
        }
    }

    if( !best_matches.empty() ) {
        std::ranges::sort( best_matches, std::greater<>() );
        const auto &winner = best_matches.front();
        return resolve_result{ true, winner.id, get_recipe_deterministic_string( winner.id ) };
    }

    return resolve_result{
        false,
        itype_id::NULL_ID(),
        string_format( _( "No recipe found for '%s'." ), raw_query.c_str() )
    };
}

static auto resolve_wildcard_id( const itype_id &id ) -> itype_id
{
    std::string s_id = id.str();
    if( !s_id.ends_with( "_any" ) ) {
        return id;
    }

    // Deterministic expansion: find the first item that satisfies this group
    // In BN, these are often defined as 'requirement' IDs or item groups.
    // We'll search the item factory for the best representative.
    for( const itype *itp : item_controller->all() ) {
        // If the item ID contains the group prefix, it's a candidate.
        // e.g., 'leather_patch' matches 'fabric_hides_any' if it shares tags/materials
        // For now, use a strict prefix/naming check as the engine flattens requirements.
        std::string candidate = itp->get_id().str();
        if( candidate.find( s_id.substr( 0, s_id.size() - 4 ) ) != std::string::npos ) {
            return itp->get_id();
        }
    }

    return itype_id( "RESOLVE_FAILED" );
}

static auto format_item_name( const itype_id &id ) -> std::string
{
    const itype_id resolved = resolve_wildcard_id( id );
    if( resolved.str() == "RESOLVE_FAILED" ) {
        return string_format( _( "<Unknown Group: %s>" ), id.c_str() );
    }
    
    if( id != resolved ) {
        return string_format( _( "%s (from %s)" ), item::nname( resolved ).c_str(), id.c_str() );
    }

    return item::nname( id );
}

auto get_recipe_deterministic_string( const itype_id &id ) -> std::string
{
    const auto &recipes = get_recipes_by_result( id );
    if( recipes.empty() ) {
        return _( "No known recipe for " ) + item::nname( id ) + ".";
    }

    const recipe *r = recipes[0];
    std::string out = string_format( _( "--- Recipe: %s ---\n" ), item::nname( id ).c_str() );
    
    out += string_format( _( "Skill: %s (%d)\n" ), r->skill_used.str().c_str(), r->difficulty );

    const auto &quals = r->simple_requirements().get_qualities();
    if( !quals.empty() ) {
        out += _( "Qualities: " );
        bool first = true;
        for( const auto &q_list : quals ) {
            if( !q_list.empty() ) {
                if( !first ) out += ", ";
                out += string_format( "%s (%d)", q_list[0].type.str().c_str(), q_list[0].level );
                first = false;
            }
        }
        out += "\n";
    }

    out += _( "Tools: " );
    bool first_tool = true;
    for( const auto &tool : r->simple_requirements().get_tools() ) {
        if( !tool.empty() ) {
            if( !first_tool ) out += ", ";
            out += format_item_name( tool[0].type );
            first_tool = false;
        }
    }
    
    out += _( "\nIngredients: " );
    bool first_ing = true;
    for( const auto &comps : r->simple_requirements().get_components() ) {
        if( !comps.empty() ) {
            if( !first_ing ) out += ", ";
            out += string_format( "%dx %s", comps[0].count, format_item_name( comps[0].type ).c_str() );
            first_ing = false;
        }
    }
    
    return out;
}

} // namespace npc_recipe_cache
