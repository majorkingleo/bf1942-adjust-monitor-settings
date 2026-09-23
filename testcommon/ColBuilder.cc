/*
 * ColBuilder.cc
 *
 * @author Copyright (c) 2026
 */

#include "ColBuilder.h"

#include <algorithm>
#include <sstream>

namespace {

std::string repeat( const std::string & s, unsigned n )
{
	std::string r;
	r.reserve( s.size() * n );
	for( unsigned i = 0; i < n; ++i ) {
		r += s;
	}
	return r;
}

} // namespace

std::string ColBuilder::strip_escape_sequences( const std::string & str )
{
	std::string out;
	out.reserve( str.size() );

	for( std::string::size_type i = 0; i < str.size(); ++i ) {
		if( str[i] != '\x1b' ) {
			out += str[i];
			continue;
		}

		// ESC [ <params> <final byte in '@'..'~'>   (also handles a bare ESC + byte)
		std::string::size_type j = i + 1;
		if( j < str.size() && str[j] == '[' ) {
			++j;
			while( j < str.size() && !( str[j] >= '@' && str[j] <= '~' ) ) {
				++j;
			}
		}
		if( j < str.size() ) {
			++j;
		}
		i = j > 0 ? j - 1 : i;
	}

	return out;
}

unsigned int ColBuilder::count_visible_size( const std::string & str )
{
	return (unsigned int)strip_escape_sequences( str ).size();
}

std::string ColBuilder::fill_leading( std::string s, const std::string fill_sign, unsigned int len )
{
	const unsigned int current = count_visible_size( s );

	for( unsigned int i = current; i < len; ++i ) {
		s = fill_sign + s;
	}

	return s;
}

int ColBuilder::addCol( const std::string & name )
{
	col_headers.push_back( name );
	cols.push_back( std::vector<std::string>() );

	return (int)col_headers.size() - 1;
}

void ColBuilder::addColData( int idx, const std::string & data )
{
	if( idx < 0 || (std::size_t)idx >= cols.size() ) {
		return;
	}

	cols[(std::size_t)idx].push_back( data );
}

void ColBuilder::addColData( const std::string & name, const std::string & data )
{
	addColData( getColByName( name ), data );
}

bool ColBuilder::haveCol( const std::string & name ) const
{
	return getColByName( name ) >= 0;
}

int ColBuilder::getColByName( const std::string & name ) const
{
	for( std::size_t i = 0; i < col_headers.size(); ++i ) {
		if( col_headers[i] == name ) {
			return (int)i;
		}
	}

	return -1;
}

int ColBuilder::getMaxNumOfRows() const
{
	std::size_t max_rows = 0;

	for( const auto & col : cols ) {
		max_rows = std::max( max_rows, col.size() );
	}

	return (int)max_rows;
}

int ColBuilder::getColWidth( int idx ) const
{
	if( idx < 0 || (std::size_t)idx >= cols.size() ) {
		return 0;
	}

	int width = (int)count_visible_size( col_headers[(std::size_t)idx] );

	for( const auto & data : cols[(std::size_t)idx] ) {
		width = std::max( width, (int)count_visible_size( data ) );
	}

	return width;
}

std::string ColBuilder::toString() const
{
	const int num_cols = getNumOfCols();

	if( num_cols == 0 ) {
		return std::string();
	}

	std::vector<int> widths( (std::size_t)num_cols );
	for( int i = 0; i < num_cols; ++i ) {
		widths[(std::size_t)i] = getColWidth( i );
	}

	std::size_t total_width = 0;
	for( int i = 0; i < num_cols; ++i ) {
		total_width += (std::size_t)widths[(std::size_t)i];
	}
	total_width += 3 * (std::size_t)( num_cols - 1 ); // the " | " separators

	std::vector<std::string> lines;

	// header
	{
		std::ostringstream out;
		for( int i = 0; i < num_cols; ++i ) {
			if( i ) {
				out << " | ";
			}
			out << fill_leading( col_headers[(std::size_t)i], " ", (unsigned)widths[(std::size_t)i] );
		}
		lines.push_back( out.str() );
	}

	lines.push_back( repeat( "-", fill_bar_to_width_of ? fill_bar_to_width_of : (unsigned)total_width ) );

	const int num_rows = getMaxNumOfRows();

	for( int row = 0; row < num_rows; ++row ) {
		std::ostringstream out;
		for( int i = 0; i < num_cols; ++i ) {
			if( i ) {
				out << " | ";
			}

			const auto & col = cols[(std::size_t)i];
			const std::string cell = (std::size_t)row < col.size() ? col[(std::size_t)row] : std::string();
			out << fill_leading( cell, " ", (unsigned)widths[(std::size_t)i] );
		}
		lines.push_back( out.str() );
	}

	const std::string margin( margin_left, ' ' );

	std::ostringstream result;
	for( unsigned i = 0; i < margin_top; ++i ) {
		result << "\n";
	}
	for( const auto & line : lines ) {
		result << margin << line << "\n";
	}

	return result.str();
}
