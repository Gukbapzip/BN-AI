#include "npc_recipe_cache.h"
#include "recipe_dictionary.h"
#include "recipe.h"
#include "itype.h"
#include "item_factory.h"
#include "string_formatter.h"
#include "debug.h"
#include <unordered_map>
#include <algorithm>

namespace npc_recipe_cache
{

namespace
{

bool s_built = false;
std::unordered_map<itype_id, std::vector<const recipe *>> s_result_index;

} // namespace

void init()
{
    if( s_built ) {
        return;
    }

    s_result_index.clear();

    // Index all standard recipes
    for( auto it = recipe_dict.begin(); it != recipe_dict.end(); ++it ) {
        const recipe &r = it->second;
        // Primary result
        s_result_index[r.result()].push_back( &r );
        // Byproducts
        if( r.has_byproducts() ) {
            for( const auto &bp : r.byproducts ) {
                s_result_index[bp.first].push_back( &r );
            }
        }
    }

    s_built = true;
    Lg( "[RECIPE_CACHE] Indexed %zu result types.", s_result_index.size() );
}

auto get_recipes_by_result( const itype_id &id ) -> const std::vector<const recipe *> &
{
    static const std::vector<const recipe *> empty_list;
    const auto it = s_result_index.find( id );
    if( it != s_result_index.end() ) {
        return it->second;
    }
    return empty_list;
}

auto get_recipe_knowledge_json( const itype_id &id ) -> std::string
{
    const auto &recipes = get_recipes_by_result( id );
    const recipe &uncraft = recipe_dictionary::get_uncraft( id );

    std::string k_json = "{";
    
    // 1. Crafting info
    if( !recipes.empty() ) {
        k_json += "\"craftable\": true, \"recipes\": [";
        bool first_r = true;
        for( const recipe *r : recipes ) {
            if( !first_r ) {
                k_json += ", ";
            }
            k_json += "{";
            
            // Components
            k_json += "\"ingredients\": [";
            bool first_comp = true;
            for( const auto &comps : r->simple_requirements().get_components() ) {
                if( !comps.empty() ) {
                    if( !first_comp ) {
                        k_json += ", ";
                    }
                    k_json += "\"" + comps[0].type.str() + "\"";
                    first_comp = false;
                }
            }
            k_json += "], ";

            // Tools
            k_json += "\"tools\": [";
            bool first_tool = true;
            for( const auto &tools : r->simple_requirements().get_tools() ) {
                if( !tools.empty() ) {
                    if( !first_tool ) {
                        k_json += ", ";
                    }
                    k_json += "\"" + tools[0].type.str() + "\"";
                    first_tool = false;
                }
            }
            k_json += "]";

            k_json += "}";
            first_r = false;
        }
        k_json += "]";
    } else {
        k_json += "\"craftable\": false";
    }

    // 2. Uncraft info
    if( !uncraft.result().is_null() ) {
        k_json += ", \"disassembles_into\": [";
        bool first_comp = true;
        for( const auto &comps : uncraft.simple_requirements().get_components() ) {
            if( !comps.empty() ) {
                if( !first_comp ) {
                    k_json += ", ";
                }
                k_json += "\"" + comps[0].type.str() + "\"";
                first_comp = false;
            }
        }
        k_json += "]";
    }

    k_json += "}";
    return k_json == "{}" ? "null" : k_json;
}

} // namespace npc_recipe_cache
