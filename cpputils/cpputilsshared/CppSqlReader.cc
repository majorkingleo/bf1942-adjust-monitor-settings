/**
 * @file
 * @todo describe file content
 * @author Copyright (c) 2022 SSI Schaefer IT Solutions
 */
#if __cplusplus >= 201103
#define CPPSQLREADER_INTERNAL 	1
#include "CppSqlReader.h"

namespace Tools {

const char *CppSqlReaderBase::pcSourceFile = "";
int CppSqlReaderBase::iSourceLine = 0;

void CppSqlReaderBase::setSourceLocation( const char *pcSource, int iLine )
{
	pcSourceFile = pcSource;
	iSourceLine = iLine;
}

CppSqlReaderBase::CppSqlReaderBase( const std::string & fac_, void *tid_, const std::string & sql_ )
: tid( tid_ ),
  fac( fac_ ),
  sql( sql_ ),
  ctxt( tid_ ),
  execArgs(DbSqlTidCreateSqlExecArgs(const_cast<void*>(tid_))),
  sourceFile( pcSourceFile ),
  sourceLine( iSourceLine ),
  rvBlockwise(0)
{
	if( !ctxt ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot create SqlContext. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}

	if( !execArgs ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot create execArgs. SqlError: %s", sourceFile, sourceLine, TSqlErrTxt(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}
}

CppSqlReaderBase::~CppSqlReaderBase()
{
	SqlExecArgsDestroy(execArgs);
}

CppSqlReaderImpl::TableWrapperBase::TableWrapperBase( const std::string & table_name_, size_t table_size_ )
: table_name( table_name_ ),
  have_exec_arg( true ),
  table_size( table_size_ ),
  append_exec_arg_later( false )
{
}


CppSqlReaderImpl::TableWrapperBase::~TableWrapperBase()
{
}

CppSqlReaderImpl::TableWrapperOffset::TableWrapperOffset( std::shared_ptr<TableWrapperBase> & parentTable_, size_t offset_ )
: TableWrapperBase( "TableWrapperOffset" ),
  parentTable( parentTable_ ),
  offset( offset_ ),
  sub_table_name()
{

}

void* CppSqlReaderImpl::TableWrapperOffset::getActualStartAddress()
{
	char *start_address = reinterpret_cast<char*>(parentTable->getActualStartAddress());
	return start_address + offset;
}

void CppSqlReaderImpl::TableWrapperOffset::finish()
{
}


void CppSqlReaderImpl::TableWrapperOffset::finishWithSize( size_t size )
{
}

void CppSqlReaderImpl::TableWrapperOffset::moveIdx( size_t incIdx )
{
}

void CppSqlReaderImpl::TableWrapperOffsetStruct::appendExecArg( CppSqlReaderBase *sqlReader )
{
										   // #define SELOFSSTRUCT(n,x,o)
										   // STRUCTCODE,_SELOFS,&(x),n,NULL,0,(long)o
	int rv = _SqlExecArgsAppend( sqlReader->getExecArgs(),
								 STRUCTCODE,
								 _SELOFS,
								 getActualStartAddress(),
								 getSubTableName().c_str(),
								 NULL,
								 0,
								 (long)parentTable->getTableSize(),
								 NULL );

	if( rv < 0 ) {

		std::string sourceFile = sqlReader->getSourceFile();
		int sourceLine = sqlReader->getSourceLine();


		throw CPPSQLREADER_SQL_EXCEPTION( sqlReader->getTid() );
	}
}

CppSqlReaderImpl::TableWrapperString::TableWrapperString( std::vector<std::string> & vData_, const size_t BLOCKSIZE_, const size_t FIELD_LEN_ )
: vData( vData_),
  vBuffer(),
  BLOCKSIZE( BLOCKSIZE_ ),
  FIELD_LEN( FIELD_LEN_ ),
  idx(0)
{
	vData.clear();
	vBuffer.resize( BLOCKSIZE * FIELD_LEN );
}

void CppSqlReaderImpl::TableWrapperString::moveIdx( size_t incIdx )
{
	for( size_t i = 0; i < incIdx; i++ ) {
		vData.push_back( &vBuffer[FIELD_LEN*i] );
	}
}

void* CppSqlReaderImpl::TableWrapperString::getActualStartAddress()
{
	return &vBuffer[0];
}

void CppSqlReaderImpl::TableWrapperString::finish()
{
}

void CppSqlReaderImpl::TableWrapperString::finishWithSize( size_t size )
{
	vData.resize(size);
}


CppSqlReaderImpl::CppSqlReaderImpl( const std::string & fac_, void *tid_, const std::string & sql_, const size_t BLOCKSIZE_ )
: CppSqlReaderBase( fac_, tid_, sql_ ),
  BLOCKSIZE( BLOCKSIZE_ ),
  vTables()
{
	if( SqlExecArgsSetArrayLen(execArgs, static_cast<int>(BLOCKSIZE), 0) < 0 ) {
		THROW_CPPSQLREADER_REPORT_EXCEPTION( format("Cannot set array length to %d", BLOCKSIZE ) );
	}
}

std::shared_ptr<CppSqlReaderImpl::TableWrapperBase> CppSqlReaderImpl::findOffsTable( const std::string & table_name )
{
	for( auto & pTable : vTables ) {
		if( pTable->getName() == table_name ) {
			return pTable;
		}
	}

	THROW_CPPSQLREADER_REPORT_EXCEPTION( Tools::format( "cannot find table named '%s' in vOffsetTables."
														" Have you registered it via selOfsStruct()?", table_name ));
}

CppSqlReaderImpl & CppSqlReaderImpl::selStr( std::vector<std::string> & vTable, const size_t size )
{
	auto table = std::make_shared<TableWrapperString>( vTable, BLOCKSIZE, size );
	vTables.push_back( table );

	int rv = _SqlExecArgsAppend( execArgs, SELSTR( table->getStartAddress(), size ), NULL );
	if( rv < 0 ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlError(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}

	return *this;
}

CppSqlReaderImpl & CppSqlReaderImpl::selAttr( SqlType & attr, std::vector<long> & vTable )
{
	auto table = std::make_shared<TableWrapper<long>>( vTable, BLOCKSIZE );
	vTables.push_back( table );

	int rv = _SqlExecArgsAppend( execArgs, &attr,_SELECT, &table->vData[0], NULL );
	if( rv < 0 ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlError(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}

	return *this;
}

CppSqlReaderImpl & CppSqlReaderImpl::sqlStr( const std::string & str )
{
	int rv = _SqlExecArgsAppend( execArgs, SQLSTRING( str.c_str() ), NULL );
	if( rv < 0 ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlError(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}
	return *this;
}

CppSqlReaderImpl & CppSqlReaderImpl::sqlStr( const char * str )
{
	int rv = _SqlExecArgsAppend( execArgs, SQLSTRING( str ), NULL );
	if( rv < 0 ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d Cannot append execArgs. SqlError: %s", sourceFile, sourceLine, TSqlError(tid) );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}
	return *this;
}

size_t CppSqlReaderImpl::execAll()
{
	prepare();

	int rv = 0;
	size_t total = 0;

	LogPrintf( fac, LT_TRACE, "Sql: %s", sql );

	do {
		rv = rv == 0 ? _TExecSqlArgsV(tid, ctxt, sql.c_str(), execArgs )
					 : _TExecSqlArgsV(tid, ctxt, 0, 0 );

		if( rv <= 0 && TSqlError(tid) != SqlNotFound ) {
			LogPrintf( fac, LT_ALERT, "from: %s:%d SqlError: %s Stmt: %s", sourceFile, sourceLine, TSqlError(tid), sql );
			throw CPPSQLREADER_SQL_EXCEPTION( tid );
		}

		SqlTbindListPtr sellist = SqlExecArgsGetSelectList(execArgs);
		SqlTbindDescPtr act = sellist->list;

		if( rv > 0 ) {
			total += rv;

			for( auto pvTable : vTables ) {

				pvTable->moveIdx( static_cast<size_t>(rv) );

				if( !pvTable->haveExecArg() ) {
					continue;
				}

				act->location.address = pvTable->getActualStartAddress();
				act++;
			}
		}

	} while( rv == static_cast<int>(BLOCKSIZE) );

	for( auto pvTable : vTables ) {
		pvTable->finish();
	}

	return total;
}

bool CppSqlReaderImpl::execBlockwise()
{
	prepare();

	// last block already read
	if( rvBlockwise != 0 && rvBlockwise < static_cast<int>(BLOCKSIZE) ) {
		return false;
	}

	if( rvBlockwise == 0 ) {
		rvBlockwise = _TExecSqlArgsV(tid, ctxt, sql.c_str(), execArgs );
	} else {
		rvBlockwise = _TExecSqlArgsV(tid, ctxt, 0, 0 );
	}

	if( rvBlockwise <= 0 && TSqlError(tid) != SqlNotFound ) {
		LogPrintf( fac, LT_ALERT, "from: %s:%d SqlError: %s Stmt: %s", sourceFile, sourceLine, TSqlError(tid), sql );
		throw CPPSQLREADER_SQL_EXCEPTION( tid );
	}

	if( rvBlockwise <= 0 && TSqlError(tid) == SqlNotFound ) {
		rvBlockwise = 0;
	}

	if( rvBlockwise < static_cast<int>(BLOCKSIZE) ) {
		for( auto pvTable : vTables ) {
			pvTable->finishWithSize( rvBlockwise );
		}
	}

	return (rvBlockwise > 0);
}

void CppSqlReaderImpl::prepare()
{
	class OffsetSorter
	{
	public:
		bool operator()( std::shared_ptr<TableWrapperBase> a, std::shared_ptr<TableWrapperBase> b ) {
			TableWrapperOffset *ao = dynamic_cast<TableWrapperOffset*>(a.get());
			TableWrapperOffset *bo = dynamic_cast<TableWrapperOffset*>(b.get());

			return ao->getOffset() < bo->getOffset();
		}
	};

	std::map<std::string,std::vector<std::shared_ptr<TableWrapperBase>>> tables_by_container_name;

	// this is required mostly ba FetchTableTuple, because the component order inside
	// the tuple is backwards to the notation when using gcc

	// reorder all TableWrapperOffset that contain a substruct by offset ascending
	// otherwise the sql statement will stuck
	for( auto & table_wrapper : vTables ) {
		if( table_wrapper->willAppendExecArgLater() ) {
			tables_by_container_name[table_wrapper->getName()].push_back( table_wrapper );
		}
	}

	for( auto & pair : tables_by_container_name ) {
		auto & tables = pair.second;
		std::sort( tables.begin(), tables.end(), OffsetSorter() );
	}

	// remove all those exec args from vTable
	TABLES_CONT reordered_tables;

	for( auto & table_wrapper : vTables ) {
		if( !table_wrapper->willAppendExecArgLater() ) {
			reordered_tables.push_back( table_wrapper );
		}
	}

	for( auto & pair : tables_by_container_name ) {
		 auto & tables = pair.second;
		 for( auto & table_wrapper : tables ) {
			 reordered_tables.push_back( table_wrapper );
		 }
	}

	vTables = reordered_tables;

	for( auto & table_wrapper : vTables ) {
		if( table_wrapper->willAppendExecArgLater() ) {
			table_wrapper->appendExecArg( this );
		}
	}
}

} // /namespace Tools
#endif // __cplusplus >= 201103

