bin_PROGRAMS=adjust_monitor_settings

adjust_monitor_settings_SOURCES=\
		tools_config.h\
		adjust_monitor_settings.cc
			  
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
		cpputils/cpputilsshared/logtool2.cc \
		cpputils/cpputilsshared/logtool2.h \
		cpputils/cpputilsshared/string_utils.cc \
		cpputils/cpputilsshared/string_utils.h \
		cpputils/cpputilsshared/vec_utils.cc \
		cpputils/cpputilsshared/vec_utils.h \
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
		cpputils/cpputilsshared/report_exception.h \
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
	cpputils/cpputilsshared/cpputilsformat/cformat.h \
	cpputils/cpputilsshared/cpputilsformat/wformat2.cc \
	cpputils/cpputilsshared/cpputilsformat/wformat2.h \
	cpputils/cpputilsshared/cpputilsformat/cwformat.h \
	cpputils/cpputilsshared/cpputilsformat/cwformat.cc

adjust_monitor_settings_LDADD = \
	cpputils/cpputilsshared/libcpputilsshared.a \
	cpputils/cpputilsshared/cpputilsformat/libcpputilsformat.a 

LIBS=
	
AM_LDFLAGS= -static 
	
if MINGW
AM_LDFLAGS +=
# with -std=c++11 WIN32 is disabled (_WIN32) would be the valid one, so we define it in mingw.
AM_CPPFLAGS += -DWIN32
else
AM_LDFLAGS +=
endif

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	README \
	LICENSE 
	
	
	
	
