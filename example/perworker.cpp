#include "mcsl_aligned.hpp"
#include "mcsl_perworker.hpp"
#include <iostream>


int main() {
  std::cout << "id=" << mcsl::unique_id::get_my_id() << std::endl;
  return 0;
}
