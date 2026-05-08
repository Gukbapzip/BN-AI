#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/// Asynchronous local-LLM bridge.
///
/// Design goals:
/// - Never block the main thread: request execution happens on a worker thread.
/// - Low overhead: callers supply pre-summarized context strings.
/// - Safe action injection: AI output is parsed into explicit commands; game code chooses what to execute.
///
/// Threading model:
/// - `enqueue()` is thread-safe.
/// - Worker thread performs HTTP (or other transport) and parsing work.
/// - Completions are delivered by `pump_callbacks()` on the calling (main/UI) thread.
class ai_bridge
{
    public:
        using request_id = std::uint64_t;

        enum class error_code : std::uint8_t {
            none = 0,
            cancelled,
            rate_limited,
            transport_error,
            invalid_response,
            parse_error,
            timeout
        };

        struct error_info {
            error_code code = error_code::none;
            std::string message;
        };

        struct command {
            std::string action;
            std::string target;
            std::unordered_map<std::string, std::string> args;
        };

        struct parsed_response {
            /// Displayable dialogue text (already separated from action payload).
            std::string text;

            /// Zero or more action commands requested by the model.
            std::vector<command> commands;

            /// Optional model/debug metadata (opaque to the engine).
            std::unordered_map<std::string, std::string> meta;
        };

        struct parse_result {
            bool ok = false;
            parsed_response response;
            error_info error;
        };

        struct context {
            /// Summarized world state relevant to the conversation.
            std::string game_state_summary;

            /// Summarized nearby NPC list / proximity facts.
            std::string proximity_npcs_summary;

            /// Summarized player stats/status/inventory highlights.
            std::string player_status_summary;

            /// Recent dialogue turns, already condensed by caller.
            std::string conversation_summary;
        };

        struct request {
            /// A short system instruction / role prompt.
            std::string system;

            /// The user-visible prompt for this turn (e.g., what the NPC should respond to).
            std::string user;

            /// Pre-summarized game context.
            context ctx;

            /// Maximum end-to-end time allowed (queue + transport + model).
            std::chrono::milliseconds timeout = std::chrono::milliseconds{ 8'000 };

            /// Maximum tokens/length desired, if supported by backend (best-effort).
            int max_tokens = -1;

            /// Optional deterministic knob, if supported by backend (best-effort).
            float temperature = -1.0f;
        };

        struct completion {
            request_id id = 0;
            std::string raw;
            parse_result result;
        };

        using completion_cb_t = std::function<auto( const completion & ) -> void>;

        using transport_fn_t = std::function<auto( const request &, std::string &, error_info & ) -> bool>;

        struct options {
            /// URL for the local Mistral 22B server (OpenAI-compatible or custom).
            std::string endpoint;

            /// Model identifier, if required by backend.
            std::string model;

            /// Transport hook that performs the actual request. If not set, requests will fail with transport_error.
            transport_fn_t transport;

            /// Upper bound on queued in-flight requests.
            std::size_t max_in_flight = 2;

            /// If true, invalid/unknown commands are kept as text only.
            bool ignore_invalid_commands = true;
        };

        explicit ai_bridge( const options &opts );
        ~ai_bridge();
        ai_bridge( const ai_bridge & ) = delete;
        ai_bridge( ai_bridge && ) = delete;
        auto operator=( const ai_bridge & ) -> ai_bridge & = delete;
        auto operator=( ai_bridge && ) -> ai_bridge & = delete;

        /// Enqueue a request; completion callback will run when `pump_callbacks()` is called.
        auto enqueue( request req, completion_cb_t on_complete ) -> request_id;

        /// Best-effort cancellation of a pending/in-flight request.
        auto cancel( request_id id ) -> void;

        /// Pump queued completions and run callbacks on the calling thread.
        /// Returns the number of callbacks invoked.
        auto pump_callbacks( std::chrono::milliseconds budget = std::chrono::milliseconds{ 2 } ) -> std::size_t;

        /// Parse model output into (text + commands).
        ///
        /// Expected output formats (best-effort):
        /// - Plain text: treated as `{ "text": "<raw>" }`
        /// - JSON object: `{ "text": "...", "commands": [ { "action": "...", "target": "...", "args": {...} } ] }`
        /// - JSON command-only: `{ "action": "...", "target": "...", ... }` (wrapped into a single command)
        static auto parse_response( const std::string &raw, bool ignore_invalid_commands ) -> parse_result;

    private:
        class impl;
        std::unique_ptr<impl> pimpl;
};

