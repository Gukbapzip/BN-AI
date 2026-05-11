/// npc_item_search.cpp
/// Item Search Service — substring candidate generation over the item registry.
///
/// LAYER: Search
///   Input : raw user query string
///   Output: list of itype_id strings (candidates with known recipes)
///
/// This module MUST NOT:
///   - Resolve, validate, or render any item_id
///   - Call or enqueue the LLM
///   - Build LLM prompts
///   - Access recipe data beyond checking recipe existence

#include "npc_item_search.h"
#include "npc_recipe_cache.h"
#include "item_factory.h"
#include "itype.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace npc_item_search
{

namespace
{

auto normalize( const std::string &raw ) -> std::string
{
    std::string out;
    out.reserve( raw.size() );
    for( const char c : raw ) {
        out += static_cast<char>(
            std::isalnum( static_cast<unsigned char>( c ) )
                ? std::tolower( static_cast<unsigned char>( c ) )
                : '_' );
    }
    std::string collapsed;
    collapsed.reserve( out.size() );
    bool prev = false;
    for( const char c : out ) {
        if( c == '_' && prev ) continue;
        prev = ( c == '_' );
        collapsed += c;
    }
    const auto s = collapsed.find_first_not_of( '_' );
    const auto e = collapsed.find_last_not_of( '_' );
    return ( s == std::string::npos ) ? "" : collapsed.substr( s, e - s + 1 );
}

} // namespace

auto find_candidates( const std::string &raw_query, std::size_t limit )
    -> std::vector<std::string>
{
    const std::string norm = normalize( raw_query );
    std::vector<std::string> results;

    for( const itype *itp : item_controller->all() ) {
        // Only items with a known recipe are useful candidates
        if( npc_recipe_cache::get_recipes_by_result( itp->get_id() ).empty() ) {
            continue;
        }

        const std::string norm_id   = normalize( itp->get_id().str() );
        std::string loc_name        = itp->nname( 1 );
        std::transform( loc_name.begin(), loc_name.end(), loc_name.begin(), ::tolower );
        const std::string norm_name = normalize( loc_name );

        if( norm_id.find( norm )   != std::string::npos ||
            norm_name.find( norm ) != std::string::npos ||
            norm.find( norm_id )   != std::string::npos ) {
            results.push_back( itp->get_id().str() );
            if( results.size() >= limit ) {
                break;
            }
        }
    }

    return results;
}

} // namespace npc_item_search
