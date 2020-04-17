#include "bit_vector.h"

//bitmap for used process control blocks
static unsigned int bitmap = 0; //for procs[] usage.
static unsigned int bitshare = 0; 	//for shareable resources. If set to 1, resource is shareable

//toggle bit
static int toggle(int map, int bit){
	int mask = 1 << bit;
	return map ^= mask;
}

//test bit
static int test(int map, int b){
	return (map & (1 << b)) >> b;
}

//find unset bit in bitmap
int search_bitvector(const int max_bit){
  int bit;
  for(bit=0; bit < max_bit; bit++){
    if(test(bitmap, bit) == 0){
      bitmap = toggle(bitmap, bit);
      return bit;
    }
  }
  return -1;
}

void unset_bit(const int bit){
  bitmap = toggle(bitmap, bit);
}

void set_shareable(int id){
	bitshare = toggle(bitshare, id);
}

int is_shareable(int id){
	return test(bitshare, id);
}
