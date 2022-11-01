/**
 * FetchTableInherit: Something like FetchTable
 * @author Copyright (c) 2022 SSI Schaefer IT Solutions
 *
 * Various examples can befound at cpputils test suite:
 * http://w4.prj.sources.wamas.com/cgit/cpputilstest.git/tree/test_cpputilsshared/test_cppsqlreader.cc
 *
 * Basic usage:
 *
 *		// creates an object that inherits from std::vector< Object inherit from { KPL, FES } >
 *		FetchTableInherit<KPL,FES> vTable( FAC_DEFAULT, tid,
 *									       TN_KPL, TN_FES,
 *									       "select %KPL, %FES from " TN_FES ", " TN_KPL
 *										   " where " TCN_FES_FeldId " = " TCN_KPL_FeldId );
 *		vTable.execAll(); // read all data
 *
 *		for( auto & data : vTable ) {
 *		  // since data inherits from KPL and FES you can access both members
 *		  std::cout << data.fesFeldId << ": " << data.kplArtNr << std::endl;
 *
 *		  // automatically upcasting is no problem
 *		  KPL kpl = data;
 *		  FES fes = data;
 *	    }
 *
 *		// extract all KPL date from vTable and create a vector.
 *		// of course this is a copy
 *		std::vector<KPL> vKPL = vTable;
 *
 */

#ifndef _wamas_FETCHTABLEINHERIT_H
#define _wamas_FETCHTABLEINHERIT_H

#if __cplusplus >= 201103

#include <CppSqlReader.h>

namespace Tools {

template <class ...Tables> struct FetchTableInheritData : public Tables...
{

};

struct FetchTableInheritStmt {};

template<class Table> struct FetchTableInheritAppendPart
{
	template<class tInherit> void append( tInherit * parent, const std::string & table_name ) {

		Table* start_address = &(parent->operator[](0));

		parent->getSqlReader().selOffsetStruct( "sqlReaderData", table_name, *start_address );
	}
};

template<> struct FetchTableInheritAppendPart<FetchTableInheritStmt>
{
	template<class tInherit> void append( tInherit * parent, const std::string & stmt ) {
		parent->getSqlReader().appendQuery( stmt );
	}
};

#define FetchTableInherit  CppSqlReaderBase::setSourceLocation( __FILE__, __LINE__ ); Tools::FetchTableInheritImpl

template <class ...Tables> class FetchTableInheritImpl : public std::vector< FetchTableInheritData<Tables...> >
{
protected:
	Tools::CppSqlReaderImpl sqlReader;

public:
	template<class ...TableNames> FetchTableInheritImpl( const std::string & fac, void *tid, TableNames... names )
	: std::vector< FetchTableInheritData<Tables...> >(),
	  sqlReader( fac, tid, std::string() )
	{
		sqlReader.addStruct4OffsetSelection( "sqlReaderData", *this );
		appendPart<Tables...,FetchTableInheritStmt>( names... );
	}

	CppSqlReaderImpl & getSqlReader() { return sqlReader; }

	size_t execAll() {
		return sqlReader.execAll();
	}

	bool execBlockwise() {
		return sqlReader.execBlockwise();
	}

	/* this makes a copy */
	template<class T> operator std::vector<T> () {
		std::vector<T> vRet;
		vRet.reserve(this->size());

		for( auto & data : *this ) {
			vRet.push_back( data );
		}

		return vRet;
	}


private:

#if __cplusplus <= 201402
	template<class ...Types> void appendPart()
	{}
#endif

	template<class Table,class ...Type, class ...TableNames> void appendPart( const std::string & table_name, TableNames... table_names  )
	{
		FetchTableInheritAppendPart<Table>().append( this, table_name );

#if __cplusplus > 201402
		if constexpr (sizeof...(table_names) > 0) {
			appendPart<Type...>( table_names... );
		}
#else
		appendPart<Type...>( table_names... );
#endif
	}

};

} // /namespace Tools
#endif // __cplusplus >= 201103

#endif  /* _wamas_FETCHTABLEINHERIT_H */
