/**
 * @file
 * @todo describe file content
 * @author Copyright (c) 2009 Salomon Automation GmbH
 */

#ifndef _Tools_ZVECTOR_H
#define _Tools_ZVECTOR_H

#include "jvector.h"

namespace Tools {

template <class T> class zvector : public JVector<T>
{
public:
	typedef typename JVector<T>::size_type size_type;

public:
  zvector( size_type init_size )
	: JVector<T>( init_size )
  {
	memset( &operator[](0), 0, sizeof(T) * init_size );
  }

  T & operator[]( size_type i ) { return JVector<T>::operator[](i); }

  zvector<T> & operator=( const JVector<T> & other ) { JVector<T>::set_vector( other.get_vector() ); return *this; }
};


} // /namespace wamas

#endif  /* _wamas_ZVECTOR_H */
