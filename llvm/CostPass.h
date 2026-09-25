#ifndef PORTWALK_COST_PASS_H
#define PORTWALK_COST_PASS_H

#include "llvm/IR/PassManager.h"

namespace portwalk {

struct RTMOptions {
  int domains;
  int nports;
  int spacing;
  int max_zero_run;
};

RTMOptions rtm_default_options();
void rtm_set_options(const RTMOptions &Opt);

struct RTMCostPass : llvm::PassInfoMixin<RTMCostPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
};

} // namespace portwalk

#endif
