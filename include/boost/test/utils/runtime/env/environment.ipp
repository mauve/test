//  (C) Copyright Gennadiy Rozental 2005-2015.
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org/libs/test for the library home page.
//
//  File        : $RCSfile$
//
//  Version     : $Revision$
//
//  Description : implements model of program environment
// ***************************************************************************

#ifndef BOOST_TEST_UTILS_RUNTIME_ENV_ENVIRONMENT_IPP
#define BOOST_TEST_UTILS_RUNTIME_ENV_ENVIRONMENT_IPP

// Boost.Runtime.Parameter
#include <boost/test/utils/runtime/config.hpp>
#include <boost/test/utils/runtime/validation.hpp>

#include <boost/test/utils/runtime/env/variable.hpp>

// Boost.Test
#include <boost/test/utils/basic_cstring/compare.hpp>
#include <boost/test/utils/basic_cstring/io.hpp>

// STL
#include <map>
#include <list>

namespace boost {

namespace runtime {

namespace environment {

// ************************************************************************** //
// **************             runtime::environment             ************** //
// ************************************************************************** //

namespace rt_env_detail {

typedef std::map<cstring,rt_env_detail::variable_data> registry;
typedef std::list<std::string> keys;

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE registry& s_registry()    { static registry instance; return instance; }
BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE keys&     s_keys()        { static keys instance; return instance; }

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE variable_data&
new_var_record( cstring var_name )
{
    // save the name in list of keys
    s_keys().push_back( std::string() );
    std::string& key = s_keys().back();
    assign_op( key, var_name, 0 );

    s_keys().push_back( std::string(var_name.begin(), var_name.size()) );

    // create and return new record
    variable_data& new_var_data = s_registry()[key];

    new_var_data.m_var_name = key;

    return new_var_data;
}

//____________________________________________________________________________//

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE variable_data*
find_var_record( cstring var_name )
{
    auto it = s_registry().find( var_name );

    return it == s_registry().end() ? 0 : &(it->second);
}

//____________________________________________________________________________//

#ifdef BOOST_MSVC
#pragma warning(push)
#pragma warning(disable:4996) // getenv
#endif

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE cstring
sys_read_var( cstring var_name )
{
    using namespace std;
    return getenv( var_name.begin() );
}

#ifdef BOOST_MSVC
#pragma warning(pop)
#endif
//____________________________________________________________________________//

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE void
sys_write_var( cstring var_name, format_stream& var_value )
{
    ::boost::runtime::putenv_impl( var_name, cstring( var_value.str() ) );
}

//____________________________________________________________________________//

} // namespace rt_env_detail

BOOST_TEST_UTILS_RUNTIME_PARAM_INLINE variable_base
var( cstring var_name )
{
    rt_env_detail::variable_data* vd = rt_env_detail::find_var_record( var_name );

    BOOST_TEST_UTILS_RUNTIME_PARAM_VALIDATE_LOGIC( !!vd,
        "First access to the environment variable " << var_name << " should be typed" );

    return variable_base( *vd );
}

//____________________________________________________________________________//

} // namespace environment

} // namespace runtime

} // namespace boost

#endif // BOOST_TEST_UTILS_RUNTIME_ENV_ENVIRONMENT_IPP_062904GER
