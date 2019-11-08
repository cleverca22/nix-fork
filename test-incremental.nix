builtins.importNative ./fork.so "nixFork" {
  a = 42;
  b = 5*5;
  c.d.e.f = 7*8;
  bool = true;
  bool2 = false;
  path = ./fork.hh;
  string = "string";
  withcontext = "${./fork.hh}";
  float = 3.14;
  everything = [ 1 (2*3) 3.14 "foo" ./fork.hh "${./fork.hh}" false ];
}
