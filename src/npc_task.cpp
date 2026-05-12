#include "npc_task.h"
#include <algorithm>
#include <ranges>

namespace npc_task
{

std::vector<craft_task> task_manager::s_tasks;
int task_manager::s_next_id = 0;

auto task_manager::add_task( const craft_task &task ) -> int
{
    auto new_task = task;
    new_task.task_id = s_next_id++;
    s_tasks.push_back( new_task );
    return new_task.task_id;
}

auto task_manager::get_next_task( const character_id &actor ) -> std::optional<craft_task>
{
    const auto it = std::ranges::find_if( s_tasks, [&]( const craft_task & t ) {
        return t.actor == actor && t.status == task_status::PENDING;
    } );

    if( it != s_tasks.end() ) {
        return *it;
    }
    return std::nullopt;
}

auto task_manager::update_status( const int task_id, const task_status status ) -> void
{
    const auto it = std::ranges::find_if( s_tasks, [&]( const craft_task & t ) {
        return t.task_id == task_id;
    } );

    if( it != s_tasks.end() ) {
        it->status = status;
    }
}

auto task_manager::mark_completed( const character_id &actor, const recipe_id &recipe ) -> void
{
    const auto it = std::ranges::find_if( s_tasks, [&]( const craft_task & t ) {
        return t.actor == actor && t.recipe == recipe && t.status == task_status::IN_PROGRESS;
    } );

    if( it != s_tasks.end() ) {
        it->status = task_status::COMPLETED;
    }
}

auto task_manager::mark_cancelled( const character_id &actor ) -> void
{
    const auto it = std::ranges::find_if( s_tasks, [&]( const craft_task & t ) {
        return t.actor == actor && t.status == task_status::IN_PROGRESS;
    } );

    if( it != s_tasks.end() ) {
        it->status = task_status::CANCELLED;
    }
}

auto task_manager::reset() -> void
{
    s_tasks.clear();
    s_next_id = 0;
}

void craft_task::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "recipe", recipe );
    jsout.member( "batch_size", batch_size );
    jsout.member( "actor", actor );
    jsout.member( "priority", priority );
    jsout.member( "loc", loc );
    jsout.member( "status", static_cast<int>( status ) );
    jsout.member( "task_id", task_id );
    jsout.end_object();
}

void craft_task::deserialize( JsonIn &jsin )
{
    JsonObject jsobj = jsin.get_object();
    jsobj.read( "recipe", recipe );
    jsobj.read( "batch_size", batch_size );
    jsobj.read( "actor", actor );
    jsobj.read( "priority", priority );
    jsobj.read( "loc", loc );
    int status_val = 0;
    jsobj.read( "status", status_val );
    status = static_cast<task_status>( status_val );
    jsobj.read( "task_id", task_id );
}

void task_manager::serialize( JsonOut &jsout )
{
    jsout.start_object();
    jsout.member( "tasks", s_tasks );
    jsout.member( "next_id", s_next_id );
    jsout.end_object();
}

void task_manager::deserialize( const JsonObject &jsobj )
{
    jsobj.read( "tasks", s_tasks );
    jsobj.read( "next_id", s_next_id );
}

} // namespace npc_task
