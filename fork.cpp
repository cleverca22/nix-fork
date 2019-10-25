#include "fork.hh"

#include <nix/value.hh>
#include <nix/eval.hh>
#define GC_LINUX_THREADS 1
#include <gc/gc_allocator.h>

using namespace nix;

static void prim_fork(EvalState &state, const Pos &pos, Value **args, Value &v) {
  state.forceAttrs(*args[0], pos);

  std::shared_ptr<ChildProc> child = std::make_shared<ChildProc>(args[0]);

  state.mkAttrs(v, args[0]->attrs->size());
  for (auto &i : *args[0]->attrs) {
    Value *out = state.allocAttr(v, i.name);
    out->type = tThunk;
    out->thunk.env = NULL;
    Strings path;
    path.push_back((const string&) i.name);
    out->thunk.expr = new ExprRemoteValue(child, (const string&) i.name);
  }
}

ExprRemoteValue::ExprRemoteValue(std::shared_ptr<ChildProc> child, Strings path) : child{child}, path{path} {
}

ExprRemoteValue::ExprRemoteValue(std::shared_ptr<ChildProc> child, std::string path) : child{child} {
  this->path.push_back(path);
}

extern "C" void nixFork(EvalState &state, Value &v) {
  Symbol sym = state.symbols.create("fork");
  v.primOp = new PrimOp(prim_fork, 1, sym);
  v.type = tPrimOp;
}

void ExprRemoteValue::eval(EvalState & state, Env & env, Value & v) {
  puts("evaling remote expr");
  child->eval(path);
  v.type = tInt;
  v.integer = 42;
}

ChildProc::ChildProc(Value *v) : v{v} {
  ProcessOptions options;
  options.allowVfork = false;

  parentToChild.create();
  childToParent.create();

  GC_atfork_prepare();
  auto pid = startProcess([&]() {
      GC_atfork_child();
      GC_start_mark_threads();
      parentToChild.writeSide = -1;
      childToParent.readSide = -1;
      FdSource parentIn(parentToChild.readSide.get());
      FdSink parentOut(childToParent.writeSide.get());
      while (true) {
        Strings attrPath = readStrings<Strings>(parentIn);
        printError(format("child received %1%") % concatStringsSep(".",attrPath));
        //for (auto &i : attrPath) {
          //Bindings::iterator attr = args[0]->attrs->find();
    //if (attr == args[0]->attrs->end())
      }
      puts("child exiting");
      exit(0);
  }, options);
  GC_atfork_parent();
  parentToChild.readSide = -1;
  childToParent.writeSide = -1;

  out = parentToChild.writeSide.get();
}

void ChildProc::eval(const Strings &path) {
  printError(format("parent about to trigger eval of %1%") % concatStringsSep(".",path));
  out << path;
  out.flush();
  sleep(1);
}
