/**
 * CppSqlReader: Something like WamasSqlReader
 * @author Copyright (c) 2022 SSI Schaefer IT Solutions
 *
 * Various examples can befound at cpputils test suite:
 * http://w4.prj.sources.wamas.com/cgit/cpputilstest.git/tree/test_cpputilsshared/test_cppsqlreader.cc
 *
 * Public function: (all functions can throw SQL_EXECPTION or REPORT_EXCEPTION)
 *
 * 		// select datatypes
 * 		CppSqlReader::selStruct()
 * 		CppSqlReader::selChar()
 * 		CppSqlReader::selShort()
 * 		CppSqlReader::selInt()
 * 		CppSqlReader::selLong()
 * 		CppSqlReader::selTimet()
 * 		CppSqlReader::selTimel()
 * 		CppSqlReader::selDate()
 * 		CppSqlReader::selDouble()
 * 		CppSqlReader::selFloat()
 * 		CppSqlReader::selStr() // std::string only
 * 		CppSqlReader::selAttr()
 *
 * 		// bind datatypes
 * 		CppSqlReader::sqlChar()
 * 		CppSqlReader::sqlShort()
 * 		CppSqlReader::sqlInt()
 * 		CppSqlReader::sqlLong()
 * 		CppSqlReader::sqlTimet()
 * 		CppSqlReader::sqlTimel()
 * 		CppSqlReader::sqlDate()
 * 		CppSqlReader::sqlDouble()
 * 		CppSqlReader::sqlFloat()
 * 		CppSqlReader::sqlStr() // char* and std::string
 *
 *		// select datatypes with offsets
 * 		CppSqlReader::selOffsetStruct()
 * 		CppSqlReader::selOffsetChar()
 * 		CppSqlReader::selOffsetShort()
 * 		CppSqlReader::selOffsetInt()
 * 		CppSqlReader::selOffsetLong()
 * 		CppSqlReader::selOffsetTimet()
 * 		CppSqlReader::selOffsetTimel()
 * 		CppSqlReader::selOffsetDate()
 * 		CppSqlReader::selOffsetDouble()
 * 		CppSqlReader::selOffsetFloat()
 * 		CppSqlReader::selOffsetStr() // std::string only
 * 		CppSqlReader::selOffsetAttr()
 *
 *
 *      // modify quers
 *      CppSqlReader::appendQuery()
 *
 *      // execute query
 *      CppSqlReader::execAll()
 *      CppSqlReader::execBlockwise()
 *
 * Basic usage:
 *
 * ************************ Example 1 * Fetch Data *********************
 * 		std::vector<KPL> vKPL;
 *
 * 		CppSqlReader sqlReader(FAC_DEFAULT, tid, "select %" TN_KPL " from " TN_KPL);
 *
 * 		sqlReader.selStruct( TN_KPL, vKPL); // register vector vKPL, all data will be stored here
 * 		sqlReader.execAll();
 *
 * 		for (unsigned i = 0; i < vKPL.size(); i++) {
 *	 		KPL &kpl = vKPL[i];
 *   		std::cout << format("% 4d %s", i, kpl.kplFeldId) << std::endl;
 * 		}
 *
 * ************************ Example 2 * Bind a Variable ****************
 *
 * 		std::vector<std::string> vFeldId;
 *
 * 		CppSqlReader sqlReader(FAC_DEFAULT, tid, "select " TCN_KPL_FeldId " from " TN_KPL );
 *
 * 		sqlReader.selStr( vFeldId, FELDID_LEN+1 );
 *
 *      // extend query an bind a value
 * 		sqlReader.appendQuery(" where " TCN_KPL_OrdNr " > :ordnr " ).sqlLong( 1000 );
 * 		sqlReader.execAll();
 *
 *		for (unsigned i = 0; i < vFeldId.size(); i++) {
 *			std::cout << format("% 4d std::string %s", i, vFeldId[i]) << std::endl;
 *		}
 *
 * ************************ Example 3 * Read Blockwise *****************
 *
 *		std::vector<KPL> vKPL;
 *
 *		CppSqlReader sqlReader(FAC_DEFAULT, tid, "select %" TN_KPL " from " TN_KPL);
 *		sqlReader.selStruct( TN_KPL, vKPL);
 *
 *		while( sqlReader.execBlockwise() ) {
 *			for (unsigned i = 0; i < vKPL.size(); i++) {
 *				KPL &kpl = vKPL[i];
 *				std::cout << format("% 4d %s", i, kpl.kplFeldId) << std::endl;
 *			}
 *		}
 *
 * ************************ Example 4 * Select into a substruct *****************
 *
 *		struct KPL_FES
 *		{
 *		  KPL kpl;
 *		  FES fes;
 *	  	};
 *
 *	  	std::vector<KPL_FES> vKplFes;
 *
 *  	CppSqlReader sqlReader(FAC_DEFAULT, tid,  "select %" TN_KPL ", %" TN_FES " from " TN_KPL ", "  TN_FES
 *												 " where " TCN_KPL_FeldId " = " TCN_FES_FeldId );
 *
 *		sqlReader.addStruct4OffsetSelection( "kplfes", vKplFes );     // name it somehow
 *		sqlReader.selOffsetStruct( "kplfes", TN_KPL, vKplFes[0].kpl ) // tell sqlreader the table name and field
 *				 .selOffsetStruct( "kplfes", TN_FES, vKplFes[0].fes );
 *
 *	   size_t size = sqlReader.execAll();
 *
 *	   for (size_t i = 0; i < size; i++) {
 *		  std::cout << format("% 4d FeldId: %s ArtNr: %ld", i, vKplFes[i].fes.fesFeldId, vKplFes[i].kpl.kplArtNr ) << std::endl;
 *	   }
 *
 */

#ifndef _wamas_CPPSQLREADER_H
#define _wamas_CPPSQLREADER_H

#if __cplusplus >= 201103

#include <cpp_util.h>
#include <algorithm>
#include <memory>
#include <typeinfo>
#include <sqltable.h>
#include <logtool2.h>

#include <boost/version.hpp>

//#define BOOST_VERSION 1
#if (__cplusplus >= 201402) && (BOOST_VERSION >= 107500 )
#define BOOST_PFR_USE_CPP17 0
#include <boost/pfr.hpp>
#undef BOOST_PFR_USE_CPP17
#endif

// gcc 10 can handle c++14, but it was probably disable by -std=gnu++11
#if __GNUC__ >= 10 && __cplusplus < 201402
#	warning "compile with -std=gnu++14 to enable all type check features"
#endif

#define CPPSQLREADER_API	(1)

namespace Tools {

class CppSqlReaderBase
{
private:
	static const char *pcSourceFile;
	static int iSourceLine;

protected:
	void *tid;
	std::string fac;
	std::string sql;
	CppContext ctxt;
	SqlTexecArgsPtr execArgs;
	std::string sourceFile;
	int sourceLine;
	int rvBlockwise;

public:
	CppSqlReaderBase( const std::string & fac_, void *tid_, const std::string & sql_ );

	CppSqlReaderBase( const CppSqlReaderBase & other ) = delete;
	CppSqlReaderBase & operator=( const CppSqlReaderBase & other ) = delete;

	virtual ~CppSqlReaderBase();

	SqlTexecArgsPtr getExecArgs() {
		return execArgs;
	}

	const std::string & getStatment() const {
		return sql;
	}

	static void setSourceLocation( const char *pcSource, int iLine );

	const std::string & getSourceFile() const {
		return sourceFile;
	}

	int getSourceLine() const {
		return sourceLine;
	}

	void* getTid() const {
		return tid;
	}
};

#define CppSqlReader  CppSqlReaderBase::setSourceLocation( __FILE__, __LINE__ ); Tools::CppSqlReaderImpl

#define THROW_CPPSQLREADER_REPORT_EXCEPTION( what ) \
	{ \
		LogPrintf( fac, LT_ALERT, Tools::format( "from %s:%d %s", sourceFile, sourceLine, what ) ); \
		throw ReportException( Tools::format( "Exception from: %s:%d:%s message: %s%s\ninitial at: %s:%d", \
				sourceFile, sourceLine, __FUNCTION__, what, Tools::BackTraceHelper::bt.bt(),  __FILE__, __LINE__ ), \
				Tools::format("%s", what ) ); \
	}

#define CPPSQLREADER_SQL_EXCEPTION( tid ) \
	Tools::SqlException( Tools::format( "SqlError from: %s:%d:%s %s%s\ninitial at: %s:%d",\
		sourceFile, sourceLine, __FUNCTION__, \
		TSqlErrTxt(tid),					\
		Tools::BackTraceHelper::bt.bt(), \
		__FILE__, __LINE__ ), TSqlErrTxt(tid), TSqlError(tid) )

class CppSqlReaderImpl : public CppSqlReaderBase
{
private:
	class TableWrapperBase
	{
	protected:
		std::string table_name;
		bool have_exec_arg;
		size_t table_size;
		bool append_exec_arg_later;

	public:
		TableWrapperBase( const std::string & table_name_ = "", size_t table_size = 0 );

		virtual ~TableWrapperBase();

		// enlarge the vector to fit next BLOCKSIZE
		virtual void moveIdx( size_t incIdx ) = 0;

		// returns the next start address for the next BLOCK
		virtual void* getActualStartAddress() =  0;

		// reduce current vector size to final size so
		// vector.size() will return a correct result
		virtual void finish() = 0;

		// reduce current vector size to final size so
		// vector.size() will return a correct result
		// size is the total size of the vector
		// function used in execBlockwise()
		virtual void finishWithSize( size_t size ) = 0;

		virtual const std::string & getName() const {
			return table_name;
		}

		bool haveExecArg() const {
			return have_exec_arg;
		}

		void setHaveExecArg( bool state ) {
			have_exec_arg = state;
		}

		size_t getTableSize() const {
			return table_size;
		}

		void setAppendExecArgLater( bool state ) {
			append_exec_arg_later = state;
		}

		bool willAppendExecArgLater() {
			return append_exec_arg_later;
		}

		virtual void appendExecArg( CppSqlReaderBase *sqlReader ) {}
	};

	template<class T> class TableWrapper : public TableWrapperBase
	{
	public:
		std::vector<T> & vData;
		const size_t BLOCKSIZE;
		size_t idx;

	public:
		TableWrapper( std::vector<T> & vData_, const size_t BLOCKSIZE_, const std::string & name_ = "" )
		: TableWrapperBase( name_, sizeof(T) ),
		  vData( vData_),
		  BLOCKSIZE( BLOCKSIZE_ ),
		  idx(0)
		{
			vData.clear();
			vData.resize( BLOCKSIZE );
		}

		TableWrapper( const TableWrapper & other ) = delete;
		TableWrapper & operator=( const TableWrapper & other ) = delete;

		virtual void moveIdx( size_t incIdx ) override
		{
			size_t old_size = vData.size();
			vData.resize( old_size + BLOCKSIZE );
			idx += incIdx;
		}

		virtual void* getActualStartAddress() override {
			return &vData[idx];
		}

		virtual void finish() override {
			vData.resize(idx);
		}

		virtual void finishWithSize( size_t size ) override {
			vData.resize(size);
		}
	};

	class TableWrapperOffset : public TableWrapperBase
	{
	protected:
		std::shared_ptr<TableWrapperBase> parentTable;
		size_t offset;
		std::string sub_table_name;
	public:
		TableWrapperOffset( std::shared_ptr<TableWrapperBase> & parentTable_, size_t offset_ );

		virtual void* getActualStartAddress() override;

		// does nothing
		virtual void finish() override;

		// does nothing
		virtual void finishWithSize( size_t size ) override;

		// does nothing
		virtual void moveIdx( size_t incIdx ) override;

		void setSubTableName( const std::string & name ) {
			sub_table_name = name;
		}

		const std::string & getSubTableName() {
			return sub_table_name;
		}

		size_t getOffset() const {
			return offset;
		}
	};

	class TableWrapperOffsetStruct : public TableWrapperOffset
	{
	public:
		TableWrapperOffsetStruct( std::shared_ptr<TableWrapperBase> & parentTable_, size_t offset_ )
		: TableWrapperOffset( parentTable_, offset_ )
		{}

		void appendExecArg( CppSqlReaderBase *sqlReader ) override;
	};

	class TableWrapperString : public TableWrapperBase
	{
	public:
		std::vector<std::string> & vData;
		std::vector<char> vBuffer;
		const size_t BLOCKSIZE;
		const size_t FIELD_LEN;
		size_t idx;

	public:
		TableWrapperString( std::vector<std::string> & vData_, const size_t BLOCKSIZE_, const size_t FIELD_LEN_ );

		TableWrapperString( const TableWrapperString & other ) = delete;
		TableWrapperString & operator=( const TableWrapperString & other ) = delete;

		virtual void moveIdx( size_t incIdx ) override;

		virtual void* getActualStartAddress() override;

		char *getStartAddress() {
			return &vBuffer[0];
		}

		// does nothing
		virtual void finish() override;

		// may reduce vData size to last size, when reading blockwise
		virtual void finishWithSize( size_t size ) override;
	};

	const size_t BLOCKSIZE;
	typedef std::list< std::shared_ptr<TableWrapperBase> > TABLES_CONT;
	TABLES_CONT vTables;

public:
	CppSqlReaderImpl( const std::string & fac_, void *tid_, const std::string & sql_, const size_t BLOCKSIZE_ = 100 );

	template <class TABLE> CppSqlReaderImpl & selStruct( const std::string & table_name, std::vector<TABLE> & vTable )
	{
		int size = getStructSize(const_cast<char*>(table_name.c_str()));

		if( size == -1 ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "cannot determine sizeof table '%s'", table_name ) );
		}

		if( static_cast<size_t>( size ) != sizeof(TABLE) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table size of table '%s'(%d) is not '%s'(%ul)",
																table_name,
																size,
																typeid(TABLE).name(), // this returns a demangled name
																sizeof(TABLE) ) );
		}

		auto table = std::make_shared<TableWrapper<TABLE>>( vTable, BLOCKSIZE, table_name );
		vTables.push_back( table );

		int rv = _SqlExecArgsAppend( execArgs, SELSTRUCT( table->getName().c_str(), table->vData[0] ), NULL );
		if( rv < 0 ) {
			throw CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		return *this;
	}

	template <class TABLE> void addStruct4OffsetSelection( const std::string & table_name, std::vector<TABLE> & vTable )
	{
		for( auto & pTable : vTables ) {
			if( pTable->getName() == table_name ) {
				THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table with name: '%s' already appended", table_name ) );
			}
		}

		auto table = std::make_shared<TableWrapper<TABLE>>( vTable, BLOCKSIZE, table_name );
		table->setHaveExecArg( false );
		vTables.push_back( table );

		/* do not return *this, because when chaining some operations
		 * the computations of the values can happen before, out of order.
		 *
		 * eg:
		 * sqlReader.addStruct4OffsetSelection( "xxx", vXXX )
		 *          .selOffsetStr<xxx>( "xxx", vXXX[0].kplFeldId, FELDID_LEN+1 );
		 *          
		 * selOffsetStruct() will call vXXX.resize(BLOCKSIZE), 
		 * but computation of vXXX[0].kplFeldId might already happened on an empty vector
		 * so the addresss that is used for offset calculation is not usable. 
		 * something like 0x38
		 */ 
	}

	CppSqlReaderImpl & selStr( std::vector<std::string> & vTable, const size_t size );

	CppSqlReaderImpl & selAttr( SqlType & attr, std::vector<long> & vTable );

	/*
	template <class T> CppSqlReaderImpl & selOffsetLong( const std::string & table_name, size_t offset )
	{
		std::shared_ptr<TableWrapperBase> pTable = findOffsTable( table_name );
		auto table = std::make_shared<TableWrapperOffset>( pTable, offset );

		vTables.push_back( table );
		void *start_address = table->getActualStartAddress();

		int rv = _SqlExecArgsAppend( execArgs, LONGCODE,_SELOFS,reinterpret_cast<long*>(start_address),sizeof(T), NULL );
		if( rv < 0 ) {
			throw CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		return *this;
	}*/

	/*
	template <class T> CppSqlReaderImpl & selOffsetLong( const std::string & table_name, long & lValue )
	{
		std::shared_ptr<TableWrapperBase> pTable = findOffsTable( table_name );

		if( pTable->getTableSize() != sizeof(T) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table size of table '%s'(%d) is not '%s'(%ul)",
															   table_name,
															   pTable->getTableSize(),
															   typeid(T).name(), // this returns a demangled name
															   sizeof(T) ) );
		}

		char *start_address = reinterpret_cast<char*>(pTable->getActualStartAddress());
		size_t offset = reinterpret_cast<char*>(&lValue) - start_address;

		auto table = std::make_shared<TableWrapperOffset>( pTable, offset );

		vTables.push_back( table );
		void *value_start_address = table->getActualStartAddress();

		int rv = _SqlExecArgsAppend( execArgs, LONGCODE,_SELOFS,reinterpret_cast<long*>(value_start_address),sizeof(T), NULL );
		if( rv < 0 ) {
			THROW_CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		return *this;
	}
	*/

#define CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( NAME, TYPE, SELMAKRO ) \
	CppSqlReaderImpl & NAME( std::vector<TYPE> & vTable ) \
	{ \
		auto table = std::make_shared<TableWrapper<TYPE>>( vTable, BLOCKSIZE ); \
		vTables.push_back( table ); \
		int rv = _SqlExecArgsAppend( execArgs, SELMAKRO( table->vData[0] ), NULL ); \
		if( rv < 0 ) { \
			LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) ); \
			throw CPPSQLREADER_SQL_EXCEPTION( tid ); \
		} \
		return *this; \
	}

	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selChar, char, SELCHAR )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selShort, short, SELSHORT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selInt, int, SELINT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selLong, long, SELLONG )

	// SELTIMEL does not work on 64-winnt since long is 32bit
#if !defined WIN32 && !defined _WIN32
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selTimel, long, SELTIMEL )
#endif	

	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selFloat, float, SELFLOAT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selDouble, double, SELDOUBLE )

	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selTimet, time_t, SELTIMET )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SEL( selDate, struct tm, SELDATE )

#undef CPPSQLREADER_IMPLEMENT_SIMPLE_SEL

#define CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( NAME, TYPE, SQLMAKRO ) \
	CppSqlReaderImpl & NAME( const TYPE & arg ) \
	{ \
		int rv = _SqlExecArgsAppend( execArgs, SQLMAKRO( arg ), NULL ); \
		if( rv < 0 ) { \
			LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) ); \
			throw CPPSQLREADER_SQL_EXCEPTION( tid ); \
		} \
		return *this; \
	}

	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlChar, char, SQLCHAR )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlShort, short, SQLSHORT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlInt, int, SQLINT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlLong, long, SQLLONG )

	// SQLTIMEL does not work on 64-winnt since long is 32bit
#if !defined WIN32 && !defined _WIN32
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlTimel, long, SQLTIMEL )
#endif	

	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlFloat, float, SQLFLOAT )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlDouble, double, SQLDOUBLE )

	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlTimet, time_t, SQLTIMET )
	CPPSQLREADER_IMPLEMENT_SIMPLE_SQL( sqlDate, struct tm, SQLDATE )

	CppSqlReaderImpl & sqlStr( const std::string & str );

	CppSqlReaderImpl & sqlStr( const char * str );

#if (__cplusplus >= 201402) && (BOOST_VERSION >= 107500 )
#	define CPPSQLREADER_IMPLEMENT_OFFSET_TYPE_CHECK( TYPE ) \
		/* check if the given offset matches any field of the struct, and if yes, */ \
		/* is it the same type */ \
		bool found_field_at_correct_offset = false; \
		T type = {}; \
		\
		boost::pfr::for_each_field(type, [&type, &found_field_at_correct_offset, offset,this](auto& field) { \
				if( !found_field_at_correct_offset ) { \
					size_t field_offset = (char*)&field -(char*)&type; \
					if( field_offset == offset ) { \
						found_field_at_correct_offset = true; \
						if( typeid(field) != typeid( TYPE ) ) { \
							THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "invalid type %s != %s", \
																				typeid(field).name(), \
																				typeid( TYPE ).name()) ); \
						} \
					} \
				} \
	    }); \
	    \
		if( !found_field_at_correct_offset ) { \
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'", \
																		    table_name ) ); \
		}
#else
#	define CPPSQLREADER_IMPLEMENT_OFFSET_TYPE_CHECK( TYPE )
#endif

#define CPPSQLREADER_IMPLEMENT_OFFSET_SEL( NAME, TYPE, SQLMAKRO ) \
	template <class T> CppSqlReaderImpl & NAME( const std::string & table_name, TYPE & lValue ) \
	{ \
		std::shared_ptr<TableWrapperBase> pTable = findOffsTable( table_name ); \
		\
		if( pTable->getTableSize() != sizeof(T) ) { \
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table size of table '%s'(%d) is not '%s'(%ul)", \
															   table_name, \
															   pTable->getTableSize(), \
															   typeid(T).name(), \
															   sizeof(T) ) ); \
		} \
		\
		char *start_address = reinterpret_cast<char*>(pTable->getActualStartAddress()); \
		size_t offset = reinterpret_cast<char*>(&lValue) - start_address; \
		\
		if( offset > sizeof(T) ) { \
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'", \
																table_name ) ); \
		} \
		\
		CPPSQLREADER_IMPLEMENT_OFFSET_TYPE_CHECK( TYPE ) \
		auto table = std::make_shared<TableWrapperOffset>( pTable, offset ); \
		\
		vTables.push_back( table ); \
		void *value_start_address = table->getActualStartAddress(); \
		\
		int rv = _SqlExecArgsAppend( execArgs, SQLMAKRO,_SELOFS,reinterpret_cast<TYPE*>(value_start_address),sizeof(T), NULL ); \
		if( rv < 0 ) { \
			LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) ); \
			throw CPPSQLREADER_SQL_EXCEPTION( tid ); \
		} \
		\
		return *this; \
	}


	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetChar,  char, CHARCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetShort, short, SHORTCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetInt, int, INTCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetLong, long, LONGCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetTimel, long, TIMELCODE )

	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetFloat, float, FLOATCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetDouble, double, DOUBLECODE )

	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetTimet, time_t, TIMETCODE )
	CPPSQLREADER_IMPLEMENT_OFFSET_SEL( selOffsetDate, struct tm, DATECODE )

	template <class T> CppSqlReaderImpl & selOffsetStr( const std::string & table_name, char * pcValue, size_t FIELD_LEN )
	{
		std::shared_ptr<TableWrapperBase> pTable = findOffsTable( table_name );

		if( pTable->getTableSize() != sizeof(T) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table size of table '%s'(%d) is not '%s'(%ul)",
															   table_name,
															   pTable->getTableSize(),
															   typeid(T).name(), // this returns a demangled name
															   sizeof(T) ) );
		}

		char *start_address = reinterpret_cast<char*>(pTable->getActualStartAddress());
		size_t offset = pcValue - start_address;

		if( offset > sizeof(T) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'",
															    table_name ) );
		}

#if (__cplusplus >= 201402) && (BOOST_VERSION >= 107500 )
		// check if the given offset matches any field of the struct, and if yes,
		// is it the same type
		bool found_field_at_correct_offset = false;
		T type = {};

		boost::pfr::for_each_field(type, [&type, &found_field_at_correct_offset, offset,this](auto& field) {
				if( !found_field_at_correct_offset ) {
					size_t field_offset = (char*)&field -(char*)&type;
					if( field_offset == offset ) {
						found_field_at_correct_offset = true;
						if( typeid(field) != typeid( char ) ) {
							THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "invalid type %s != %s",
																				typeid(field).name(),
																				typeid( char ).name()) );
						}
					}
				}
	    });

		if( !found_field_at_correct_offset ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'",
																		    table_name ) );
		}
#endif

		auto table = std::make_shared<TableWrapperOffset>( pTable, offset );

		vTables.push_back( table );
		void *value_start_address = table->getActualStartAddress();

											   // SELOFSSTR(block->orderId,MAS_TELE_ORDERID_LEN+1,sizeof(PALORDER)),
											   // SELOFSSTR(x,l,o) => STRINGCODE,_SELOFS,x,l,(long)o
		int rv = _SqlExecArgsAppend( execArgs, STRINGCODE,_SELOFS,reinterpret_cast<char*>(value_start_address),FIELD_LEN,sizeof(T), NULL );
		if( rv < 0 ) {
			LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) );
			throw CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		return *this;
	}

	template <class TABLE> CppSqlReaderImpl & selOffsetStruct( const std::string & container_name,
												 			   const std::string & table_name,
												 			   TABLE & table_value )
	{
		std::shared_ptr<TableWrapperBase> pContainerTable = findOffsTable( container_name );

		char *start_address = reinterpret_cast<char*>(pContainerTable->getActualStartAddress());
		size_t offset = reinterpret_cast<char*>(&table_value) - start_address;

		if( offset > pContainerTable->getTableSize() ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'",
															    table_name ) );
		}

		auto table = std::make_shared<TableWrapperOffsetStruct>( pContainerTable, offset );
		vTables.push_back( table );

		// remember the string in persitent storage
		table->setSubTableName( table_name );
		table->setAppendExecArgLater( true );

		return *this;
	}

	template <class T> CppSqlReaderImpl & selOffsetAttr( const std::string & table_name, SqlType & attr, long & lValue )
	{
		std::shared_ptr<TableWrapperBase> pTable = findOffsTable( table_name );

		if( pTable->getTableSize() != sizeof(T) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "table size of table '%s'(%d) is not '%s'(%ul)",
					table_name,
					pTable->getTableSize(),
					typeid(T).name(), // this returns a demangled name
					sizeof(T) ) );
		}

		char *start_address = reinterpret_cast<char*>(pTable->getActualStartAddress());
		size_t offset =  reinterpret_cast<char*>(&lValue) - start_address;

		if( offset > sizeof(T) ) {
			THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "field is not part of table '%s'",
					table_name ) );
		}

		CPPSQLREADER_IMPLEMENT_OFFSET_TYPE_CHECK( long )

		auto table = std::make_shared<TableWrapperOffset>( pTable, offset );

		vTables.push_back( table );
		void *value_start_address = table->getActualStartAddress();

		// #define SELOFSKPATTR(x,o) &Sql_KPATTR,_SELOFS,(__sqldef_longfunc(&(x))),(long)(o)
		int rv = _SqlExecArgsAppend( execArgs, &attr,_SELOFS,reinterpret_cast<long*>(value_start_address),(long)sizeof(T), NULL );
		if( rv < 0 ) {
			LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) );
			throw CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		return *this;
	}


#undef CPPSQLREADER_IMPLEMENT_OFFSET_SEL
#undef CPPSQLREADER_IMPLEMENT_SIMPLE_SQL
#undef CPPSQLREADER_IMPLEMENT_SIMPLE_SEL
#undef CPPSQLREADER_IMPLEMENT_OFFSET_TYPE_CHECK

	CppSqlReaderImpl & appendQuery( const std::string & stmt ) {
		if( !sql.empty() ) {
			sql += " ";
		}

		sql += stmt;
		return *this;
	}

	/**
	 * Read all data into the vector arrays at once.
	 * Returns the total amount, or 0.
	 * throws SQL_EXCEPTION()
	 */
	size_t execAll();

	/**
	 * Reads one data block. (BLOCKSIZE)
	 * The maximum size of all data vectors is BLOCKSIZE
	 * return false, if there are no data left
	 * throws SQL_EXCEPTION()
	 */
	bool execBlockwise();

private:
	std::shared_ptr<TableWrapperBase> findOffsTable( const std::string & table_name );

	void prepare();
};

// cleanup ugly macro stuff
#ifndef CPPSQLREADER_INTERNAL
#	undef THROW_CPPSQLREADER_REPORT_EXCEPTION
#	undef CPPSQLREADER_SQL_EXCEPTION
#endif
} // /namespace Tools;

#endif // __cplusplus >= 201103

#endif  /* _wamas_CPPSQLREADER_H */
