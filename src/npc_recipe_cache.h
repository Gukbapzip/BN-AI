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

/// Clears the recipe index.
void reset();

/// Returns all recipes that produce the given item ID.
/// O(1) lookup after build phase.
auto get_recipes_by_result( const itype_id &id ) -> const std::vector<const recipe *> &;

/// Returns a deterministic string representation of the recipe (Ingredients, Tools, Result).
/// Used by the display layer to render strict engine data. No LLM involvement.
auto get_recipe_deterministic_string( const itype_id &id ) -> std::string;

/// Holds the result of a deterministic item resolution attempt.
struct resolve_result {
    bool found = false;
    itype_id id;
    std::string display; ///< Final user-facing recipe string, or error message.
};

/// Deterministic item resolver: pure C++ only, no LLM.
/// Stages: string normalization -> alias lookup -> exact registry match -> recipe validation.
/// Returns resolve_result::found=false if no match exists. Never falls back to inference.
auto resolve_item_id( const std::string &raw_query ) -> resolve_result;

} // namespace npc_recipe_cache
