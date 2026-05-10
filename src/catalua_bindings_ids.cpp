#include "catalua_bindings.h"

#include "catalua_bindings_utils.h"
#include "catalua_luna.h"
#include "catalua_luna_doc.h"

#include "ammo.h"
#include "activity_type.h"
#include "bionics.h"
#include "bodypart.h"
#include "disease.h"
#include "effect.h"
#include "faction.h"
#include "field_type.h"
#include "flag.h"
#include "flag_trait.h"
#include "itype.h"
#include "json.h"
#include "magic.h"
#include "mapdata.h"
#include "martialarts.h"
#include "material.h"
#include "mission.h"
#include "monfaction.h"
#include "monstergenerator.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "omdata.h"
#include "recipe.h"
#include "skill.h"
#include "trap.h"
#include "type_id.h"
#include "ammo_effect.h"
#include "mod_manager.h"
#include "emit.h"
#include "fault.h"
#include "requirements.h"
#include "vitamin.h"

template<typename T, bool do_int_id>
void reg_id( sol::state &lua )
{
    using SID = string_id<T>;
    using IID = int_id<T>;
#define UT_CLASS SID
    {
        // Register string_id class under given name
        sol::usertype<SID> ut;
        if constexpr( do_int_id ) {
            ut = luna::new_usertype<SID>( lua, luna::no_bases, luna::constructors <
                                          SID(),
                                          SID( const SID & ),
                                          SID( const IID & ),
                                          SID( std::string )
                                          > ()
                                        );
        } else {
            ut = luna::new_usertype<SID>( lua, luna::no_bases, luna::constructors <
                                          SID(),
                                          SID( const SID & ),
                                          SID( std::string )
                                          > ()
                                        );
        }

        luna::set_fx( ut, "obj", []( const SID & sid ) -> const T* { return &sid.obj(); } );
        if constexpr( do_int_id ) {
            luna::set_fx( ut, "int_id", &SID::id );
            luna::set_fx( ut, "implements_int_id", []() { return true; } );
        } else {
            luna::set_fx( ut, "implements_int_id", []() { return false; } );
        }
        SET_FX( is_null );
        SET_FX( is_valid );
        luna::set_fx( ut, "str", &SID::c_str );
        SET_FX( NULL_ID );
        luna::set_fx( ut, sol::meta_function::to_string, []( const SID & id ) -> std::string { return string_format( "%s[%s]", luna::detail::luna_traits<SID>::name, id.c_str() ); } );

        // (De-)Serialization
        luna::set_fx( ut, "serialize", []( const SID & ut, JsonOut & jsout ) { jsout.write( ut.str() ); } );
        luna::set_fx( ut, "deserialize", []( SID & ut, JsonIn & jsin ) { ut = SID( jsin.get_string() ); } );
    }
#undef UT_CLASS

#define UT_CLASS IID
    if constexpr( do_int_id ) {
        // Register int_id class under given name
        sol::usertype<IID> ut = luna::new_usertype<IID>( lua, luna::no_bases, luna::constructors <
                                IID(),
                                IID( const IID & ),
                                IID( const SID & )
                                > () );

        luna::set_fx( ut, "obj", []( const IID & iid ) -> const T* { return &iid.obj(); } );
        luna::set_fx( ut, "str_id", &IID::id );
        SET_FX( is_valid );
        luna::set_fx( ut, sol::meta_function::to_string, []( const IID & id ) -> std::string { return string_format( "%s[%d][%s]", luna::detail::luna_traits<IID>::name, id.to_i(), id.is_valid() ? id.id().c_str() : "<invalid>" ); } );
    }
#undef UT_CLASS
}

void cata::detail::reg_game_ids( sol::state &lua )
{
    // Part 1
    reg_id<ammunition_type, false>( lua );
    reg_id<activity_type, false>( lua );
    reg_id<bionic_data, false>( lua );
    reg_id<body_part_type, true>( lua );
    reg_id<disease_type, false>( lua );
    reg_id<effect_type, false>( lua );
    reg_id<faction, false>( lua );
    reg_id<field_type, true>( lua );
    reg_id<furn_t, true>( lua );
    reg_id<itype, false>( lua );
    reg_id<json_flag, false>( lua );
    reg_id<json_trait_flag, false>( lua );
    reg_id<ma_buff, false>( lua );
    reg_id<mission_type, false>( lua );
    reg_id<ma_technique, false>( lua );
    reg_id<martialart, false>( lua );
    reg_id<material_type, false>( lua );
    reg_id<monfaction, true>( lua );
    reg_id<morale_type_data, false>( lua );
    reg_id<mtype, false>( lua );
    
    // Call the second part in another translation unit
    reg_game_ids_ext( lua );
}

// reg_types moved to catalua_bindings_ids_2.cpp
