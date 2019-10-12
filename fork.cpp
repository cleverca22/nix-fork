#include <nix/value.hh>

using namespace nix;

extern "C" void nixFork(EvalState &state, Value &v) {
  v.type = tInt;
  v.integer = 42;
}
