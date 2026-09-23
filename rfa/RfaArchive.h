/**
 * Reader for Battlefield 1942 .rfa archives.
 *
 * @author Copyright (c) 2026
 */

#ifndef RFA_RFAARCHIVE_H
#define RFA_RFAARCHIVE_H

#include "RfaFormat.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace rfa {

/**
 * Read-only view of an archive's directory table.
 *
 * open() reads the table plus every entry's chunk descriptor table, but never a payload,
 * so opening a 741 MB texture.rfa is cheap and memory use stays proportional to the
 * entry count rather than the file size. Payloads are read on demand via PayloadReader.
 *
 * Paths are narrow strings. Non-ASCII paths would need the wide-char APIs on Windows;
 * that is a known limitation, not an oversight.
 */
class RfaArchive
{
public:
	RfaArchive() = default;

	/**
	 * Parse the archive.
	 *
	 * @param error  when this returns false, receives the reason
	 * @return false when the file is structurally unusable. Recoverable oddities do not
	 *         fail the call - they are collected in problems().
	 */
	bool open( const std::string & path, std::string * error = nullptr );

	const std::string & path() const { return path_; }
	std::uint64_t file_size() const { return file_size_; }
	std::uint32_t version() const { return version_; }
	std::uint32_t toc_offset() const { return toc_offset_; }

	const std::vector<Entry> & entries() const { return entries_; }

	/// Every structural complaint found while parsing. Empty for all four fixtures in
	/// tests/data - that is the invariant the tests assert.
	const std::vector<std::string> & problems() const { return problems_; }

	/// Linear lookup by exact internal path (forward slashes, as stored in the table).
	const Entry * find( const std::string & name ) const;

	std::uint64_t total_uncompressed_size() const;
	std::uint64_t total_stored_size() const;

	/// Count of entries per payload variant, for diagnostics.
	std::size_t count_of( PayloadVariant variant ) const;

private:
	bool parse_table( std::ifstream & file, std::string * error );
	bool classify_entry( std::ifstream & file, Entry & entry );

	std::string path_;
	std::uint64_t file_size_ = 0;
	std::uint32_t toc_offset_ = 0;
	std::uint32_t version_ = 0;
	std::vector<Entry> entries_;
	std::vector<std::string> problems_;
};

/**
 * Reads and decompresses payloads.
 *
 * Holds its own file handle, so one instance per thread gives concurrent extraction with
 * no locking, no seeks shared between threads, and no hidden global state.
 */
class PayloadReader
{
public:
	explicit PayloadReader( const std::string & path );

	bool good() const { return file_.good(); }

	/**
	 * Decompress `entry` into `out`.
	 *
	 * @return false on I/O error, on a corrupt payload, or when the decompressed length
	 *         does not match entry.uncompressed_size. That last case matters: a size
	 *         mismatch means the table and the payload disagree, and accepting it would
	 *         write a silently truncated file.
	 */
	bool read( const Entry & entry, std::vector<unsigned char> & out, std::string * error = nullptr );

private:
	std::ifstream file_;
};

} // namespace rfa

#endif /* RFA_RFAARCHIVE_H */
