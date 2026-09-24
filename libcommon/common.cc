/*
 * common.cc
 *
 *  Created on: 14.10.2023
 *      Author: Martin
 */
#include "common.h"
#include <cppdir.h>
#include <xml.h>
#include <stderr_exception.h>
#include <string_utils.h>

using namespace Tools;

std::string get_user_profile( const std::string & bf_base_folder )
{
    std::string content;
    const std::string profile_file = CppDir::concat_dir( bf_base_folder, R"(Mods\bf1942\Settings\Profile.con)");
    if( !XML::read_file( profile_file,
                         content ) ) {
        throw STDERR_EXCEPTION( format( "cannot open: '%s'", profile_file ) );
    }

    // the file looks like this:
    // rem ** MajorLeo **
    // game.setProfile "MajorLeo"

    auto sl = split_and_strip_simple( content, "\n" );

    for( auto line : sl ) {
        if( line.find( "game.setProfile" ) != std::string::npos ) {
            auto entries = split_simple( line );

            if( entries.size() < 2 ) {
                throw STDERR_EXCEPTION( format( "Cannot parse line: '%s'", line ) );
            }

            std::string entry = strip(entries[1], " \"\t" );
            return entry;
        }
    }

    return std::string();
}

