#include "mcsl_snzi.hpp"
#include <iostream>

int main() {
  mcsl::snzi_fixed_capacity_tree<> t;
  t.mine().increment();
  return 0;
}
