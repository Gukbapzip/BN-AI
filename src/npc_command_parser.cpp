#include "npc_command_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace npc_cmd {

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {

auto to_lower( std::string s ) -> std::string
{
    std::ranges::transform( s, s.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    return s;
}

/// Strip leading/trailing whitespace in-place.
auto trim( std::string_view sv ) -> std::string_view
{
    while( !sv.empty() && std::isspace( static_cast<unsigned char>( sv.front() ) ) ) {
        sv.remove_prefix( 1 );
    }
    while( !sv.empty() && std::isspace( static_cast<unsigned char>( sv.back() ) ) ) {
        sv.remove_suffix( 1 );
    }
    return sv;
}

/// Returns true when the string starts with the given prefix (case-insensitive,
/// already-lowercased prefix expected).
auto starts_with_lower( const std::string &lower_text, std::string_view prefix ) -> bool
{
    return lower_text.size() >= prefix.size() &&
           lower_text.compare( 0, prefix.size(), prefix ) == 0;
}

// ─── known item categories (must match data/json/item_category.json ids) ────

constexpr std::array<std::string_view, 43> ITEM_CATEGORIES = { {
    "guns", "magazines", "ammo", "weapons", "tools", "tools_entry",
    "tools_workshop", "tools_cooking", "tools_chemistry", "tools_farming",
    "deployables", "electronics", "clothing", "food", "cooking_ingredients",
    "drugs", "books", "spellbooks", "mods", "mutagen", "bionics", "veh_parts",
    "other", "fuel", "seeds", "chems", "battery", "spare_parts", "scrap_metal",
    "scrap_electronics", "scrap_fabric", "scrap_wood", "scrap_plastic",
    "scrap_ceramics", "scrap_glass", "valuables", "rocks", "soil", "container",
    "artifacts", "maps",
    // plural aliases  ↓
    "containers", "weapons",
} };

/// Maps user-facing singular/plural category words → canonical category id.
const std::unordered_map<std::string, std::string> CATEGORY_ALIASES = {
    // plurals → singular canonical id
    { "book",        "books"         },
    { "novel",       "books"         },
    { "novels",      "books"         },
    { "magazine",    "magazines"     },
    { "gun",         "guns"          },
    { "weapon",      "weapons"       },
    { "ammos",       "ammo"          },
    { "ammunition",  "ammo"          },
    { "tool",        "tools"         },
    { "food",        "food"          },
    { "drug",        "drugs"         },
    { "meds",        "drugs"         },
    { "medicine",    "drugs"         },
    { "clothing",    "clothing"      },
    { "clothes",     "clothing"      },
    { "cloth",       "clothing"      },
    { "containers",  "container"     },
    { "container",   "container"     },
    { "bag",         "container"     },
    { "bags",        "container"     },
    { "electronics", "electronics"   },
    { "electronic",  "electronics"   },
    { "scrap",       "scrap_metal"   },
    { "maps",        "maps"          },
    { "map",         "maps"          },
    { "seeds",       "seeds"         },
    { "seed",        "seeds"         },
    { "fuel",        "fuel"          },
    { "valuables",   "valuables"     },
    { "valuable",    "valuables"     },
    { "loot",        "valuables"     },
};

/// Attempt to resolve a word to a canonical category id.
/// Returns empty string on failure.
auto resolve_category( const std::string &word ) -> std::string
{
    // Direct canonical match
    for( const auto &cat : ITEM_CATEGORIES ) {
        if( word == cat ) {
            return std::string( cat );
        }
    }
    // Alias lookup
    const auto it = CATEGORY_ALIASES.find( word );
    if( it != CATEGORY_ALIASES.end() ) {
        return it->second;
    }
    return {};
}

// ─── pick_up pattern matching ────────────────────────────────────────────────

/// Verb stems that signal a pickup intent.
constexpr std::array<std::string_view, 8> PICKUP_VERBS = { {
    "pick up", "pickup", "pick", "grab", "take", "collect", "gather", "get",
} };

/// Filler words we strip before trying to resolve the target noun.
constexpr std::array<std::string_view, 10> FILLER_WORDS = { {
    "nearby", "near", "all", "every", "the", "some", "any",
    "those", "these", "surrounding",
} };

auto strip_filler( std::string text ) -> std::string
{
    bool changed = true;
    while( changed ) {
        changed = false;
        for( const auto &filler : FILLER_WORDS ) {
            // strip at start
            if( starts_with_lower( text, filler ) ) {
                text = std::string( trim( text.substr( filler.size() ) ) );
                changed = true;
            }
            // strip at end
            if( text.size() >= filler.size() &&
                text.compare( text.size() - filler.size(), filler.size(), filler ) == 0 ) {
                text = std::string( trim( text.substr( 0, text.size() - filler.size() ) ) );
                changed = true;
            }
        }
    }
    return text;
}

auto try_parse_pickup( const std::string &lower ) -> std::optional<parsed_command>
{
    for( const auto &verb : PICKUP_VERBS ) {
        if( !starts_with_lower( lower, verb ) ) {
            continue;
        }
        auto rest = std::string( trim( lower.substr( verb.size() ) ) );
        rest = strip_filler( rest );
        if( rest.empty() ) {
            continue; // no target noun
        }
        // Try to map to a known category first
        const auto cat = resolve_category( rest );
        if( !cat.empty() ) {
            return parsed_command{ "pick_up", cat };
        }
        // Fall back: treat the remaining text as a literal item id.
        // Only accept it when it looks like an id (no spaces, no capital letters).
        const bool looks_like_id =
            rest.find( ' ' ) == std::string::npos &&
            std::ranges::none_of( rest, []( unsigned char c ) {
                return std::isupper( c );
            } );
        if( looks_like_id ) {
            return parsed_command{ "pick_up", rest };
        }
    }
    return std::nullopt;
}

// ─── follow / guard patterns ────────────────────────────────────────────────

auto try_parse_movement( const std::string &lower ) -> std::optional<parsed_command>
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> PATTERNS = { {
        { "follow me",      "follow"      },
        { "follow",         "follow"      },
        { "guard here",     "guard"       },
        { "guard this",     "guard"       },
        { "guard",          "guard"       },
        { "stop guarding",  "stop_guard"  },
    } };
    for( const auto &[pattern, action] : PATTERNS ) {
        if( starts_with_lower( lower, pattern ) ) {
            return parsed_command{ std::string( action ), {} };
        }
    }
    return std::nullopt;
}

// ─── combat stance patterns ─────────────────────────────────────────────────

auto try_parse_combat( const std::string &lower ) -> std::optional<parsed_command>
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 8> PATTERNS = { {
        { "engage all",      "engage_all"   },
        { "engage everyone", "engage_all"   },
        { "engage none",     "engage_none"  },
        { "stop engaging",   "engage_none"  },
        { "engage close",    "engage_close" },
        { "aim precise",     "aim_precise"  },
        { "aim spray",       "aim_spray"    },
        { "use guns",        "use_guns"     },
    } };
    for( const auto &[pattern, action] : PATTERNS ) {
        if( starts_with_lower( lower, pattern ) ) {
            return parsed_command{ std::string( action ), {} };
        }
    }
    return std::nullopt;
}

} // anonymous namespace

// ─── resupply pattern ───────────────────────────────────────────────────────────

namespace {

auto try_parse_resupply( const std::string &lower ) -> std::optional<parsed_command>
{
    // All phrases that unambiguously mean "go get your ammo/magazines".
    constexpr std::array<std::string_view, 8> PATTERNS = { {
        "resupply yourself",
        "resupply ammo",
        "resupply magazines",
        "resupply weapon",
        "resupply",
        "restock",
        "get ammo",
        "reload yourself",
    } };
    for( const auto &pat : PATTERNS ) {
        if( starts_with_lower( lower, pat ) ) {
            return parsed_command{ "resupply", {} };
        }
    }
    return std::nullopt;
}

} // anonymous namespace

// ─── public API ─────────────────────────────────────────────────────────────

auto try_parse( const std::string &raw_text ) -> std::optional<parsed_command>
{
    const auto lower = to_lower( raw_text );
    const auto trimmed = std::string( trim( lower ) );

    if( trimmed.empty() ) {
        return std::nullopt;
    }

    // Heuristic: if the text contains a question mark it is almost certainly
    // conversational, not a command.
    if( trimmed.find( '?' ) != std::string::npos ) {
        return std::nullopt;
    }

    if( auto cmd = try_parse_pickup( trimmed ) ) {
        return cmd;
    }
    if( auto cmd = try_parse_resupply( trimmed ) ) {
        return cmd;
    }
    if( auto cmd = try_parse_movement( trimmed ) ) {
        return cmd;
    }
    if( auto cmd = try_parse_combat( trimmed ) ) {
        return cmd;
    }

    return std::nullopt;
}

} // namespace npc_cmd
