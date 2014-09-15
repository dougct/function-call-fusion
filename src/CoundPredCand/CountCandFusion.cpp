//===-- CountPredCandidates.cpp ------ Counts Candidates for Fusion -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a counter of candidates for function call fusion.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cnt-ff"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;


STATISTIC(CandidatesForFusion, "Candidates for Function Fusion.");
STATISTIC(NumberOfBranches, "Number of Branches.");

namespace {
  // CountPredCand
  struct CountCandFusion : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    CountCandFusion() : FunctionPass(ID) {}

    void getCandidates (std::vector<Function*> &candidates, BasicBlock *BB) {
      // Gather functions called inside of a basic block.
      // We're only checking the first function for now.
      for (auto I = BB->begin(), E = BB->end(); I != E; ++I) {
        if (CallInst *C = dyn_cast<CallInst>(I)) {
          // Check to see if it's an indirect function call
          Function *CF = C->getCalledFunction();
          if (CF == NULL) 
            continue;

          if (CF->isVarArg() || CF->isDeclaration() || CF->isIntrinsic())
            continue;

          candidates.push_back(CF);
        }
      }
    }



    virtual bool runOnFunction(Function &F) {
      std::vector<Function*> V1;
      std::vector<Function*> V2;
      std::vector<Function*> Worklist;
      for (Function::iterator bgn = F.begin(), end = F.end(); bgn != end; ++bgn) {
        TerminatorInst *BBTerm = bgn->getTerminator();
        if (BBTerm->getNumSuccessors() != 2)
          continue;

        // We don't evaluate 'invoke' terminated basic blocks.
        if (dyn_cast<InvokeInst>(BBTerm))
          continue;

        NumberOfBranches++;

        // Gets a list with candidates for function fusion
        getCandidates(V1, BBTerm->getSuccessor(0));
        getCandidates(V2, BBTerm->getSuccessor(1));

        // set_intersect expects the sets to be sorted.
        std::sort(V1.begin(), V1.end());
        std::sort(V2.begin(), V2.end());

        // See whether there is an actuall function that we can optimize.
        //std::vector<Function*> candidates;
        std::set_intersection(V1.begin(), V1.end(), 
                              V2.begin(), V2.end(), 
                              std::back_inserter(Worklist));

        CandidatesForFusion += Worklist.size();
        V1.clear();
        V2.clear();
        Worklist.clear();
      }

      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }
  };
}

char CountCandFusion::ID = 0;
static RegisterPass<CountCandFusion> Y("cnt-ff", "Function Fusion Candidates Pass");
