#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "character_id.h"

class JsonOut;
class JsonObject;
class npc;
struct tripoint;

// ── NATO-coded Squad identifiers ──────────────────────────────────────
enum class squad_id : int {
    SQ_NONE = 0,
    SQ_ALPHA,
    SQ_BRAVO,
    SQ_CHARLIE,
    SQ_DELTA,
    SQ_ECHO,
    SQ_FOXTROT,
    SQ_GOLF,
    SQ_HOTEL,
    SQ_INDIA,
    SQ_JULIETT,
    SQ_KILO,
    SQ_LIMA,
    SQ_MIKE,
    SQ_NOVEMBER,
    SQ_OSCAR,
    SQ_PAPA,
    SQ_QUEBEC,
    SQ_ROMEO,
    SQ_SIERRA,
    SQ_TANGO,
    SQ_UNIFORM,
    SQ_VICTOR,
    SQ_WHISKEY,
    SQ_XRAY,
    SQ_YANKEE,
    SQ_ZULU,
    SQ_END
};

// ── Tactical mode for squad behaviour ──────────────────────────────────
enum class tactical_mode : int {
    IDLE = 0,       // No combat, passive
    FOLLOW,         // Stay near leader, basic follow
    AGGRESSIVE,     // Push forward, increased engagement range
    DEFENSIVE,      // Hold near leader, prioritize cover
    STORM,          // Full assault, charge with melee
    END
};

// ── Formation types for spatial positioning ──────────────────────────
enum class squad_formation : int {
    LINE = 0,       // Horizontal line abreast
    WEDGE,          // Inverted-V behind leader
    COLUMN,         // Single file behind leader
    END
};

/// Convert a squad_id to its human-readable NATO code name.
auto squad_id_to_name( squad_id id ) -> std::string;

/// Convert a tactical_mode to its human-readable name.
auto tactical_mode_to_name( tactical_mode mode ) -> std::string;

/// Convert a squad_formation to its human-readable name.
auto squad_formation_to_name( squad_formation f ) -> std::string;

// ── Squad class (encapsulates a single squad) ─────────────────────────
class Squad
{
    public:
        explicit Squad( squad_id id );

        /// @return The squad's NATO identifier.
        auto get_id() const -> squad_id;
        /// @return The human-readable NATO code (e.g. "Alpha").
        auto get_name() const -> std::string;

        // ── Membership ────────────────────────────────────────────────
        /// Register an NPC by character_id as part of this squad.
        void add_member( character_id who );
        /// Remove an NPC from this squad by character_id.
        void remove_member( character_id who );
        /// @return true if the NPC is in this squad.
        auto has_member( character_id who ) const -> bool;
        /// Get all member IDs for iteration.
        auto get_member_ids() const -> const std::vector<character_id> &;

        /// @return Resolved npc pointer for member at index, or nullptr if unloaded.
        auto get_member_ptr( size_t idx ) const -> npc *;
        /// @return All currently-loaded member npc pointers (filters out unloaded NPCs).
        auto get_loaded_members() const -> std::vector<npc *>;

        // ── Leadership ─────────────────────────────────────────────────
        /// Designate an NPC by character_id as squad leader.
        void set_leader( character_id who );
        /// Clear the leader (member still stays in squad).
        void clear_leader();
        /// @return Current squad leader ID.
        auto get_leader_id() const -> character_id;
        /// @return Current squad leader npc pointer, or nullptr if unloaded.
        auto get_leader_ptr() const -> npc *;

        // ── Tactics & Formation ────────────────────────────────────────────
        void set_tactical_mode( tactical_mode mode );
        auto get_tactical_mode() const -> tactical_mode;

        void set_formation( squad_formation f );
        auto get_formation() const -> squad_formation;

        /// @return A relative tile offset for a squad member based on their
        ///         index in the squad and the squad's formation type.
        ///         The offset is relative to the leader's current position.
        ///         leader_facing is the direction the leader is looking
        ///         (e.g. from leader to target).
        auto get_formation_offset( character_id n_id,
                                   const tripoint &leader_facing_dir ) const -> tripoint;

        /// @return A compact status string for the LLM / debug overlay.
        auto get_status_summary() const -> std::string;

        /// Reaps dead NPCs from the member list.  Auto-fallback if leader dead.
        void cleanup_dead();

        // ── Persistence ───────────────────────────────────────────────
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );

    private:
        squad_id id_ = squad_id::SQ_NONE;
        std::vector<character_id> members_;
        character_id leader_;
        tactical_mode mode_ = tactical_mode::FOLLOW;
        squad_formation formation_ = squad_formation::WEDGE;
};

// ── SquadManager (centralized controller, singleton-like) ────────────
class SquadManager
{
    public:
        SquadManager() = default;
        ~SquadManager() = default;

        SquadManager( const SquadManager & ) = delete;
        auto operator=( const SquadManager & ) = delete;

        /// Get the global SquadManager instance.
        static auto get() -> SquadManager &;

        // ── Per-squad access ───────────────────────────────────────────
        /// Get or create a Squad for the given id.
        auto get_squad( squad_id id ) -> Squad &;
        /// Access all squads (for iteration)
        auto get_all_squads() -> std::map<squad_id, Squad> &;
        /// Access the NPC's current squad (if any), or nullptr.
        auto get_squad_for( character_id who ) -> Squad *;
        auto get_squad_for( character_id who ) const -> const Squad *;

        // ── Registration ───────────────────────────────────────────────
        /// Move an NPC into the given squad.  Removes from previous squad.
        /// Updates both Squad.members_ and npc_to_squad_map_ atomically.
        void assign_npc_to_squad( npc &who, squad_id id );
        /// Remove an NPC from any squad (sets to SQ_NONE).
        void remove_npc_from_squad( npc &who );
        /// Promote an NPC to squad leader within their squad.
        void promote_to_leader( npc &who );

        // ── Tactics propagation ────────────────────────────────────────
        /// Propagate a tactical mode to all members of a squad.
        void sync_squad_tactics( squad_id id, tactical_mode mode );

        /// Set the formation for a squad.
        void sync_squad_formation( squad_id id, squad_formation f );

        // ── Queries for LLM / debug ────────────────────────────────────
        /// Returns a single-line tactical summary of all squads.
        auto get_tactical_summary() const -> std::string;

        /// Remove dead members from all squads.
        void cleanup_all();

        /// @return true if the given id is a valid squad.
        static auto is_valid( squad_id id ) -> bool;

        // ── Reverse-lookup ─────────────────────────────────────────────
        auto get_npc_squad_id( character_id who ) const -> squad_id;

        // ── Persistence ───────────────────────────────────────────────
        void serialize( JsonOut &jsout ) const;
        /// Loads squad roster from a JsonObject member "squads".
        void deserialize( const JsonObject &jo );

    private:
        std::map<squad_id, Squad> squads_;
        /// Reverse-lookup: character_id -> squad_id (cheap, avoids scanning all squads).
        std::map<character_id, squad_id> npc_to_squad_map_;
};

// ── Free-standing helpers for NPC class integration ─────────────────
namespace squad_helpers
{

/// Return the SquadManager's squad_id for this NPC, or SQ_NONE.
auto get_npc_squad_id( const npc &who ) -> squad_id;

/// @return The display prefix, e.g. "[Alpha] " or "[Bravo*] " for squad leaders.
auto squad_display_prefix( const npc &who ) -> std::string;

/// @return true if the NPC is a valid squad leader (alive, ally, in a squad, flagged).
auto is_valid_squad_leader( const npc &who ) -> bool;

/// Handle leader death: return new leader candidate, or nullptr.
auto handle_leader_death( npc &dead_npc ) -> npc *;

/// Move an NPC towards their squad's formation position, if applicable.
void move_to_formation_position( npc &who );

} // namespace squad_helpers
