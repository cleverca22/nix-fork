#include <nix/nixexpr.hh>
#include <nix/util.hh>
#include <nix/serialise.hh>

using namespace nix;

class ChildProc {
  public:
    ChildProc(Value *v);
    void eval(const Strings &path);
  private:
    Pipe parentToChild, childToParent;
    FdSink out;
    Value *v;
};

struct ExprRemoteValue : public Expr {
  ExprRemoteValue(std::shared_ptr<ChildProc> child, Strings path);
  ExprRemoteValue(std::shared_ptr<ChildProc> child, const std::string path);
  virtual void eval(EvalState & state, Env & env, Value & v);
private:
  std::shared_ptr<ChildProc> child;
  Strings path;
};
