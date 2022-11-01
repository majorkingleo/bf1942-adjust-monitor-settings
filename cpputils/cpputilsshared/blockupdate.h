#ifndef _BLOCKUPDATE_H
#define _BLOCKUPDATE_H
#ifndef NOWAMAS

/*
 * Blockupdate utility functions that will return a defined state
 * @author Copyright (c) 2022 SSI Schaefer IT Solutions
 */

#ifdef __cplusplus
# include <string>
# include <vector>
# include <cpp_util.h>
#endif

#include <dbsql.h>

#if (defined TOOLS_VERSION && TOOLS_VERSION <= 33)
#define SqlTstdStmtRes sqlNstmtRes
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------------------------------------
 * Updates a block of table data even if nEle is larger than the configured limit by tools
 * Any error will by logged to pcFac.
 *
 * eg:
 * if( BlockTExecStdSql( tid, fac, StdNinsert, TN_TPA, atTpa, num_tpa ) != 1 ) {
 *   LogPrintf( fac, LT_ALERT, "failed inserting %ld tpas. SqlError: %s", num_tpa, TSqlErrTxt(tid) );
 * 	 return -1;
 * }
 *
 *
 * iReason:     StdNinsert, StdNupdate, StdNdelete. All other reasons are invalid. -1 will be returned.
 * pcTableName: TN_XXX
 * pvData:      pointer to the data
 * nEle:		number of elements
 *
 * RETURNS
 *      -1 ... Error
 *       1 ... OK
 *---------------------------------------------------------------------------------------------------------*/
int BlockTExecStdSqlX(void *pvTid, const char *pcFac, SqlTstdStmtRes iReason,
					  const char *pcTableName, void *pvData, size_t nEle);

#if (defined TOOLS_VERSION && TOOLS_VERSION <= 33)
int getStructSize(char *pcTable);
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/* Absicht, dies soll einen Compilerfehler provozieren! */
template<class T> int BlockTExecStdSqlX(void *pvTid, const void*, SqlTstdStmtRes iReason,
										const std::string & pcTableName, std::vector<T> & ptData );


/*---------------------------------------------------------------------------------------------------------
 * Updates a block of table data even if nEle is larger than the configured limit by tools
 * Any error will by logged to pcFac.
 *
 * eg:
 * if( BlockTExecStdSql( tid, fac, StdNinsert, TN_TPA, vTpa ) != 1 ) {
 *   LogPrintf( fac, LT_ALERT, "failed inserting %ld tpas. SqlError: %s", num_tpa, TSqlErrTxt(tid) );
 * 	 return -1;
 * }
 *
 *
 * iReason:     StdNinsert, StdNupdate, StdNdelete. All other reasons are invalid. -1 will be returned.
 * pcTableName: TN_XXX
 * ptData:		all vector elements will be inserted
 *
 * RETURNS
 *      -1 ... Error
 *       1 ... OK
 *---------------------------------------------------------------------------------------------------------*/
template<class T> int BlockTExecStdSqlX(void *pvTid, const std::string & pcFac, SqlTstdStmtRes iReason,
										const std::string & pcTableName, std::vector<T> & ptData )
{
  return BlockTExecStdSqlX( pvTid, pcFac.c_str(), iReason, pcTableName.c_str(), (void*) &ptData[0], ptData.size() );
}


/*---------------------------------------------------------------------------------------------------------
 * Updates a block of table data even if nEle is larger than the configured limit by tools
 * Any error will by logged to pcFac.
 *
 * eg:
 * if( BlockTExecStdSql( tid, fac, StdNinsert, TN_TPA, vTpa ) != 1 ) {
 *   LogPrintf( fac, LT_ALERT, "failed inserting %ld tpas. SqlError: %s", num_tpa, TSqlErrTxt(tid) );
 * 	 return -1;
 * }
 *
 *
 * iReason:     StdNinsert, StdNupdate, StdNdelete. All other reasons are invalid. -1 will be returned.
 * pcTableName: TN_XXX
 * ptData:		all vector elements will be inserted
 *
 * RETURNS
 *      -1 ... Error
 *       1 ... OK
 *---------------------------------------------------------------------------------------------------------*/
template<class T> int BlockTExecStdSqlX(void *pvTid, const std::string & pcFac, SqlTstdStmtRes iReason,

										const std::string & pcTableName, Tools::FetchTable<T> & ptData )
{
  return BlockTExecStdSqlX( pvTid, pcFac.c_str(), iReason, pcTableName.c_str(), (void*) &ptData[0], ptData.size() );
}


/*---------------------------------------------------------------------------------------------------------
 * Updates a block of table data even if nEle is larger than the configured limit by tools
 * Any error will by logged to pcFac.
 *
 * eg:
 * if( BlockTExecStdSql( tid, fac, StdNinsert, TN_TPA, vTpa ) != 1 ) {
 *   LogPrintf( fac, LT_ALERT, "failed inserting %ld tpas. SqlError: %s", num_tpa, TSqlErrTxt(tid) );
 * 	 return -1;
 * }
 *
 *
 * iReason:     StdNinsert, StdNupdate, StdNdelete. All other reasons are invalid. -1 will be returned.
 * pcTableName: TN_XXX
 * ptData:		all vector elements will be inserted
 *
 * RETURNS
 *      -1 ... Error
 *       1 ... OK
 *---------------------------------------------------------------------------------------------------------*/
template<class T> int BlockTExecStdSqlX(void *pvTid, const char *pcFac, SqlTstdStmtRes iReason,
										 const char *pcTableName, std::vector<T> & ptData )
{
  return BlockTExecStdSqlX( pvTid, pcFac, iReason, pcTableName, (void*) &ptData[0], ptData.size() );
}


/*---------------------------------------------------------------------------------------------------------
 * Updates a block of table data even if nEle is larger than the configured limit by tools
 * Any error will by logged to pcFac.
 *
 * eg:
 * if( BlockTExecStdSql( tid, fac, StdNinsert, TN_TPA, vTpa ) != 1 ) {
 *   LogPrintf( fac, LT_ALERT, "failed inserting %ld tpas. SqlError: %s", num_tpa, TSqlErrTxt(tid) );
 * 	 return -1;
 * }
 *
 *
 * iReason:     StdNinsert, StdNupdate, StdNdelete. All other reasons are invalid. -1 will be returned.
 * pcTableName: TN_XXX
 * ptData:		all vector elements will be inserted
 *
 * RETURNS
 *      -1 ... Error
 *       1 ... OK
 *---------------------------------------------------------------------------------------------------------*/
template<class T> int BlockTExecStdSqlX(void *pvTid, const char *pcFac, SqlTstdStmtRes iReason,
										const std::string & pcTableName, std::vector<T> & ptData )
{
  return BlockTExecStdSqlX( pvTid, pcFac, iReason, pcTableName.c_str(), (void*) &ptData[0], ptData.size() );
}


#endif // ifdef __cplusplus

#endif /* NOWAMAS */
#endif  /* _BLOCKUPDATE_H */
