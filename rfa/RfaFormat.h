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

#ifndef RFA_RFAFORMAT_H
#define RFA_RFAFORMAT_H

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

	/// A chunk is either LZO1X or stored verbatim. Note that compressed_size may EXCEED
	/// uncompressed_size for incompressible data - real archives do this.
	bool is_compressed() const { return compressed_size < uncompressed_size; }
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

	/// Per-entry and opaque. Preserve it verbatim on in-place updates; the engine loads
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
inline std::uint32_t chunk_count_for( std::uint32_t uncompressed_size )
{
	if( uncompressed_size == 0 ) {
		return 1u;
	}

	return ( uncompressed_size + CHUNK_SIZE - 1u ) / CHUNK_SIZE;
}

} // namespace rfa

#endif /* RFA_RFAFORMAT_H */
