#ifndef LOCKING_UTILS_H_
#define LOCKING_UTILS_H_


#include <tsleep.h>
#include <dbsqlstd.h>
#include <sqltable.h>
#include <mlmsg.h>
#include <cpp_util.h>

/*
 * locking_utils.h
 *
 *  Created on: 11.06.2014
 *      Author: mmattl
 */

#ifdef FIX_BUG_20809
#  define WORKAROUND_BUG_20809_TExecSql _TExecSql
#  define WORKAROUND_BUG_20809_TExecStdSql _TExecStdSql
#else
#  define WORKAROUND_BUG_20809_TExecSql TExecSql
#  define WORKAROUND_BUG_20809_TExecStdSql TExecStdSql
#endif


/*
 *
 * Trying to lock a record (or some columns of it) several times
 *
 *
 * Example without whereStmt:
 *
 * TEK		tTek;
 * strcpy (tTek.tekTeId, myTeId);
 *
 * LockSingleRecord<TEK> record (pvTid, pcFac, TN_TEK, tTek);
 *
 * NOTE: In this case, tTek must be pre-filled with PK!
 *
 *-----------------------------------------------------------------------
 *
 * Example with whereStmt:
 *
 * TPA		tTpa;
 * const string whereStmt = format ("where "TCN_TPA_TaNr" = %ld "
 * 	" for update of "TCN_TPA_Status","TCN_TPA_Hist_AeZeit","
 * 		TCN_TPA_Hist_AeUser" nowait ", lTaNr);
 *
 * LockSingleRecord<TEK> record (pvTid, pcFac, TN_TPA, tTpa, whereStmt);
 * or
 * LockSingleRecord<TEK> record (pvTid, pcFac, TN_TPA, tTpa,
 * 	whereStmt, 10, 100);
 *
 *-------------------------------------------------------------------------
 *
 * THROWS exceptions in case of errors - client must catch it
 *
 */

template <class Table> class LockSingleRecord {

private:

	const void 	*pvTid;			// TID
	std::string	fac;			// Log-Facility
	std::string	tableName;		// TableName, e.g. TN_TEK
	Table		&table;			// reference to struct
	std::string	whereStmt;		// optional: whereCnd and columns to lock
	short		attempts;		// optional: max. attempts
	long		timeout;		// optional: wait time between attemps (in ms.)
	bool        all_cols;       // optional: all columns known by the database


	LockSingleRecord & operator=( const LockSingleRecord<Table> & data ) {
		return *this;
	}

	LockSingleRecord <Table> (const LockSingleRecord<Table> & data) {

	}

	void exec () {

		std::string lockWithColsStmt;


		if( all_cols ) {
		    lockWithColsStmt = Tools::format ("select %%%s, %s.* from %s %s",tableName, tableName, tableName, whereStmt);
		} else {
		    lockWithColsStmt = Tools::format ("select %%%s from %s %s",tableName, tableName, whereStmt);
		}

		int	 	dbrv = -1;
		bool 	success = false;
		for (int idx = 0; idx < attempts; idx++) {

			LogPrintf (fac, LT_TRACE,
					"Trying to select for update (table %s), attempt #%d",
					tableName, idx+1);


			if (whereStmt.empty()) {

				// No memset (!)

				dbrv = WORKAROUND_BUG_20809_TExecStdSql (const_cast<void*>(pvTid), StdNselectUpdNo, TO_CHAR(tableName), &table);

			} else {

				size_t	structSize = getStructSize (TO_CHAR(tableName));

				memset (&table, 0, structSize);

				dbrv = WORKAROUND_BUG_20809_TExecSql (const_cast<void*>(pvTid),
						TO_CHAR(lockWithColsStmt),
						SELSTRUCT (TO_CHAR(tableName), table),
						NULL);
			}

			if (dbrv <= 0) {

				if (TSqlError (const_cast<void*>(pvTid)) == SqlNotFound) {
					throw REPORT_EXCEPTION (MlMsg("Kein Eintrag gefunden"));
				} else if (TSqlError (const_cast<void*>(pvTid)) == SqlLocked) {
					tsleep (timeout);
					continue;
				} else {
					throw SQL_EXCEPTION (const_cast<void*>(pvTid));
				}

			} else {
				success = true;
				LogPrintf (fac, LT_TRACE, "Success at attempt #%d", idx+1);
				break;
			}

		}

		if (success == false) {
			throw REPORT_EXCEPTION (Tools::format (MlMsg ("Failed to lock records of table %s after %d attempts"),
					tableName, attempts));
		}
	}


public:

	LockSingleRecord <Table> (const void *pvTid, const std::string fac,
			const std::string tableName, Table &table, const std::string whereStmt = "",
			const short attempts = 5, const long timeout = 200,
			bool all_columns = false ) :

			pvTid(pvTid), fac(fac), tableName(tableName), table(table),
			whereStmt(whereStmt),
			attempts(attempts),
			timeout(timeout),
			all_cols( all_columns ){

		exec ();

	}

};


/*
 *
 * Trying to insert a record several times
 *
 *
 * Example without whereStmt:
 *
 * TEK		tTek;
 *
 * InsertSingleRecord<TEK> record (pvTid, pcFac, TN_TEK, tTek);
 *
 * NOTE: In this case, tTek must be pre-filled with PK!
 *
 * THROWS exceptions in case of errors - client must catch it
 *
 */

template <class Table> class InsertSingleRecord {

private:

	const void 	*pvTid;			// TID
	std::string	fac;			// Log-Facility
	std::string	tableName;		// TableName, e.g. TN_TEK
	Table		&table;			// reference to struct
	short		attempts;		// optional: max. attempts
	long		timeout;		// optional: wait time between attemps (in ms.)



	InsertSingleRecord & operator=( const InsertSingleRecord<Table> & data ) {
		return *this;
	}

	InsertSingleRecord <Table> (const  InsertSingleRecord<Table> & data) {

	}

	void exec () {

		int	 	dbrv = -1;
		bool 	success = false;
		for (int idx = 0; idx < attempts; idx++) {

			LogPrintf (fac, LT_TRACE,
					"Trying to insert (table %s), attempt #%d",
					tableName, idx+1);

			dbrv = WORKAROUND_BUG_20809_TExecStdSql (const_cast<void*>(pvTid), StdNinsert, TO_CHAR(tableName), &table);


			if (dbrv <= 0) {

				if (TSqlError (const_cast<void*>(pvTid)) == SqlLocked) {
					tsleep (timeout);
					continue;
				} else {
					throw SQL_EXCEPTION (const_cast<void*>(pvTid));
				}

			} else {
				success = true;
				LogPrintf (fac, LT_TRACE, "Success at attempt #%d", idx+1);
				break;
			}

		}

		if (success == false) {
			throw REPORT_EXCEPTION (Tools::format (MlMsg ("Failed to insert into table %s after %d attempts"),
					tableName, attempts));
		}
	}


public:

	InsertSingleRecord <Table> (const void *pvTid, const std::string & fac,
			const std::string & tableName, Table &table,
			const short attempts = 5, const long timeout = 200 ) :

			pvTid(pvTid), fac(fac), tableName(tableName), table(table),
			attempts(attempts),
			timeout(timeout) {

		exec ();

	}

};

#endif /* LOCKING_UTILS_H_ */
