#include "CostPass.h"

#include "rtm.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <climits>
#include <optional>
#include <string>

using namespace llvm;

namespace portwalk {

static RTMOptions Current = {64, 1, 0, 8};

RTMOptions rtm_default_options() { return Current; }

void rtm_set_options(const RTMOptions &Opt) { Current = Opt; }

static const Value *scev_base(const SCEV *S) {
  if (!S)
    return nullptr;
  if (auto *U = dyn_cast<SCEVUnknown>(S))
    return U->getValue();
  if (auto *AR = dyn_cast<SCEVAddRecExpr>(S))
    return scev_base(AR->getStart());
  if (auto *Add = dyn_cast<SCEVAddExpr>(S)) {
    for (const SCEV *Op : Add->operands()) {
      if (const Value *V = scev_base(Op))
        return V;
    }
  }
  if (auto *Cast = dyn_cast<SCEVCastExpr>(S))
    return scev_base(Cast->getOperand());
  return nullptr;
}

static std::string value_name(const Value *V) {
  if (!V)
    return "?";
  if (V->hasName())
    return V->getName().str();
  if (auto *Arg = dyn_cast<Argument>(V))
    return "arg" + std::to_string(Arg->getArgNo());
  std::string slot;
  raw_string_ostream os(slot);
  V->printAsOperand(os, false);
  return os.str();
}

static int steady_shift(int64_t step_domains) {
  rtm_geom_t geom;
  if (rtm_geom_set(&geom, Current.domains, Current.nports, Current.spacing,
                   Current.max_zero_run) != 0)
    return -1;
  rtm_port_t port;
  if (rtm_port_init(&port, &geom, 0) != 0)
    return -1;
  /* Head sits on domain 0. The next element of an affine stream is `step`
   * domains away. That distance is the per-iteration shift once the stream
   * is underway. */
  int dist = 0;
  if (step_domains > INT32_MAX || step_domains < INT32_MIN) {
    rtm_port_free(&port);
    return -1;
  }
  if (step_domains != 0)
    dist = rtm_align(&port, (int)step_domains);
  rtm_port_free(&port);
  return dist;
}

PreservedAnalyses RTMCostPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (F.isDeclaration())
    return PreservedAnalyses::all();

  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  const DataLayout &DL = F.getParent()->getDataLayout();

  outs() << "== function " << F.getName() << " ==\n";
  bool any = false;

  for (Loop *L : LI.getLoopsInPreorder()) {
    std::optional<uint64_t> trip;
    const SCEV *BTC = SE.getBackedgeTakenCount(L);
    if (const auto *C = dyn_cast<SCEVConstant>(BTC)) {
      if (C->getAPInt().isIntN(63))
        trip = C->getAPInt().getZExtValue() + 1;
    }

    bool header = false;
    auto ensure_header = [&]() {
      if (header)
        return;
      header = true;
      any = true;
      outs() << "  loop depth=" << L->getLoopDepth();
      if (L->getHeader()->hasName())
        outs() << " " << L->getHeader()->getName();
      if (trip)
        outs() << " trip=" << *trip;
      else
        outs() << " trip=?";
      outs() << "\n";
    };

    for (BasicBlock *BB : L->blocks()) {
      if (LI.getLoopFor(BB) != L)
        continue;
      for (Instruction &I : *BB) {
        Value *Ptr = nullptr;
        Type *Ty = nullptr;
        const char *Kind = nullptr;
        if (auto *Ld = dyn_cast<LoadInst>(&I)) {
          Ptr = Ld->getPointerOperand();
          Ty = Ld->getType();
          Kind = "load";
        } else if (auto *St = dyn_cast<StoreInst>(&I)) {
          Ptr = St->getPointerOperand();
          Ty = St->getValueOperand()->getType();
          Kind = "store";
        } else {
          continue;
        }

        TypeSize Sz = DL.getTypeStoreSize(Ty);
        if (Sz.isScalable() || Sz.getFixedValue() == 0)
          continue;
        uint64_t elem = Sz.getFixedValue();
        const SCEV *S = SE.getSCEV(Ptr);
        std::string base = value_name(scev_base(S));

        if (auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
          if (AR->getLoop() == L && AR->isAffine()) {
            if (const auto *Step = dyn_cast<SCEVConstant>(AR->getOperand(1))) {
              if (!Step->getAPInt().isSignedIntN(64))
                continue;
              int64_t step_bytes = Step->getAPInt().getSExtValue();
              ensure_header();
              if (step_bytes % (int64_t)elem != 0) {
                outs() << "    " << Kind << " " << base
                       << " step is not a whole number of elements\n";
                outs() << "METRIC " << F.getName() << " " << Kind << " " << base
                       << " unaligned 1\n";
                continue;
              }
              int64_t step_domains = step_bytes / (int64_t)elem;
              int steady = steady_shift(step_domains);
              const char *domain_word =
                  (step_domains == 1 || step_domains == -1) ? "domain" : "domains";
              outs() << "    " << Kind << " " << base << " elem=" << elem
                     << "B step=" << step_bytes << "B (" << step_domains << " "
                     << domain_word << ") steady-shift/iter=" << steady;
              /* steady * (trip - 1): the first element is free when the port
               * is already there. Matches rtm_replay starting at head 0. */
              if (trip && *trip >= 1 && steady >= 0) {
                unsigned long long carry =
                    (unsigned long long)steady * (*trip - 1);
                outs() << " carry-shifts=" << carry;
              }
              outs() << "\n";
              outs() << "METRIC " << F.getName() << " " << Kind << " " << base
                     << " steady " << steady << "\n";
              continue;
            }
          }
        }

        ensure_header();
        if (SE.isLoopInvariant(S, L)) {
          outs() << "    " << Kind << " " << base
                 << " loop-invariant (one seek, then free)\n";
          outs() << "METRIC " << F.getName() << " " << Kind << " " << base
                 << " invariant 1\n";
        } else {
          outs() << "    " << Kind << " " << base
                 << " indirect: no affine stride, hand the bundle to the "
                    "port scheduler\n";
          outs() << "METRIC " << F.getName() << " " << Kind << " " << base
                 << " indirect 1\n";
        }
      }
    }
  }

  if (!any)
    outs() << "  (no loops)\n";
  return PreservedAnalyses::all();
}

} // namespace portwalk
