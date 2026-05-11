#pragma once
#include <string>
#include "type_id.h"

class npc;

namespace ai_actions {

void ai_log( const std::string &message );

/** 
 * Executes an AI-requested action on the engine side.
 * @param n The NPC performing the action.
 * @param action The action name (e.g., "pick_up", "wear", "wield").
 * @param target The target string (e.g., item ID).
 * @return A result string describing what happened (for the AI to see).
 */
auto execute_command( npc &n, const std::string &action, const std::string &target ) -> std::string;

// Individual actions
auto pick_up( npc &n, const std::string &item_id ) -> std::string;

/// Deterministic resupply: compute plan from weapon cache, scan nearby tiles,
/// pick up matching magazines and ammo until shortages are satisfied.
/// Does NOT use LLM logic. Pure engine-side execution.
auto execute_resupply( npc &n ) -> std::string;

/// Deterministic NPC crafting: validates the recipe_id against the static registry,
/// then starts the crafting activity via the C++ engine pipeline.
/// Never uses LLM logic. recipe_id must come from select_crafting_recipe() or a
/// pre-assigned registry lookup.
auto execute_craft( npc &n, const recipe_id &id ) -> std::string;

} // namespace ai_actions
