/**
 * Battlefield 1942 .rfa container layout.
 *
 * Re-derived and machine-validated against every .rfa in the bf_pablov_mod tree
 * (814 archives, 448,127 entries, 99.99% structurally consistent). The narrative lives
 * in .github/skills/bf1942-standalone-map/references/rfa-format.md; the validator that
 * produced those numbers is tools/rfa_probe.py.
 *
 * The single most important thing to know: the per-entry data block has TWO variants,
 * and which one applies is decided from the sizes, never from the archive `version`.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rfa {

// ---------------------------------------------------------------------------
// Container constants
// ---------------------------------------------------------------------------

/// Payload compression unit. Every chunk except the last holds exactly this many bytes.
constexpr std::uint32_t CHUNK_SIZE = 32768u;

/// chunkCount + the first chunk descriptor.
constexpr std::uint32_t BLOCK_HEADER_SIZE = 16u;

/// compressedSize + uncompressedSize + payloadOffset: the size of each ADDITIONAL chunk
/// descriptor. The first one is folded into BLOCK_HEADER_SIZE.
constexpr std::uint32_t CHUNK_DESCRIPTOR_SIZE = 12u;

/// File data cannot begin before the 8-byte archive header (tocOffset, version).
constexpr std::uint32_t MIN_DATA_OFFSET = 8u;

/// The six fixed u32 fields of a directory entry.
constexpr std::uint32_t ENTRY_FIELDS_SIZE = 24u;

/// Bytes an entry with no content occupies. Not zero - the original packer stores 4.
constexpr std::uint32_t EMPTY_STORED_SIZE = 4u;

/// The value RFA Pack 1.7 writes into the first of the two "reserved" entry fields.
///
/// Unexplained. The shipped DICE archives write 0 there (and a non-zero value in the
/// `flags` field instead), and the game loads both, so the field is not load-bearing.
/// We reproduce rfaPack's value so that our archives can be compared with the oracle's
/// byte for byte in the tests; if a later probe proves it is ignored, it is trivial to
/// drop.
constexpr std::uint32_t RFA_PACK_RESERVED1 = 1253856u;

/// Archive header version. Both values occur in real archives, and neither selects the
/// payload layout: version-0 archives are always raw, but version-1 archives can be too.
constexpr std::uint32_t VERSION_0 = 0u;
constexpr std::uint32_t VERSION_1 = 1u;

/// Nice names for the observed `flags` values. The field is per-entry and opaque; these
/// exist only to make diagnostics readable. NEVER derive behaviour from it.
constexpr std::uint32_t FLAGS_FH = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Entry payload model
// ---------------------------------------------------------------------------

struct Chunk
{
	std::uint32_t compressed_size = 0;
	std::uint32_t uncompressed_size = 0;

	/// Offset of this chunk's payload, relative to the start of the payload region.
	std::uint32_t payload_offset = 0;

	/// True when the sizes say this chunk is an LZO1X stream rather than verbatim bytes.
	///
	/// The test is inequality, NOT "compressed_size < uncompressed_size". LZO1X output can
	/// be LARGER than its input - a 1-byte file becomes a 5-byte chunk, and 32 KiB of
	/// incompressible data grows slightly - so a less-than test misclassifies precisely the
	/// incompressible chunks that the writer and the game produce.
	///
	/// ⚠️ **Equality does not prove the chunk is verbatim.** LZO1X can also compress a chunk
	/// into exactly as many bytes as it started with. Measured on the shipping
	/// `Battle_of_Britain.rfa`, whose `Objects/Willy/Willy.con` carries a 30-byte stream that
	/// expands to the 30-byte file (finding 30). The sizes are therefore a HINT, not a
	/// verdict: PayloadReader treats "equal" as "try the codec, then copy", and the writer
	/// never has to care because it emits either all-raw or all-chunked entries.
	/// `payload_is_compressed()` under-reports for the same reason - it is a diagnostic, not
	/// a decision.
	bool is_compressed() const { return compressed_size != uncompressed_size; }
};

enum class PayloadVariant
{
	/// Not classified: either the entry was never parsed, or parsing failed.
	Unknown,

	/// uncompressed_size == 0. The entry stores EMPTY_STORED_SIZE bytes and expands to
	/// nothing. Observed 56 times in the 814-archive sweep.
	Empty,

	/// stored_size == uncompressed_size: the payload sits directly at data_offset with
	/// NO block header at all. 34,617 entries - 34,382 in version-0 archives and 235
	/// inside version-1 archives.
	Raw,

	/// chunkCount + 12-byte-per-chunk descriptors, then the concatenated chunk payloads.
	/// The common case: 413,510 entries.
	Chunked,
};

inline const char * to_string( PayloadVariant variant )
{
	switch( variant ) {
		case PayloadVariant::Empty:   return "empty";
		case PayloadVariant::Raw:     return "raw";
		case PayloadVariant::Chunked: return "chunked";
		default:                      return "unknown";
	}
}

struct Entry
{
	std::string name;

	/// Bytes this entry occupies in the data region (header included).
	std::uint32_t stored_size = 0;

	/// Size after decompression.
	std::uint32_t uncompressed_size = 0;

	/// Absolute offset of this entry's data block.
	std::uint32_t data_offset = 0;

	std::uint32_t reserved1 = 0;
	std::uint32_t reserved2 = 0;

	/// Per-entry and opaque. The original `rfaPack.exe -u` overwrites it with zero rather than
	/// preserving it (probed: PLAN_rfa_tools.md §2.5), so treat any meaning it carries as
	/// unavailable in archives that have been updated by the original. We preserve the target's
	/// value by default and rescind that with `--reset-entry-flags` (D6). The engine loads
	/// archives whose value is 0 just as happily as 0xFFFFFFFF.
	std::uint32_t flags = 0;

	PayloadVariant variant = PayloadVariant::Unknown;
	std::vector<Chunk> chunks;

	std::size_t chunk_count() const { return chunks.size(); }

	/// Bytes of chunk table at data_offset: 16 for one chunk, +12 for each further one.
	/// Zero for Raw and Empty, which have no header.
	std::uint32_t header_size() const
	{
		if( variant != PayloadVariant::Chunked || chunks.empty() ) {
			return 0u;
		}

		return BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * (std::uint32_t)( chunks.size() - 1 );
	}

	/// Absolute offset of the payload region.
	std::uint64_t payload_offset() const
	{
		return (std::uint64_t)data_offset + header_size();
	}

	/// Total compressed bytes across all chunks (0 for Raw/Empty).
	std::uint64_t total_compressed_size() const
	{
		std::uint64_t total = 0;
		for( const Chunk & chunk : chunks ) {
			total += chunk.compressed_size;
		}
		return total;
	}

	/// True when at least one chunk is LZO1X-compressed.
	bool payload_is_compressed() const
	{
		for( const Chunk & chunk : chunks ) {
			if( chunk.is_compressed() ) {
				return true;
			}
		}
		return false;
	}
};

/// ceil(uncompressed_size / CHUNK_SIZE), minimum 1. An entry's chunkCount must equal this.
///
/// Takes a 64-bit size so that callers can use it as a work estimate - counting the chunks
/// of a whole tree before reading any of it - without truncating a file larger than 4 GiB.
/// The RESULT stays 32-bit because that is what the entry table stores.
inline std::uint32_t chunk_count_for( std::uint64_t uncompressed_size )
{
	if( uncompressed_size == 0 ) {
		return 1u;
	}

	return (std::uint32_t)( ( uncompressed_size + CHUNK_SIZE - 1u ) / CHUNK_SIZE );
}

} // namespace rfa


