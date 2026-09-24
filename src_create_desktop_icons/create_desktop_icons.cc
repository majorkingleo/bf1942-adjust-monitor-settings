/*
 * create_desktop_icons.cc
 *
 *  Created on: 14.10.2023
 *      Author: Martin
 */
#include "common.h"
#include "ShortcutProvider.h"
#include <cppdir.h>
#include <stderr_exception.h>
#include <windows.h>
#include <shlobj.h>

using namespace Tools;

static std::string get_desktop_directory_ansi()
{
    static char path[MAX_PATH+1];
    if (SHGetSpecialFolderPathA(HWND_DESKTOP, path, CSIDL_DESKTOP, FALSE))
        return path;

    throw STDERR_EXCEPTION( "cannot find desktop path" );
}

static void create_icon( const std::string & bf_bf1942_exe,
						 const std::string & start_options,
						 const std::string & link_name,
						 const std::string & description,
						 const std::string & base_folder,
						 const std::string & icon )
{
	static std::string desktop_directory = get_desktop_directory_ansi();
	ShortcutProvider sp;

	sp.Create( const_cast<char*>(bf_bf1942_exe.c_str()),
			   const_cast<char*>(start_options.c_str()),
			   const_cast<char*>(CppDir::concat_dir(desktop_directory,link_name).c_str()),
			   const_cast<char*>(description.c_str()),
			   SW_SHOWMAXIMIZED,
			   const_cast<char*>(base_folder.c_str()),
			   const_cast<char*>( CppDir::concat_dir(base_folder, "icons", icon ).c_str()),
			   0 );
}

int main()
{
	try {
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		std::string start = CppDir::pwd();

		CppDir::File f_bf1942_exe( CppDir::concat_dir( start, "bf1942.exe" ) );

		if( !f_bf1942_exe.is_valid() ) {
			f_bf1942_exe = CppDir::File( CppDir::concat_dir( start, "..", "bf1942.exe" ) );
		}

		if( !f_bf1942_exe.is_valid() ) {
			throw STDERR_EXCEPTION( "Cannot find user setting location.\n"
					"Please run this program in battlefield folder or profile settings folder");
		}

		std::string bf_base_folder = f_bf1942_exe.get_path();


		create_icon( f_bf1942_exe.get_full_path(), "+game DesertCombat", "BF DC.lnk", "BF 1942 Desert Combat", bf_base_folder, "Icon-DC.ico" );
		create_icon( f_bf1942_exe.get_full_path(), "+game DC_Extended", "BF DCX.lnk", "BF 1942 Desert Combat Extended", bf_base_folder, "Icon-DCX.ico" );
		create_icon( f_bf1942_exe.get_full_path(), "+game DC_Extended", "BF DCX.lnk", "BF 1942 Desert Combat Extended", bf_base_folder, "Icon-DCX.ico" );
		create_icon( f_bf1942_exe.get_full_path(), "+game FH", "BF FH.lnk", "BF 1942 Forgotten Hope", bf_base_folder, "Icon-FH.ico" );
		create_icon( f_bf1942_exe.get_full_path(), "+game FHTCTFMOD", "BF FHCTF.lnk", "BF 1942 Forgotten Hope CFT", bf_base_folder, "Icon-FHCTF.ico" );
		create_icon( f_bf1942_exe.get_full_path(), "", "BF1942.lnk", "BF 1942", bf_base_folder, "Icon-BF.ico" );



	} catch( const std::exception & error ) {
		std::cerr << error.what() << std::endl;
	}

	//system("PAUSE");
}



