//  (C) Copyright Gennadiy Rozental 2005-2015.
//  Use, modification, and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org/libs/test for the library home page.
//
//  File        : $RCSfile$
//
//  Version     : $Revision$
//
//  Description : defines parser - public interface for CLA parsing and accessing
// ***************************************************************************

#ifndef BOOST_TEST_UTILS_RUNTIME_CLA_PARSER_HPP
#define BOOST_TEST_UTILS_RUNTIME_CLA_PARSER_HPP

// Boost.Runtime.Parameter
#include <boost/test/utils/runtime/argument.hpp>
#include <boost/test/utils/runtime/modifier.hpp>
#include <boost/test/utils/runtime/parameter.hpp>

#include <boost/test/utils/runtime/cla/argv_traverser.hpp>

// Boost.Test
#include <boost/test/utils/foreach.hpp>
#include <boost/test/utils/algorithm.hpp>
#include <boost/test/detail/throw_exception.hpp>

// STL
#include <unordered_set>

#include <boost/test/detail/suppress_warnings.hpp>

namespace boost {
namespace runtime {
namespace cla {

// ************************************************************************** //
// **************         runtime::cla::parameter_trie         ************** //
// ************************************************************************** //

namespace rt_cla_detail {

struct parameter_trie;
typedef shared_ptr<parameter_trie> parameter_trie_ptr;
typedef std::map<char,parameter_trie_ptr> trie_per_char;
typedef std::vector<std::reference_wrapper<parameter_cla_id const>> param_cla_id_list;

struct parameter_trie {
    parameter_trie() : m_has_final_candidate( false ) {}

    /// If subtrie corresponding to the char c exists returns it otherwise creates new
    parameter_trie_ptr  make_subtrie( char c )
    {
        trie_per_char::const_iterator it = m_subtrie.find( c );

        if( it == m_subtrie.end() )
            it = m_subtrie.insert( std::make_pair( c, parameter_trie_ptr( new parameter_trie ) ) ).first;

        return it->second;
    }

    /// Creates series of sub-tries per characters in a string
    parameter_trie_ptr  make_subtrie( cstring s )
    {
        parameter_trie_ptr res;

        BOOST_TEST_FOREACH( char, c, s )
            res = (res ? res->make_subtrie( c ) : make_subtrie( c ));

        return res;
    }

    /// Registers candidate parameter for this subtrie. If final, it needs to be unique
    void                add_candidate_id( parameter_cla_id const& param_id, basic_param_ptr param_candidate, bool final )
    {
        if( m_has_final_candidate || final && !m_id_candidates.empty() ) {
            BOOST_TEST_IMPL_THROW( conflicting_param() << "Parameter cla id " << param_id.m_full_name << " conflicts with the "
                                                       << "parameter cla id " << m_id_candidates.back().get().m_full_name );
        }

        m_has_final_candidate = final;
        m_id_candidates.push_back( param_id );

        if( m_id_candidates.size() == 1 )
            m_param_candidate = param_candidate;
        else
            m_param_candidate.reset();
    }

    /// Gets subtrie for specified char if present or nullptr otherwise
    parameter_trie_ptr  get_subtrie( char c ) const
    {
        trie_per_char::const_iterator it = m_subtrie.find( c );

        return it != m_subtrie.end() ? it->second : parameter_trie_ptr();
    }

    // Data members
    trie_per_char       m_subtrie;
    param_cla_id_list   m_id_candidates;
    basic_param_ptr     m_param_candidate;
    bool                m_has_final_candidate;
};

} // namespace rt_cla_detail

// ************************************************************************** //
// **************             runtime::cla::parser             ************** //
// ************************************************************************** //

class parser {
public:
    /// Initializes a parser and builds internal trie representation used for
    /// parsing based on the supplied parameters
    template<typename Modifiers=nfp::no_params_type>
    parser( parameters_store const& parameters, Modifiers const& m = nfp::no_params )
    {
        nfp::optionally_assign( m_end_of_param_indicator, m, end_of_params );
        nfp::optionally_assign( m_negation_prefix, m, negation_prefix );

        if( !std::all_of( m_end_of_param_indicator.begin(), 
                          m_end_of_param_indicator.end(), 
                          parameter_cla_id::valid_prefix_char ) )
            BOOST_TEST_IMPL_THROW( invalid_cla_id() << "End of parameters indicator can only consist of prefix characters." );

        if( !std::all_of( m_negation_prefix.begin(), 
                          m_negation_prefix.end(), 
                          parameter_cla_id::valid_name_char ) )
            BOOST_TEST_IMPL_THROW( invalid_cla_id() << "Negation prefix can only consist of prefix characters." );

        build_trie( parameters );
    }

    // input processing method
    int
    parse( int argc, char** argv, runtime::arguments_store& res )
    {
        // save program name for help message
        m_program_name = argv[0];
        cstring path_sep( "\\/" );

        auto it = unit_test::find_last_of( m_program_name.begin(), m_program_name.end(),
                                           path_sep.begin(), path_sep.end() );
        if( it != m_program_name.end() )
            m_program_name.trim_left( it + 1 );

        // Set up the traverser
        argv_traverser tr( argc, (char const**)argv );

        // Loop till we reach end of input
        while( !tr.eoi() ) {
            cstring curr_token = tr.current_token();

            cstring prefix;
            cstring name;
            cstring value_separator;
            bool    negative_form = false;

            // Perform format validations and split the argument into prefix, name and separator
            // False return value indicates end of params indicator is met
            if( !validate_token_format( curr_token, prefix, name, value_separator, negative_form ) ) {
                // get rid of "end of params" token
                tr.get_token();
                break;
            }

            // Locate trie corresponding to found prefix and skip it in the input
            trie_ptr curr_trie = m_param_trie[prefix];

            if( !curr_trie ) {
                BOOST_TEST_IMPL_THROW( format_error() << "Unrecognized parameter prefix in the argument " 
                                                      << curr_token );
            }
            tr.skip( prefix.size() );

            // Locate parameter based on a name and skip it in the input
            auto locate_res = locate_parameter( curr_trie, name, curr_token );
            parameter_cla_id const& found_id    = locate_res.first;
            basic_param_ptr         found_param = locate_res.second;

            if( negative_form ) {
                if( !found_id.m_negatable )
                    BOOST_TEST_IMPL_THROW( format_error() << "Parameter " << found_id.m_full_name 
                                                          << " is not negatable" );

                tr.skip( m_negation_prefix.size() );
            }

            tr.skip( name.size() );

            cstring value;

            // Skip validations if parameter has optional value and we are at the end of token
            if( !value_separator.is_empty() || !found_param->p_has_optional_value ) {
                // Validate and skip value separator in the input
                if( found_id.m_value_separator != value_separator ) {
                    BOOST_TEST_IMPL_THROW( format_error() << "Invalid separator for the parameter " << found_param->p_name 
                                                          << " in the argument " << curr_token );
                }

                tr.skip( value_separator.size() );

                // Deduce value source
                value = tr.get_token();

                if( value.is_empty() )
                    BOOST_TEST_IMPL_THROW( format_error() << "Missing an argument value for the parameter " << found_param->p_name 
                                                          << " in the argument " << curr_token );
            }

            // Validate against argument duplication
            if( res.has( found_param->p_name ) && !found_param->p_repeatable )
                BOOST_TEST_IMPL_THROW( duplicate_arg() << "Duplicate argument value for the parameter " << found_param->p_name 
                                                       << " in the argument " << curr_token );

            // Produce argument value
            found_param->produce_argument( value, negative_form, res );
        }

        // generate the remainder and return it's size
        return tr.remainder();
    }

    // help/usage
    void
    usage( std::ostream& ostr, parameters_store const& parameters, cstring param_name )
    {
        if( !param_name.is_empty() ) {
            parameters.get( param_name )->help( ostr, m_negation_prefix );
            return;
        }

        ostr << "Usage: " << m_program_name << " [Boost.Test arguments] ";
        if( !m_end_of_param_indicator.empty() )
            ostr << m_end_of_param_indicator << " [custom test module arguments]";

        ostr << "\n\nBoost.Test arguments correspond to parameters listed below. "
                "All parameters are optional. Use --help <parameter name> to display detail "
                "help for specific parameter. You can use specify parameter value either "
                "as a command line argument or as a value of corresponding environment "
                "variable. In case if argument for the same parameter is specified in both "
                "places, command line is taking precendence. Command line argument format "
                "supports parameter name guessing, so you can specify only any unambiguos "
                "prefix to identify a parameter.";
        if( !m_end_of_param_indicator.empty() )
            ostr << " All the arguments after the " << m_end_of_param_indicator << " are ignored by the Boost.Test.";

        ostr << "\n\nBoost.Test supports following parameters:\n";
        
        BOOST_TEST_FOREACH( parameters_store::storage_type::value_type const&, v, parameters.all() ) {
            basic_param_ptr param = v.second;

            param->usage( ostr, m_negation_prefix );
        }        
    }

private:
    typedef rt_cla_detail::parameter_trie_ptr   trie_ptr;
    typedef rt_cla_detail::trie_per_char        trie_per_char;
    typedef std::map<cstring,trie_ptr>          str_to_trie;

    void
    build_trie( parameters_store const& parameters )
    {
        // 10. Iterate over all parameters
        BOOST_TEST_FOREACH( parameters_store::storage_type::value_type const&, v, parameters.all() ) {
            basic_param_ptr param = v.second;

            // 20. Register all parameter's ids in trie.
            BOOST_TEST_FOREACH( parameter_cla_id const&, id, param->cla_ids() ) {
                // 30. This is the trie corresponding to the prefix.
                trie_ptr next_trie = m_param_trie[id.m_prefix];
                if( !next_trie )
                    next_trie = m_param_trie[id.m_prefix] = trie_ptr( new rt_cla_detail::parameter_trie );

                // 40. Build the trie, by following parameter id's full name
                //     and register this parameter as candidate on each level
                for( size_t index = 0; index < id.m_full_name.size(); ++index ) {
                    next_trie = next_trie->make_subtrie( id.m_full_name[index] );

                    next_trie->add_candidate_id( id, param, index == (id.m_full_name.size() - 1) );
                }
            }
        }
    }

    bool
    validate_token_format( cstring token, cstring& prefix, cstring& name, cstring& separator, bool& negative_form )
    {
        // Match prefix
        auto it = token.begin();
        while( it != token.end() && parameter_cla_id::valid_prefix_char( *it ) )
            ++it;

        prefix.assign( token.begin(), it );

        // Match name
        while( it != token.end() && parameter_cla_id::valid_name_char( *it ) )
            ++it;

        name.assign( prefix.end(), it );

        if( name.empty() ) {
            if( !prefix.is_empty() && prefix == m_end_of_param_indicator )
                return false;

            BOOST_TEST_IMPL_THROW( format_error() << "Invalid format for an actual argument " << token );
        }

        // Match value separator
        while( it != token.end() && parameter_cla_id::valid_separator_char(*it) )
            ++it;

        separator.assign( name.end(), it );

        // Match negation prefix
        negative_form = !m_negation_prefix.empty() && ( name.substr( 0, m_negation_prefix.size() ) == m_negation_prefix );
        if( negative_form )
            name.trim_left( m_negation_prefix.size() );

        return true;
    }

    typedef std::pair<parameter_cla_id const&, basic_param_ptr> locate_result;

    locate_result
    locate_parameter( trie_ptr curr_trie, cstring name, cstring token )
    {
        std::vector<trie_ptr> typo_candidates;
        std::vector<trie_ptr> next_typo_candidates;
        trie_ptr next_trie;

        BOOST_TEST_FOREACH( char, c, name ) {
            if( curr_trie ) {
                // locate next subtrie corresponding to the char
                next_trie = curr_trie->get_subtrie( c );

                if( next_trie ) 
                    curr_trie = next_trie;
                else {
                    // Initiate search for typo candicates. We will account for 'wrong char' typo
                    // 'missing char' typo and 'extra char' typo
                    BOOST_TEST_FOREACH( trie_per_char::value_type const&, typo_cand, curr_trie->m_subtrie ) {
                        // 'wrong char' typo
                        typo_candidates.push_back( typo_cand.second );

                        // 'missing char' typo
                        if( next_trie = typo_cand.second->get_subtrie( c ) )
                            typo_candidates.push_back( next_trie );
                    }
                    
                    // 'extra char' typo
                    typo_candidates.push_back( curr_trie );

                    curr_trie.reset();
                }
            }
            else {
                // go over existing typo candidates and see if they are still viable
                BOOST_TEST_FOREACH( trie_ptr, typo_cand, typo_candidates ) {
                    trie_ptr next_typo_cand = typo_cand->get_subtrie( c );

                    if( next_typo_cand )
                        next_typo_candidates.push_back( next_typo_cand );
                }

                next_typo_candidates.swap( typo_candidates );
                next_typo_candidates.clear();
            }
        }

        if( !curr_trie ) {
            std::vector<cstring> typo_candidate_names;
            std::unordered_set<parameter_cla_id const*> unique_typo_candidate;
            typo_candidate_names.reserve( typo_candidates.size() );
            unique_typo_candidate.reserve( typo_candidates.size() );

            BOOST_TEST_FOREACH( trie_ptr, trie_cand, typo_candidates ) {
                // avoid ambiguos candidate trie 
                if( trie_cand->m_id_candidates.size() > 1 )
                    continue;

                BOOST_TEST_FOREACH( parameter_cla_id const&, param_cand, trie_cand->m_id_candidates ) {
                    if( !unique_typo_candidate.insert( &param_cand ).second )
                        continue;

                    typo_candidate_names.push_back( param_cand.m_full_name );
                }
            }

            BOOST_TEST_IMPL_THROW( unrecognized_param( std::move(typo_candidate_names) ) << "An unrecognized parameter in the argument " << token );
        }

        if( curr_trie->m_id_candidates.size() > 1 )            
            BOOST_TEST_IMPL_THROW( ambiguous_param() << "An ambiguous parameter name in the argument " << token );

        return locate_result( curr_trie->m_id_candidates.back().get(), curr_trie->m_param_candidate );
    }

    // Data members
    cstring     m_program_name;
    std::string m_end_of_param_indicator;
    std::string m_negation_prefix;
    str_to_trie m_param_trie;
};

} // namespace cla
} // namespace runtime
} // namespace boost

#include <boost/test/detail/enable_warnings.hpp>

#endif // BOOST_TEST_UTILS_RUNTIME_CLA_PARSER_HPP
