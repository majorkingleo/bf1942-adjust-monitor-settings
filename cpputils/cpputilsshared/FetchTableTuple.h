/**
 * FetchTableTuple: Something like FetchTable, the vector contains a tuple with all data
 * @author Copyright (c) 2022 SSI Schaefer IT Solutions
 *
 * Various examples can befound at cpputils test suite:
 * http://w4.prj.sources.wamas.com/cgit/cpputilstest.git/tree/test_cpputilsshared/test_cppsqlreader.cc
 *
 * Basic usage:
 *
 * 		FetchTableTuple<KPL,FES> vTable( FAC_DEFAULT, tid, "select %FES, %KPL from " TN_FES ", " TN_KPL
 *                                                         " where " TCN_FES_FeldId " = " TCN_KPL_FeldId );
 *      vTable.execAll();
 *
 * 		for( auto & data : vTable ) {
 *      	FES fes;
 *          KPL kpl;
 *
 *          std::tie( kpl, fes ) = data;
 *          std::cout << "tie: " << fes.fesFeldId               << ": " << kpl.kplArtNr << std::endl;
 *          std::cout << "get: " << std::get<1>(data).fesFeldId << ": " << std::get<0>(data).kplFeldId << std::endl;
 *      }
 *
 */

#ifndef _wamas_FETCHTABLETUPLE_H
#define _wamas_FETCHTABLETUPLE_H

#if __cplusplus > 201402

#include <CppSqlReader.h>
#include <FetchTableTuple.h>

namespace Tools {

struct FetchTableTupleStmt {};

template<int element,class Table> struct FetchTableTupleAppendPart
{
	template<class tInherit> void append( tInherit * parent, const std::string & table_name ) {

		Table* start_address = &std::get<element>(parent->operator[](0));

		parent->getSqlReader().selOffsetStruct( "sqlReaderData", table_name, *start_address );
	}
};


#define FetchTableTuple  CppSqlReaderBase::setSourceLocation( __FILE__, __LINE__ ); Tools::FetchTableTupleImpl

template <class ...Tables> class FetchTableTupleImpl : public std::vector< std::tuple<Tables...> >
{
protected:
	Tools::CppSqlReaderImpl sqlReader;
	typedef typename std::vector< std::tuple<Tables...> >::value_type VALUE_TYPE;

public:
	template<class ...TableNames> FetchTableTupleImpl( const std::string & fac, void *tid, TableNames... names )
	: std::vector< std::tuple<Tables...> >(),
	  sqlReader( fac, tid, std::string() )
	{
		sqlReader.addStruct4OffsetSelection( "sqlReaderData", *this );
		appendPart<0, Tables...,FetchTableTupleStmt>( names... );
	}

	CppSqlReaderImpl & getSqlReader() { return sqlReader; }

	size_t execAll() {
		return sqlReader.execAll();
	}

	bool execBlockwise() {
		return sqlReader.execBlockwise();
	}

private:

	template<int element, class Table,class ...Type, class ...TableNames> void appendPart( const std::string & table_name, TableNames... table_names  )
	{
		FetchTableTupleAppendPart<element,Table>().append( this, table_name );

		if constexpr (sizeof...(table_names) > 1) {
			appendPart<element+1, Type...>( table_names... );
		}
		else if constexpr (sizeof...(table_names) == 1 ) {
			sqlReader.appendQuery( table_names... );
		}
	}
};

} // /namespace Tools
#endif // __cplusplus > 201402

#endif  /* _wamas_FETCHTABLETUPLE_H */
