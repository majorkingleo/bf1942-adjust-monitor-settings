bin_PROGRAMS=adjust_monitor_settings \
	create_desktop_icons \
	list_monitor_resolutions

adjust_monitor_settings_SOURCES=\
		tools_config.h\
		adjust_monitor_settings.cc \
		common.cc \
		common.h
		
list_monitor_resolutions_SOURCES=\
		tools_config.h\
		list_monitor_resolutions.cc
				
create_desktop_icons_SOURCES=\
		tools_config.h \
		create_desktop_icons.cc \
		common.cc \
		common.h \
		ShortcutProvider.h \
		ShortcutProvider.cc
			  
AM_CPPFLAGS = \
	-I$(top_srcdir)/cpputils/cpputilsshared  \
	-I$(top_srcdir)/cpputils/cpputilsshared/cpputilsformat \
	-I$(top_srcdir)/src  \
	-DNOWAMAS \
	-std=c++20

noinst_LIBRARIES=\
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

cpputils_cpputilsshared_libcpputilsshared_a_SOURCES= \
		cpputils/cpputilsshared/backtrace.cc \
		cpputils/cpputilsshared/backtrace.h \
		cpputils/cpputilsshared/cppdir.cc \
		cpputils/cpputilsshared/cppdir.h \
		cpputils/cpputilsshared/leoini.cc \
		cpputils/cpputilsshared/leoini.h \
		cpputils/cpputilsshared/string_utils.cc \
		cpputils/cpputilsshared/string_utils.h \
		cpputils/cpputilsshared/xml.cc \
		cpputils/cpputilsshared/xml.h \
		cpputils/cpputilsshared/cpp_util.h \
		cpputils/cpputilsshared/cpp_util_min_max.h \
		cpputils/cpputilsshared/cpp_utils.h \
		cpputils/cpputilsshared/cpputils.h \
		cpputils/cpputilsshared/ref.h \
		cpputils/cpputilsshared/jvector.h \
		cpputils/cpputilsshared/zvector.h \
		cpputils/cpputilsshared/range.h \
		cpputils/cpputilsshared/stderr_exception.h \
		cpputils/cpputilsshared/stderr_exception.cc \
		cpputils/cpputilsshared/sql_exception.h \
		cpputils/cpputilsshared/utf8.h \
		cpputils/cpputilsshared/utf8/core.h \
		cpputils/cpputilsshared/utf8/checked.h \
		cpputils/cpputilsshared/utf8/unchecked.h \
		cpputils/cpputilsshared/utf8/cpp11.h \
		cpputils/cpputilsshared/utf8/cpp17.h \
		cpputils/cpputilsshared/utf8_util.h \
		cpputils/cpputilsshared/utf8_util.cc


cpputils_cpputilsshared_cpputilsformat_libcpputilsformat_a_SOURCES= \
	cpputils/cpputilsshared/cpputilsformat/format1.cc \
	cpputils/cpputilsshared/cpputilsformat/format2.cc \
	cpputils/cpputilsshared/cpputilsformat/format2.h \
	cpputils/cpputilsshared/cpputilsformat/format1.h \
	cpputils/cpputilsshared/cpputilsformat/format.h \
	cpputils/cpputilsshared/cpputilsformat/cformat.cc \
	cpputils/cpputilsshared/cpputilsformat/cformat.h \
	cpputils/cpputilsshared/cpputilsformat/wformat2.cc \
	cpputils/cpputilsshared/cpputilsformat/wformat2.h \
	cpputils/cpputilsshared/cpputilsformat/cwformat.h \
	cpputils/cpputilsshared/cpputilsformat/cwformat.cc

adjust_monitor_settings_LDADD = \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

list_monitor_resolutions_LDADD = \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

create_desktop_icons_LDADD = \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

LIBS=-lole32 -luuid
	
AM_LDFLAGS= -static
	
if MINGW
AM_LDFLAGS +=
# with -std=c++11 WIN32 is disabled (_WIN32) would be the valid one, so we define it in mingw.
AM_CPPFLAGS += -DWIN32
else
AM_LDFLAGS +=
endif

# ---------------------------------------------------------------------------
# Test suite (make check)
#
# The harness lives in testcommon/, not common/, because common.cc and common.h
# already exist at the top level for the monitor tools. One program aggregates every
# subject, following the majorkingleo/cpputilstest layout.
#
# NOTE: tests/data/ is intentionally NOT in EXTRA_DIST yet - the fixtures are 6 MB of
# binaries, they are tracked in git, and nothing here runs `make dist`. Revisit if a
# release tarball ever needs to be self-contained.
# ---------------------------------------------------------------------------

AM_CPPFLAGS += -I$(top_srcdir)/testcommon
AM_CPPFLAGS += -I$(top_srcdir)/third_party/minilzo

noinst_LIBRARIES += testcommon/libtestcommon.a

# core .rfa library: container reader/writer plus the codec wrapper.
# It inherits AM_CPPFLAGS, which already carries -I third_party/minilzo.
noinst_LIBRARIES += rfa/librfa.a

rfa_librfa_a_SOURCES = \
		rfa/RfaFormat.h \
		rfa/RfaStamp.h \
		rfa/LzoCodec.h \
		rfa/LzoCodec.cc \
		rfa/RfaArchive.h \
		rfa/RfaArchive.cc \
		rfa/RfaWriter.h \
		rfa/RfaWriter.cc

# vendored third-party LZO1X codec (see third_party/minilzo/README.md).
# It gets its own CPPFLAGS because AM_CPPFLAGS carries -std=c++20, which is not a
# valid option for a C translation unit.
noinst_LIBRARIES += third_party/minilzo/libminilzo.a

third_party_minilzo_libminilzo_a_SOURCES = \
		third_party/minilzo/minilzo.c \
		third_party/minilzo/minilzo.h \
		third_party/minilzo/lzoconf.h \
		third_party/minilzo/lzodefs.h

third_party_minilzo_libminilzo_a_CPPFLAGS = -I$(top_srcdir)/third_party/minilzo

testcommon_libtestcommon_a_SOURCES = \
		testcommon/TestUtils.h \
		testcommon/TestUtils.cc \
		testcommon/ColBuilder.h \
		testcommon/ColBuilder.cc \
		testcommon/TestRunner.h \
		testcommon/TestRunner.cc

check_PROGRAMS = test_rfa

test_rfa_SOURCES = \
		src_test_rfa/test_rfa.cc \
		src_test_rfa/test_lzo.h \
		src_test_rfa/test_lzo.cc \
		src_test_rfa/test_rfa_archive.h \
		src_test_rfa/test_rfa_archive.cc \
		src_test_rfa/test_rfa_format.h \
		src_test_rfa/test_rfa_format.cc \
		src_test_rfa/test_rfa_writer.h \
		src_test_rfa/test_rfa_writer.cc \
		src_test_rfa/test_testcommon.h \
		src_test_rfa/test_testcommon.cc \
		tools_config.h

test_rfa_LDADD = \
		testcommon/libtestcommon.a \
		rfa/librfa.a \
		third_party/minilzo/libminilzo.a \
		cpputils/cpputilsshared/libcpputilsshared.a \
		cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

TESTS = test_rfa

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	README \
	LICENSE 
	
	
	
	
