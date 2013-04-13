#ifndef GRAPHLAB_DYNAMIC_BLOCK_HPP
#define GRAPHLAB_DYNAMIC_BLOCK_HPP

#include <stdint.h>
#include <graphlab/logger/assertions.hpp>

namespace graphlab {
  template<typename valuetype, uint32_t capacity>
  class block_linked_list;

  /**
   * Define a block with value type and fixed capacity
   */
  template<typename valuetype, uint32_t capacity>
  class dynamic_block {
   public:
     /// construct empty block
     dynamic_block(float _id) : _id(_id), _next(NULL), _size(0) { }

     template<typename InputIterator>
     void assign(InputIterator first, InputIterator last) {
       // ASSERT_LE(last - first, capacity);
       _size = last-first;
       int i = 0;
       InputIterator iter = first;
       while (iter != last) {
         values[i++] = *iter;
         iter++;
       }
     }

     /// split the block into two parts
     void split() {
       float newid = (_next == NULL) ?  (_id+1) : (_id + _next->_id)/2;
       // create new block
       dynamic_block* secondhalf = new dynamic_block(newid);
       // copy the second half over
       uint32_t mid = capacity/2;
       memcpy(secondhalf->values, &values[mid], (capacity/2)*sizeof(valuetype));
       // update pointer
       secondhalf->_next = _next;
       _next = secondhalf;
       _size = capacity/2;
       secondhalf->_size = capacity/2;
     }

     /// return the ith element in the block
     valuetype& get(uint32_t i) {
       ASSERT_LT(i, _size);
       return values[i];
     }

     /// add a new element in to the end of the block
     /// return false when the block is full
     bool add(const valuetype& elem) {
       if (_size == capacity) {
         return false;
       } else {
         values[_size++] = elem;
         return true;
       }
     }

     inline bool is_full() const { return _size == capacity; }

     /// insert an element at pos, move elements after pos by 1.
     /// return false when the block is full
     bool insert(const valuetype& elem, uint32_t pos) {
       if (is_full()) {
         return false;
       } else {
         memmove(values+pos+1, values+pos, (_size-pos)*sizeof(valuetype));
         values[pos] = elem;
         ++_size;
         return true;
       }
     }

     /// returns the size of the block
     size_t size() const {
       return _size;
     }

     /// returns the size of the block
     float id() const {
       return _id;
     }


     dynamic_block* next() {
       return _next;
     }

   //////////////////// Pretty print API ///////////////////////// 
   void print(std::ostream& out) {
     for (size_t i = 0; i < _size; ++i) {
       out << values[i] << " ";
     }
     if (_size < capacity) {
       out << "_" << (capacity-_size) << " ";
     }
   }
   
   private:
     /// unique id, also encoding relative ordering of the blocks 
     float _id;
     /// value storage
     valuetype values[capacity];
     /// pointer to the next block
     dynamic_block* _next;
     /// size of the block
     uint32_t _size;

    friend class block_linked_list<valuetype, capacity>;
  };
} // end of namespace
#endif
