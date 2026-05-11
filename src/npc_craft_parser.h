#pragma once

#include <string>
#include "type_id.h"

/// Deterministic Craft Display — recipe information rendering gate.
///
/// STRICT RESPONSIBILITY:
///   - Accepts a recipe_id resolved from the engine's static registry
///   - Renders the final user-facing recipe string via npc_recipe_cache
///
/// This module MUST NOT:
///   - Accept natural-language queries
///   - Scan the item registry for fuzzy matches
///   - Call or reference the LLM in any way
///   - Apply any fallback reasoning or inference
namespace npc_craft_parser
{

/// Result of a deterministic recipe display attempt.
struct result {
    bool        found   = false;
    std::string display; ///< Final user-facing recipe string, or error message.
};

/// Resolve and render a recipe by its canonical itype_id.
/// Delegates entirely to npc_recipe_cache::get_recipe_deterministic_string().
/// Returns found=false if the id has no recipe in the static registry.
/// No LLM, no fuzzy matching, no inference.
auto resolve_and_display( const itype_id &id ) -> result;

} // namespace npc_craft_parser
