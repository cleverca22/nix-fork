#include <nix/nixexpr.hh>
#include <nix/util.hh>
#include <nix/serialise.hh>

using namespace nix;

class ChildProc {
  public:
    ChildProc(Value *v, EvalState &state);
    void eval(const Strings &path, std::shared_ptr<ChildProc> self ,EvalState &state, Value &value);
  private:
    uint16_t pid;
    Pipe parentToChild, childToParent;
    FdSink out;
    FdSource in;
    Value *v;
};

struct ExprRemoteValue : public Expr {
  ExprRemoteValue(Value *, Strings path);
  ExprRemoteValue(Value *, const std::string path);
  ExprRemoteValue(std::shared_ptr<ChildProc> self, Strings path);
  virtual void eval(EvalState & state, Env & env, Value & v);
private:
  std::shared_ptr<ChildProc> child;
  bool withChild;
  Value *v;
  Strings path;
  int type;
};
