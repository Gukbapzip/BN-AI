#pragma once

#include <string>
#include <vector>

/// Item Search Service — substring candidate generation over the item registry.
///
/// STRICT RESPONSIBILITY:
///   - Substring / prefix matching over item names and IDs
///   - Filters candidates to items that have known crafting recipes
///
/// This module MUST NOT perform item_id resolution, validation, or rendering.
/// Those are the exclusive responsibility of npc_recipe_cache.
/// This module MUST NOT build prompts for or reference the LLM in any way.
namespace npc_item_search
{

/// Returns up to `limit` itype_id strings whose name or ID contains the query.
/// Only items with at least one known recipe are included.
auto find_candidates( const std::string &raw_query, std::size_t limit = 50 )
    -> std::vector<std::string>;

} // namespace npc_item_search
