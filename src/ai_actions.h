#pragma once
#include <string>

class npc;

namespace ai_actions {

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

} // namespace ai_actions
