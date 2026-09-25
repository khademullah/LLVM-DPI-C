#include "CostPass.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace llvm;

static void usage(const char *argv0) {
  errs() << "usage: " << argv0
         << " [--domains N] [--ports N] [--spacing N] file.ll...\n"
         << "  Reports per-loop racetrack shift cost from LLVM IR.\n"
         << "  Affine streams get a steady-state shift per iteration.\n"
         << "  Indirect addresses are marked for the port scheduler.\n";
}

int main(int argc, char **argv) {
  portwalk::RTMOptions opt = portwalk::rtm_default_options();
  std::vector<std::string> files;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *flag) -> const char * {
      if (a != flag)
        return nullptr;
      if (i + 1 >= argc) {
        errs() << "missing value for " << flag << "\n";
        return (const char *)-1;
      }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    }
    if (const char *v = need("--domains")) {
      if (v == (const char *)-1)
        return 1;
      opt.domains = std::atoi(v);
    } else if (const char *v = need("--ports")) {
      if (v == (const char *)-1)
        return 1;
      opt.nports = std::atoi(v);
    } else if (const char *v = need("--spacing")) {
      if (v == (const char *)-1)
        return 1;
      opt.spacing = std::atoi(v);
    } else if (!a.empty() && a[0] == '-') {
      errs() << "unknown option " << a << "\n";
      usage(argv[0]);
      return 1;
    } else {
      files.push_back(a);
    }
  }
  if (files.empty()) {
    usage(argv[0]);
    return 1;
  }
  portwalk::rtm_set_options(opt);

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB;
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerModuleAnalyses(MAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  int rc = 0;
  for (const std::string &path : files) {
    SMDiagnostic err;
    LLVMContext ctx;
    std::unique_ptr<Module> M = parseIRFile(path, err, ctx);
    if (!M) {
      err.print(argv[0], errs());
      rc = 1;
      continue;
    }
    outs() << "module " << path << "\n";
    outs() << "geometry ports=" << opt.nports << " spacing=" << opt.spacing
           << " track_domains=" << opt.domains << "\n";
    ModulePassManager MPM;
    MPM.addPass(createModuleToFunctionPassAdaptor(portwalk::RTMCostPass()));
    MPM.run(*M, MAM);
  }
  return rc;
}
