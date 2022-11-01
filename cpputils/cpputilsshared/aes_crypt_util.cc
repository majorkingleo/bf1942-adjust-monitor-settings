/*
 * aes_crypt_utils.cc
 *
 *  Created on: 28.10.2015
 *  Author: mmattl
 */

/* ==========================================================================
 * INCLUDES
 * =========================================================================*/

#ifndef NOWAMAS

/* ------- Owil-Headers -------------------------------------------------- */
#include <base/buser.h> // needed: base/bowcryutil.h

/* ------- Tools-Headers -------------------------------------------------- */

#include <cpp_util.h>


#include "aes_crypt_util.h"

/* ==========================================================================
 * DEFINES AND MACROS
 * =========================================================================*/
using namespace std;
using namespace Tools;

#if (defined OWIL_VERSION && OWIL_VERSION >= 72)
/* ===========================================================================
 * LOCAL (STATIC) Variables and Function-Prototypes
 * =========================================================================*/

/* ------- Variables ----------------------------------------------------- */

/* ------------------------------------------------------------------------*/

std::string AesCrypt128::encrypt (const std::string &plainStr)
{

	char 		*pcCryptedStr = NULL;

	if (plainStr.empty ()) {
		throw REPORT_EXCEPTION ("Source string is empty");
	}

	LogPrintf (fac, LT_DEBUG, "Try to encrypt |%s|", plainStr);

	pcCryptedStr = OwCryptoEncryptString (BCryptoMethodNaes128,
			const_cast <OwByte *>(plainStr.c_str()), NULL,
			const_cast <OwByte *>(key.c_str()));

	if (pcCryptedStr == NULL) {
		throw REPORT_EXCEPTION (format (
				"Failed to crypt string |%s|", plainStr));
	}
	return string (pcCryptedStr);

}

std::string AesCrypt128::decrypt (const std::string &cryptedStr)
{

	char        *pcPlainStr = NULL;

	if (cryptedStr.empty ()) {
		throw REPORT_EXCEPTION ("Crypted source-string is empty");
	}

	pcPlainStr = OwCryptoDecryptString (BCryptoMethodNaes128,
			const_cast <OwByte *>(cryptedStr.c_str()),
			const_cast <OwByte *>(key.c_str()));

	if (pcPlainStr == NULL) {
		throw REPORT_EXCEPTION (format (
				"Failed to decode string |%s|", cryptedStr));
	}

	return string (pcPlainStr);

}

// C-compatible functions

const char *encryptString (const char *pcFac, const char *pcPlainStr) {


	if (pcFac == NULL) {
		pcFac = "AesCrypt128";
	}


	static string out;
	out.erase();

	try {
		AesCrypt128 engine (pcFac);
		out = engine.encrypt(pcPlainStr);

		return TO_CHAR(out);

	} catch (const exception & err) {

		LogPrintf (pcFac, LT_ALERT, "encryptString(): %s", err.what());
		return NULL;
	}

	return NULL;

}

const char *decryptString (const char *pcFac, const char *pcCryptedStr) {

	if (pcFac == NULL) {
		pcFac = "AesCrypt128";
	}

	static string out;
	out.erase();

	try {
		AesCrypt128 engine (pcFac);
		out = engine.decrypt(pcCryptedStr);

		return TO_CHAR(out);

	} catch (const exception & err) {
		LogPrintf (pcFac, LT_ALERT, "decryptString(): %s", err.what());
		return NULL;
	}

	return NULL;
}

#endif // OWIL_VERSION
#endif
