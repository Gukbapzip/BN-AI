/// npc_craft_parser.cpp
/// Deterministic Craft Display — recipe rendering gate.
///
/// LAYER: Display
///   Input : canonical itype_id (engine-selected, never natural-language)
///   Output: rendered recipe string, or rejection
///
/// This module MUST NOT:
///   - Accept natural-language text
///   - Scan the item registry or perform fuzzy search
///   - Call or enqueue the LLM
///   - Apply any inference, guess, or fallback logic
///
/// All recipe rendering lives in npc_recipe_cache.

#include "npc_craft_parser.h"
#include "npc_recipe_cache.h"
#include "translations.h"
#include "type_id.h"

namespace npc_craft_parser
{

auto resolve_and_display( const itype_id &id ) -> result
{
    if( id.is_null() ) {
        return result{ false, _( "No recipe selected." ) };
    }

    const auto &recipes = npc_recipe_cache::get_recipes_by_result( id );
    if( recipes.empty() ) {
        return result{ false, _( "No known recipe for this item." ) };
    }

    // Delegate rendering to the authoritative cache — no logic here.
    return result{ true, npc_recipe_cache::get_recipe_deterministic_string( id ) };
}

} // namespace npc_craft_parser
