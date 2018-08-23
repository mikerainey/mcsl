#include "mcsl_aligned.hpp"
#include <iostream>

  mcsl::cache_aligned_fixed_capacity_array<int,10> a;

int main() {
  a[0] = 123;
  std::cout << "a[0] = " << a[0] << std::endl;
  printf("addr=%p\n",&a[0]);
  return 0;
}
