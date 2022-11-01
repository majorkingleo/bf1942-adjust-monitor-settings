
# it is required to install  to the prod directory
if( $main::uname ne 'WINNT') {
	if( $ENV{SRCDIR} || $ENV{EXTERNAL_CPPUTILS} ) {

	&Rulemore('install', 'HEADERFILES', '',
			"\@mkdir -p \$($standard::ENVROOTDIR)/include && cp -f *.h \$($standard::ENVROOTDIR)/include");

	&Rulemore('install', 'LIBS', '',
			"\@mkdir -p \$($standard::ENVROOTDIR)/lib && cp -f \$(LIBRARY) \$($standard::ENVROOTDIR)/lib");

	}

	
	if( !defined ( $ENV{TOOLSBOXDIR} ) && ! ( $ENV{EPREFIX} =~ /2010/ )) {

        &main'Macromore('CFLAGSAUX', 'c++11', "-std=gnu++11");

    }
}		

$eof;
