#pragma once
#include <optional>
#include <string>

namespace npc_cmd {

/// A deterministically-parsed command, ready to send straight to the executor.
struct parsed_command {
    std::string action;
    std::string target; ///< May be empty for actions that take no target.
};

/// Attempt to parse @p raw_text as a direct gameplay command.
/// Returns nullopt when the text reads like natural conversation instead.
auto try_parse( const std::string &raw_text ) -> std::optional<parsed_command>;

} // namespace npc_cmd
