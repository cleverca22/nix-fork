#include "fork.hh"

#include <nix/value.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>

#include <string.h>

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
    switch (i.value->type) {
    case tInt: // 1
    case tBool: // 2
    case tString: // 3
    case tPath: // 4
    case tFloat: // 17
      v.attrs->push_back(i);
      break;
    default:
      Value *out = state.allocAttr(v, i.name);
      out->type = tThunk;
      out->thunk.env = &state.baseEnv;
      out->thunk.expr = new ExprRemoteValue(args[0], (const string&) i.name);
    }
  }
}

ExprRemoteValue::ExprRemoteValue(Value *v, Strings path) : v(v), path(path) {
  type = 1;
  withChild = false;
}

ExprRemoteValue::ExprRemoteValue(Value *v, std::string path) : v{v} {
  this->path.push_back(path);
  type = 2;
  withChild = false;
}

ExprRemoteValue::ExprRemoteValue(std::shared_ptr<ChildProc> child, Strings path) : child{child}, path{path} {
  type = 3;
  withChild = true;
}

extern "C" void nixFork(EvalState &state, Value &v) {
  Symbol sym = state.symbols.create("fork");
  v.primOp = new PrimOp(prim_fork, 1, sym);
  v.type = tPrimOp;
}

void ExprRemoteValue::eval(EvalState & state, Env & env, Value & v) {
  if (!withChild) {
    child = std::make_shared<ChildProc>(this->v, state);
  }
  child->eval(path, child, state, v);
}

static void serializeValue(Value &v, FdSink &out, EvalState &state) {
  PathSet context;
  Value **elems;
  string s;
  switch (v.type) {
  case tInt: // 1
    out << v.type << v.integer;
    break;
  case tBool: // 2
    out << v.type << (int64_t)v.boolean;
    break;
  case tString: // 3
    s = state.coerceToString(noPos, v, context);
    out << v.type << s << context;
    break;
  case tPath: // 4
    s = v.path;
    out << v.type << s;
    break;
  case tNull: // 5
    out << v.type;
    break;
  case tAttrs: // 6
    out << v.type << v.attrs->size();
    for (auto &i : *v.attrs) {
      string name = i.name;
      out << name;
    }
    break;
  case tList1: // 7
  case tList2: // 8
  case tListN: // 9
    // TODO, the list contents are recursively forced, rather then remaining lazy into the child
    // fixing that will require allowing an attrpath sent to the child to include array indexes
    state.forceValueDeep(v);
    out << v.type << v.listSize();
    elems = v.listElems();
    for (size_t i = 0; i < v.listSize(); i++) {
      serializeValue(*elems[i], out, state);
    };
    break;
  case tFloat: // 17
    out << v.type << v.fpoint;
    break;
  default:
    printError(format("unable to serialize type %1%") % v.type);
    exit(-2);
  }
  out.flush();
}

ChildProc::ChildProc(Value *v, EvalState &state) : v(v) {
  ProcessOptions options;
  options.allowVfork = false;

  parentToChild.create();
  childToParent.create();

#ifdef GC
  GC_atfork_prepare();
#endif
  pid = startProcess([&]() {
#ifdef GC
      GC_atfork_child();
      GC_start_mark_threads();
#endif
      parentToChild.writeSide = -1;
      childToParent.readSide = -1;
      FdSource parentIn(parentToChild.readSide.get());
      FdSink parentOut(childToParent.writeSide.get());
      while (true) {
        Strings attrPath = readStrings<Strings>(parentIn);
        //printError(format("child received %1%") % concatStringsSep(".",attrPath));
        state.forceAttrs(*v, noPos);
        Value *current = v;
        for (auto &i : attrPath) {
          //printError(format("attr %1%") % i);
          Bindings::iterator attr = current->attrs->find(state.symbols.create(i));
          if (attr == current->attrs->end()) {
            printError("todo, report exception");
            exit(-1);
          }
          current = attr->value;
          state.forceValue(*current);
        }
        // do a shallow serialization of current to the parent
        serializeValue(*current, parentOut, state);
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

static void deserializeValue(EvalState &state, Source &in, Value *v, Strings path, Value &value, std::shared_ptr<ChildProc> self) {
  if (!self) abort();
  string s;
  PathSet context;
  int64_t int64;
  NixInt i;
  NixFloat f;
  int type;
  Value **elems;
  in >> type;
  //printError(format("got %1% back") % type);
  value.type = (ValueType)type;
  switch (type) {
  case tInt:
    in >> i;
    mkInt(value, i);
    break;
  case tBool: // 2
    in >> int64;
    mkBool(value, int64);
    break;
  case tString: // 3
    in >> s;
    context = readStrings<PathSet>(in);
    mkString(value, s, context);
    break;
  case tPath: // 4
    in >> s;
    mkPath(value, s.c_str());
    break;
  case tNull: // 5
    mkNull(value);
    break;
  case tAttrs: // 6
    Bindings::size_t size;
    in >> size;
    state.mkAttrs(value, size);
    for (size_t i=0; i<size; i++) {
      string name;
      in >> name;
      Strings newpath = path;
      newpath.push_back(name);
      Value *out = state.allocAttr(value, state.symbols.create(name));
      out->type = tThunk;
      out->thunk.env = &state.baseEnv;
      out->thunk.expr = new ExprRemoteValue(self, newpath);
    }
    break;
  case tList1: // 7
  case tList2: // 8
  case tListN: // 9
    in >> size;
    state.mkList(value, size);
    elems = value.listElems();
    for (size_t i=0; i<size; i++) {
      Value *elem = state.allocValue();
      deserializeValue(state, in, v, path, *elem, self);
      elems[i] = elem;
    }
    break;
  case tFloat: // 17
    in >> f;
    mkFloat(value, f);
    break;
  default:
    printError(format("unable to parse type code %1%") % type);
    mkNull(value);
  }
}

void ChildProc::eval(const Strings &path, std::shared_ptr<ChildProc> self, EvalState &state, Value &value) {
  //printError(format("parent about to trigger eval of %1% in %2%") % concatStringsSep(".",path) % pid);
  out << path;
  out.flush();
  deserializeValue(state, in, v, path, value, self);
}
