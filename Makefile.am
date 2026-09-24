bin_PROGRAMS=adjust_monitor_settings \
	create_desktop_icons \
	list_monitor_resolutions \
	rfaPack \
	rfaUnpack

adjust_monitor_settings_SOURCES=\
		tools_config.h\
		src_adjust_monitor_settings/adjust_monitor_settings.cc
		
list_monitor_resolutions_SOURCES=\
		tools_config.h\
		src_list_monitor_resolutions/list_monitor_resolutions.cc
				
create_desktop_icons_SOURCES=\
		tools_config.h \
		src_create_desktop_icons/create_desktop_icons.cc \
		src_create_desktop_icons/ShortcutProvider.h \
		src_create_desktop_icons/ShortcutProvider.cc
			  
AM_CPPFLAGS = \
	-I$(top_srcdir)/libcommon \
	-I$(top_srcdir)/cli \
	-I$(top_srcdir)/cpputils/io \
	-I$(top_srcdir)/cpputils/cpputilsshared  \
	-I$(top_srcdir)/cpputils/cpputilsshared/cpputilsformat \
	-I$(top_srcdir)/src  \
	-DNOWAMAS \
	-std=c++23

noinst_LIBRARIES=\
	libcommon/libcommon.a \
	cpputils/io/libcpputilsio.a \
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
		cpputils/cpputilsshared/utf8_util.cc \
		cpputils/cpputilsshared/FastDelivery.h


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

# cpputils/io brings the debug plumbing: Tools::Debug / Tools::x_debug (which CPPDEBUG
# calls), the synchronous OutDebug frontend, the console colour helper, and the locale
# detection the output paths use. read_file is a dependency of DetectLocale.
noinst_LIBRARIES += cpputils/io/libcpputilsio.a

cpputils_io_libcpputilsio_a_SOURCES = \
		cpputils/io/ColoredOutput.h \
		cpputils/io/ColoredOutput.cc \
		cpputils/io/CpputilsDebug.h \
		cpputils/io/CpputilsDebug.cc \
		cpputils/io/DetectLocale.h \
		cpputils/io/DetectLocale.cc \
		cpputils/io/OutDebug.h \
		cpputils/io/OutDebug.cc \
		cpputils/io/read_file.h \
		cpputils/io/read_file.cc

# Shared code of the monitor tools: get_user_profile() plus the debug logging
# frontend and backends (see .github/skills/debug-logging/). Its directory is on the
# include path via AM_CPPFLAGS above, so the tools keep their plain
# #include "common.h" from inside their own subdirectories.
libcommon_libcommon_a_SOURCES = \
		libcommon/common.h \
		libcommon/common.cc \
		libcommon/AsyncOutDebug.h \
		libcommon/AsyncOutDebug.cc \
		libcommon/AsyncFileLogger.h \
		libcommon/AsyncFileLogger.cc \
		libcommon/DebugLog.h \
		libcommon/DebugLog.cc

adjust_monitor_settings_LDADD = \
	libcommon/libcommon.a \
	cpputils/io/libcpputilsio.a \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

list_monitor_resolutions_LDADD = \
	libcommon/libcommon.a \
	cpputils/io/libcpputilsio.a \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

create_desktop_icons_LDADD = \
	libcommon/libcommon.a \
	cpputils/io/libcpputilsio.a \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

# -liconv is needed by cpputils/io/read_file.cc (ReadFile::convert), which DetectLocale
# pulls in to translate between the console encoding and UTF-8. The mingw-w64 sysroot
# ships libiconv.a, and -static above makes the linker prefer it over the .dll.a.
LIBS=-lole32 -luuid -liconv
	
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
# The harness lives in testcommon/, not common/, because the monitor tools' shared code
# lives in libcommon/. One program aggregates every subject, following the
# majorkingleo/cpputilstest layout.
#
# NOTE: tests/data/ is intentionally NOT in EXTRA_DIST yet - the fixtures are 6 MB of
# binaries, they are tracked in git, and nothing here runs `make dist`. Revisit if a
# release tarball ever needs to be self-contained.
# ---------------------------------------------------------------------------

AM_CPPFLAGS += -I$(top_srcdir)/testcommon
AM_CPPFLAGS += -I$(top_srcdir)/third_party/lzo/include

noinst_LIBRARIES += testcommon/libtestcommon.a

# core .rfa library: container reader/writer plus the codec wrapper.
# It inherits AM_CPPFLAGS, which already carries -I third_party/lzo/include.
noinst_LIBRARIES += rfa/librfa.a

rfa_librfa_a_SOURCES = \
		rfa/RfaFormat.h \
		rfa/RfaStamp.h \
		rfa/CpuCount.h \
		rfa/CpuCount.cc \
		rfa/ParallelFor.h \
		rfa/LzoCodec.h \
		rfa/LzoCodec.cc \
		rfa/RfaArchive.h \
		rfa/RfaArchive.cc \
		rfa/RfaWriter.h \
		rfa/RfaWriter.cc

# Command lines of the .rfa tools. A library rather than loose files in the program
# directories, because the compatibility tests call the same entry points the programs
# call: spawning the binaries from a test is not viable (PLAN_rfa_tools.md finding 19).
noinst_LIBRARIES += cli/libcli.a

cli_libcli_a_SOURCES = \
		cli/PackCli.h \
		cli/PackCli.cc \
		cli/ThreadSwitch.h \
		cli/UnpackCli.h \
		cli/UnpackCli.cc

# --- rfaPack ---------------------------------------------------------------

rfaPack_SOURCES = \
		src_rfaPack/rfaPack.cc \
		cli/PackCli.h \
		tools_config.h

rfaPack_LDADD = \
		cli/libcli.a \
		rfa/librfa.a \
		third_party/lzo/liblzo.a \
		libcommon/libcommon.a \
		cpputils/io/libcpputilsio.a \
		cpputils/cpputilsshared/libcpputilsshared.a \
		cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

# --- rfaUnpack -------------------------------------------------------------

rfaUnpack_SOURCES = \
		src_rfaUnpack/rfaUnpack.cc \
		cli/UnpackCli.h \
		tools_config.h

rfaUnpack_LDADD = \
		cli/libcli.a \
		rfa/librfa.a \
		third_party/lzo/liblzo.a \
		libcommon/libcommon.a \
		cpputils/io/libcpputilsio.a \
		cpputils/cpputilsshared/libcpputilsshared.a \
		cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

# vendored third-party LZO1X codec (see third_party/lzo/README.md).
#
# Five translation units rather than the whole library: that is the closure the linker pulls
# in for lzo1x_999_compress_level + lzo1x_1_compress + lzo1x_decompress_safe, measured with
# -Wl,-Map. It gets its own CPPFLAGS because AM_CPPFLAGS carries -std=c++20, which is not a
# valid option for a C translation unit - and because the internal headers are included as
# "lzo_conf.h" from the sources, so src/ has to be on the include path too.
noinst_LIBRARIES += third_party/lzo/liblzo.a

third_party_lzo_liblzo_a_SOURCES = \
		third_party/lzo/src/lzo_init.c \
		third_party/lzo/src/lzo1x_1.c \
		third_party/lzo/src/lzo1x_1o.c \
		third_party/lzo/src/lzo1x_9x.c \
		third_party/lzo/src/lzo1x_d2.c \
		third_party/lzo/src/config1x.h \
		third_party/lzo/src/lzo_conf.h \
		third_party/lzo/src/lzo_dict.h \
		third_party/lzo/src/lzo_dll.ch \
		third_party/lzo/src/lzo_func.h \
		third_party/lzo/src/lzo_mchw.ch \
		third_party/lzo/src/lzo_ptr.h \
		third_party/lzo/src/lzo_supp.h \
		third_party/lzo/src/lzo_swd.ch \
		third_party/lzo/src/lzo1_d.ch \
		third_party/lzo/src/lzo1x_c.ch \
		third_party/lzo/src/lzo1x_d.ch \
		third_party/lzo/include/lzo/lzo1x.h \
		third_party/lzo/include/lzo/lzoconf.h \
		third_party/lzo/include/lzo/lzodefs.h

third_party_lzo_liblzo_a_CPPFLAGS = \
		-I$(top_srcdir)/third_party/lzo/include \
		-I$(top_srcdir)/third_party/lzo/src

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
		src_test_rfa/test_debuglog.h \
		src_test_rfa/test_debuglog.cc \
		src_test_rfa/test_rfa_cli.h \
		src_test_rfa/test_rfa_cli.cc \
		tools_config.h

test_rfa_LDADD = \
		testcommon/libtestcommon.a \
		cli/libcli.a \
		libcommon/libcommon.a \
		rfa/librfa.a \
		third_party/lzo/liblzo.a \
		cpputils/io/libcpputilsio.a \
		cpputils/cpputilsshared/libcpputilsshared.a \
		cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a

TESTS = test_rfa

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	README.md \
	LICENSE 
	
	
	
	
