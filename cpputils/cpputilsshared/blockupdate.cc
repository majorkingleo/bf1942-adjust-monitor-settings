#ifndef NOWAMAS
/*****************************************************************************
+* PROJECT:   projectname
+* PACKAGE:   package name
+* FILE:      blockupdate.c
+* CONTENTS:  overview description, list of functions, ...
+* COPYRIGHT NOTICE:
+*         (c) Copyright 2002 by
+*                 Salomon Automationstechnik Ges.m.b.H
+*                 Friesachstrasse 15
+*                 A-8114 Friesach bei Graz
+*                 Tel.: ++43 3127 200-0
+*                 Fax.: ++43 3127 200-22
+****************************************************************************/

/* ===========================================================================
 * INCLUDES
 * =========================================================================*/

/* ------- System-Headers ------------------------------------------------- */

/* ------- Owil-Headers --------------------------------------------------- */

/* ------- Tools-Headers -------------------------------------------------- */

#include <dbsql.h>
#include <sqltable.h>
#include <dbsqlstd.h>
/* ------- Local-Headers -------------------------------------------------- */

#include <logtool2.h>
#include "cpp_util.h"

#define _BLOCKUPDATE_C
#include "blockupdate.h"
#undef _BLOCKUPDATE_C

using namespace Tools;

/* ===========================================================================
 * LOCAL DEFINES AND MACROS
 * =========================================================================*/


/* ===========================================================================
 * LOCAL TYPEDEFS (ENUMS, STRUCTURES, ...)
 * =========================================================================*/

 /* ------- Enums --------------------------------------------------------- */

 /* ------- Structures ---------------------------------------------------- */

/* ===========================================================================
 * LOCAL (STATIC) Variables and Function-Prototypes
 * =========================================================================*/

 /* ------- Variables ----------------------------------------------------- */

 /* ------- Function-Prototypes ------------------------------------------- */

/* ===========================================================================
 * GLOBAL VARIABLES
 * =========================================================================*/

/* ===========================================================================
 * LOCAL (STATIC) Functions
 * =========================================================================*/

/*----------------------------------------------------------------------------
-* SYNOPSIS
-*      $$ (automatically replaced with function prototype by autodoc-tool)
-* DESCRIPTION
-* RETURNS
-*--------------------------------------------------------------------------*/

/* ===========================================================================
 * GLOBAL (PUBLIC) Functions
 * =========================================================================*/

/*----------------------------------------------------------------------------
-* SYNOPSIS
-*      $$ (automatically replaced with function prototype by autodoc-tool)
-* DESCRIPTION
-* RETURNS
        -1 ... Error
         1 ... OK
-*--------------------------------------------------------------------------*/
int BlockTExecStdSqlX(void *pvTid, const char *pcFac, SqlTstdStmtRes iReason,
					  const char *pcTableName, void *pvData, size_t nEle)
{       

  int  iDbRv2 = 0, iStructSize;
  size_t nAnz = 0, nI = 0;
#if (defined TOOLS_VERSION && TOOLS_VERSION < 37)
  const size_t MAX_BLOCKSIZE=5000;
#else 
  const size_t MAX_BLOCKSIZE=SqlModuleGetMaxBlockSize(
							SqlIfGetModule(DbSqlNameGetSqlIf (NULL)));
#endif

  if (pcFac == NULL) {
      pcFac = "BlockTExecStdSqlX";
  }	

  if (iReason != StdNinsert &&
	  iReason != StdNupdate &&
	  iReason != StdNdelete) {
	
	LogPrintf(pcFac, LT_ALERT, "BlockTExecStdSqlX: ERROR wrong reason!!!");
	return -1;
  }
  
  if (nEle < 1 || pvData == NULL || pcTableName == NULL) {
	LogPrintf(pcFac, LT_ALERT, "BlockTExecStdSqlX: ERROR wrong data!!!");
	return -1;
  }
  
  iStructSize = getStructSize(TO_CHAR(pcTableName));
  if (iStructSize < 0) {
	LogPrintf(pcFac, LT_ALERT,
			  "BlockTExecStdSqlX: ERROR getStructSize for table '%s'",
			  pcTableName);
	return -1;
  }
  
  do {
	
	if ((nEle - nI) > MAX_BLOCKSIZE) {
		nAnz = MAX_BLOCKSIZE;
	} else {
		nAnz = nEle - nI;
	}
	
	/* --- 10000, 15000, 20000, ... --- */
	if (nAnz == 0) {
	  break;
	}
	
	/* --- nächsten Block verwenden --- */
	if (iDbRv2 != 0) {
	  pvData = ((char*)(pvData)) + (MAX_BLOCKSIZE * iStructSize);
	}
	
	iDbRv2 = TExecStdSqlX(pvTid, NULL, iReason, (char*)pcTableName,
						  pvData,
						  (unsigned int)nAnz,
						  NULL,
						  NULL);
	
	if ((int)nAnz != iDbRv2) {
	  LogPrintf(pcFac, LT_ALERT,
			"BlockTExecStdSqlX: Error ins/upd/del %s. "
			"[nAnz=%u <> iDbRv2=%d] [SqlErrTxt:%s] ",
			pcTableName,
			(unsigned int)nAnz,
			iDbRv2,
			TSqlErrTxt (pvTid));
	  return (-1);
	}
	
	LogPrintf (pcFac, LT_ALERT,
			   "BlockTExecStdSqlX: %u %s ins/upd/del",
			   (unsigned int)nAnz,
			   pcTableName);
	
	/* --- 5000 --- */
	if (nEle == MAX_BLOCKSIZE) {
	  break;
	}
	
	nI += MAX_BLOCKSIZE;
	
  } while (iDbRv2 == (int)MAX_BLOCKSIZE);
  
  return (1);
}
  
#endif /* NOWAMAS */
