#ifndef AES_CRYPT_UTIL_H_
#define AES_CRYPT_UTIL_H_

#include <base/buser.h> 

#if (defined OWIL_VERSION && OWIL_VERSION >= 72)

/*
 * aes_crypt_util.h
 *
 *  Created on: 28.10.2015
 *      Author: mmattl
 */

#ifdef __cplusplus
#include <string>

/**
 * A simple wrapper class for Owil's crypto engine
 * EnCrypt/DeCrypt AES-128
 *
 */
class AesCrypt128 {


private:

	std::string fac;
	std::string key;

public:


	AesCrypt128 (const std::string &fac) : fac (fac), key ("!WAMASbySALOMON!") {

	}

	virtual ~AesCrypt128 () {}

	/**
	 * Encrypt the given string (AES-128)
	 *
	 * RETURNS: The crypted string
	 * THROWS: ReportException, in case of errors
	 */
	std::string encrypt (const std::string &plainStr);

	/**
	 * Decrypt the given string (AES-128)
	 *
	 * RETURNS: The decrypted plain-string
	 * THROWS: ReportException, if the string could not be decrypted
	 */
	std::string decrypt (const std::string &cryptedStr);


};

#endif // __cplusplus


#ifdef __cplusplus
extern "C" {
#endif


/**
 * C-compatible wrapper functions
 */
const char * encryptString (const char *pcFac,	// IN
			const char *pcPlainStr 				// IN
);
const char * decryptString (const char *pcFac,	// IN
			const char *pcCryptedStr			// IN
);

#ifdef __cplusplus
}
#endif

#endif // OWIL_VERSION

#endif /* AES_CRYPT_UTIL_H_ */
