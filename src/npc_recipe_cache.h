#pragma once

#include <vector>
#include <string>
#include "type_id.h"

class recipe;

/// Cache for fast reverse lookup of recipes by their result item.
namespace npc_recipe_cache
{

/// Builds the recipe index. Should be called after all recipes are loaded.
void init();

/// Returns all recipes that produce the given item ID.
/// O(1) lookup after build phase.
auto get_recipes_by_result( const itype_id &id ) -> const std::vector<const recipe *> &;

/// Formats a recipe into a JSON-friendly string for LLM context.
auto get_recipe_knowledge_json( const itype_id &id ) -> std::string;

} // namespace npc_recipe_cache
