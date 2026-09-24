#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#include <winuser.h>
#include <cppdir.h>
#include <stderr_exception.h>
#include <xml.h>
#include <string_utils.h>
#include <set>
#include <errno.h>
#include <aclapi.h>
#include <tchar.h>
#include <memory>

using namespace Tools;

struct ResolutionInfo
{
	unsigned width;
	unsigned height;

	auto to_tuple() const {
		return std::make_tuple( width, height );
	}

	bool operator<( const ResolutionInfo & other ) const {
		return to_tuple() < other.to_tuple();
	}

	bool operator==( const ResolutionInfo & other ) const {
		return to_tuple() == other.to_tuple();
	}
};

int main()
{
    try {

    	std::set<ResolutionInfo> sres;

    	DEVMODEA mode = {};
        for( DWORD iModeNum = 0; EnumDisplaySettingsA( nullptr, iModeNum, &mode ) != 0; ++iModeNum ) {
           sres.insert( ResolutionInfo{ mode.dmPelsWidth, mode.dmPelsHeight } );
        }

        std::list<ResolutionInfo> lres( sres.begin(), sres.end() );
        lres.sort();

        for( const ResolutionInfo & monitor_data : lres ) {
        	std::cout << Tools::format( "%dx%d", monitor_data.width, monitor_data.height ) << std::endl;
        }

    } catch( const std::exception & error ) {
        std::cerr << error.what() << std::endl;       
    }

    return 0;
}
