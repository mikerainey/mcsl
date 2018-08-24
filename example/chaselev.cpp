#include "mcsl_chaselev.hpp"
#include <iostream>

int main() {
  mcsl::chaselev_deque<int> d;
  int i;
  d.push(&i);
  d.push(&i);
  d.pop();
  d.steal();
  return 0;
}
