#ifndef _tools_JVECTOR_H
#define _tools_JVECTOR_H

#include <vector>

namespace Tools {

template <class T> class JVector
  {
  protected:
	std::vector<T> vector;

  public:	
	typedef typename std::vector<T>::iterator iterator;
	typedef typename std::vector<T>::const_iterator const_iterator;
	typedef typename std::vector<T>::const_reverse_iterator const_reverse_iterator;
	typedef typename std::vector<T>::reverse_iterator reverse_iterator;
	typedef typename std::vector<T>::size_type size_type;

	JVector()
	  : vector()
	  {}	

	JVector( size_type initial_size )
	  : vector( initial_size )
	  {}

	JVector( const JVector & other )
	  : vector( other.vector )
	  {}

	virtual ~JVector() {}

	void push_back( const T & t ) { vector.push_back( t ); }

	void add( const T & t ) { vector.push_back( t ); }

	T & get( size_type idx ) { return vector.at(idx); }

	const T & get( size_type idx ) const { return vector.at(idx); }

	bool IsEmpty() const { return vector.empty(); }	

	T & operator[]( size_type idx ) { return vector[idx]; }

	const T & operator[]( size_type idx ) const { return vector[idx]; }

	T & at( size_type idx ) { return vector.at(idx); }

	const T & at( size_type idx ) const { return vector.at(idx); }

	JVector<T> & operator=( const std::vector<T> & other ) { vector = other; return *this; }

	JVector<T> & operator=( const JVector<T> & other ) { vector = other.vector; return *this; }

	operator std::vector<T> & () { return vector; }
	operator const std::vector<T> & () const { return vector; }

	bool empty() const { return IsEmpty(); }

	size_type size() const { return vector.size(); }

	void resize( size_type size_ ) { vector.resize(size_); }
	void reserve( size_type size_ ) { vector.reserve(size_); }

	reverse_iterator rbegin() { return vector.rbegin(); }
	const_reverse_iterator rbegin() const { return vector.rbegin(); } 

	reverse_iterator rend() { return vector.rend(); }
	const_reverse_iterator rend() const { return vector.rend(); } 

	iterator begin() { return vector.begin(); }
	iterator end() { return vector.end(); }

	const_iterator begin() const { return vector.begin(); }
	const_iterator end() const { return vector.end(); }

	void insert( iterator where, const_iterator beg_, const_iterator end_ )
	  {
		vector.insert( where, beg_, end_ );
	  }

	void clear() { vector.clear(); }

	std::vector<T> & get_vector() { return vector; }
	const std::vector<T> & get_vector() const { return vector; }
	void set_vector( const 	std::vector<T> & other ) {  vector = other; }
  };

} // namespace tools

#endif  /* _tools_JVECTOR_H */
