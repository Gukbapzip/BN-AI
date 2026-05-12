#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "type_id.h"
#include "character_id.h"
#include "point.h"
#include "json.h"

/// Task assignment system for NPCs and Player characters.
///
/// This system decouples job selection (UI/AI) from job execution (Activity Engine).
/// All crafting jobs must be registered as a `craft_task` before execution.

namespace npc_task
{

enum class task_status : int {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    CANCELLED
};

/// Deterministic crafting job definition.
struct craft_task {
    recipe_id recipe;
    int batch_size = 1;
    character_id actor; ///< Assigned character ID (Player or NPC)
    int priority = 0;
    tripoint loc = tripoint_zero;
    task_status status = task_status::PENDING;

    /// Unique ID for this specific task instance
    int task_id = -1;

    void serialize( JsonOut &jsout ) const;
    void deserialize( JsonIn &jsin );
};

/// Global manager for character tasks.
class task_manager
{
    public:
        /// Register a new crafting task.
        static auto add_task( const craft_task &task ) -> int;

        /// Retrieves the next available task for a specific actor.
        static auto get_next_task( const character_id &actor ) -> std::optional<craft_task>;

        /// Marks a task as in-progress.
        static auto update_status( int task_id, task_status status ) -> void;

        /// Mark the first IN_PROGRESS task for this actor/recipe as COMPLETED.
        static auto mark_completed( const character_id &actor, const recipe_id &recipe ) -> void;

        /// Mark the first IN_PROGRESS task for this actor as CANCELLED.
        static auto mark_cancelled( const character_id &actor ) -> void;

        /// Clears all tasks.
        static auto reset() -> void;

        static void serialize( JsonOut &jsout );
        static void deserialize( const JsonObject &jsobj );

    private:
        static std::vector<craft_task> s_tasks;
        static int s_next_id;
};

} // namespace npc_task
