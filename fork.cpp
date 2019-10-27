#include "fork.hh"

#include <nix/value.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>

#undef GC

#ifdef GC
#define GC_LINUX_THREADS 1
#include <gc/gc_allocator.h>
#endif

using namespace nix;

static void prim_fork(EvalState &state, const Pos &pos, Value **args, Value &v) {
  state.forceAttrs(*args[0], pos);


  state.mkAttrs(v, args[0]->attrs->size());
  for (auto &i : *args[0]->attrs) {
    std::shared_ptr<ChildProc> child = std::make_shared<ChildProc>(args[0], &state);
    Value *out = state.allocAttr(v, i.name);
    out->type = tThunk;
    out->thunk.env = &state.baseEnv;
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
  //v.type = tInt;
  //v.integer = 42;
  child->eval(path, child, state, v);
}

ChildProc::ChildProc(Value *v, EvalState *state) : v{v} {
  ProcessOptions options;
  options.allowVfork = false;

  parentToChild.create();
  childToParent.create();

#ifdef GC
  GC_atfork_prepare();
#endif
  auto pid = startProcess([&]() {
#ifdef GC
      GC_atfork_child();
      GC_start_mark_threads();
#endif
      parentToChild.writeSide = -1;
      childToParent.readSide = -1;
      FdSource parentIn(parentToChild.readSide.get());
      FdSink parentOut(childToParent.writeSide.get());
      while (true) {
        string s;
        PathSet context;
        Strings attrPath = readStrings<Strings>(parentIn);
        printError(format("child received %1%") % concatStringsSep(".",attrPath));
        state->forceAttrs(*v, noPos);
        Value *current = v;
        for (auto &i : attrPath) {
          printError(format("attr %1%") % i);
          Bindings::iterator attr = current->attrs->find(state->symbols.create(i));
          if (attr == current->attrs->end()) {
            printError("todo, report exception");
            exit(-1);
          }
          current = attr->value;
          state->forceValue(*current);
        }
        // do a shallow serialization of current to the parent
        switch (current->type) {
        case tInt: // 1
          parentOut << current->type << current->integer;
          break;
        case tBool: // 2
          parentOut << current->type << (int64_t)current->boolean;
          break;
        case tString: // 3
          s = state->coerceToString(noPos, *current, context);
          parentOut << current->type << s << context;
          break;
        case tPath: // 4
          s = current->path;
          parentOut << current->type << s;
          break;
        case tAttrs: // 6
          parentOut << current->type << current->attrs->size();
          for (auto &i : *current->attrs) {
            string name = i.name;
            parentOut << name;
          }
          break;
        default:
          printError(format("unable to serialize type %1%") % current->type);
          exit(-2);
        }
        parentOut.flush();
      }
      puts("child exiting");
      exit(0);
  }, options);
#ifdef GC
  GC_atfork_parent();
#endif
  parentToChild.readSide = -1;
  childToParent.writeSide = -1;

  out = parentToChild.writeSide.get();
  in = childToParent.readSide.get();
}

void ChildProc::eval(const Strings &path, std::shared_ptr<ChildProc> self, EvalState &state, Value &value) {
  string s;
  const char * cstr = NULL;
  PathSet context;
  int64_t int64;
  printError(format("parent about to trigger eval of %1%") % concatStringsSep(".",path));
  out << path;
  out.flush();
  int type;
  in >> type;
  printError(format("got %1% back") % type);
  value.type = (ValueType)type;
  switch (type) {
  case tInt:
    in >> value.integer;
    break;
  case tBool: // 2
    printError("parsing a bool");
    in >> int64;
    value.boolean = int64;
    break;
  case tString: // 3
    in >> s;
    context = readStrings<PathSet>(in);
    mkString(value, s, context);
    break;
  case tPath: // 4
    printError("parsing a path");
    in >> s;
    printError(format("got %1%") % s);
    cstr = s.c_str();
    printError(format("got %1%") % cstr);
    mkPath(*v, cstr);
    printError(format("now its %1%") % v->path);
    printError("parsed a path");
    printf("%p\n", v);
    break;
  case tAttrs:
    Bindings::size_t size;
    in >> size;
    state.mkAttrs(value, size);
    for (int i=0; i<size; i++) {
      string name;
      in >> name;
      Strings newpath = path;
      newpath.push_back(name);
      Value *out = state.allocAttr(value, state.symbols.create(name));
      out->type = tThunk;
      out->thunk.env = NULL;
      out->thunk.expr = new ExprRemoteValue(self, newpath);
    }
    break;
  default:
    clearValue(value);
    value.type = tNull;
  }
}
