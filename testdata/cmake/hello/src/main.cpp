#include "hello.hpp"

int main() {
  int seed = 4;
  int doubled = double_value(seed);
  int result = add_one(doubled);
  return result;
}
