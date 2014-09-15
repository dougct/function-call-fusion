//===------------ FunctionFusion.cpp - Function Fusion Pass --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements function fusion.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "func-fusion"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Pass.h"

#include <string>
#include <vector>
#include <iterator>
#include <algorithm>

using namespace llvm;

STATISTIC(CandidatesForFunctionFusion, "Candidates for function fusion.");
STATISTIC(isAlive, "The pass ran fine.");
STATISTIC(NumberOfFunctions, "Number of functions in the program.");

namespace {
  // Function Fusion class
  struct FunctionFusion : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    FunctionFusion() : FunctionPass(ID) {}

    DominatorTree *DT_;
    DominanceFrontier *DF_;
    std::vector<Function*> Worklist;


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

          candidates.push_back(C->getCalledFunction());
        }
      }
    }


    BasicBlock* splitBB(BasicBlock *BB, Function* splitPoint) {
      for (auto I = BB->begin(), E = BB->end(); I != E; ++I) {
        if (CallInst *C = dyn_cast<CallInst>(I)) {
          errs () << C->getCalledFunction() << " vs " << splitPoint << "\n";
          if (C->getCalledFunction() == splitPoint) {
            errs() << "Spliting block!\n";
            return SplitBlock(BB, C, this);
          }
        }
      }
      return NULL;
    }

    void getFunctionsToOptimize (std::vector<Function*> &V1, 
                                 std::vector<Function*> &V2, 
                                 BasicBlock *TBB, 
                                 BasicBlock *FBB) {
      // Gets a list with candidates for function fusion
      getCandidates(V1, TBB);
      getCandidates(V2, FBB);

      // set_intersect expects the sets to be sorted.
      std::sort(V1.begin(), V1.end());
      std::sort(V2.begin(), V2.end());

      // See whether there is an actuall function that we can optimize.
      //std::vector<Function*> candidates;
      std::set_intersection(V1.begin(), V1.end(), 
                            V2.begin(), V2.end(), 
                            std::back_inserter(Worklist));

      CandidatesForFunctionFusion += Worklist.size();
    }

    void insertUncondBranch (BasicBlock *srcBB, BasicBlock *dstBB) {
      // Create an unconditional branch from a source BB to a destination BB.
      errs() << "Creating Branch to " << dstBB->getName() << "\n";
      TerminatorInst *srcBBTerm = srcBB->getTerminator();
      assert (srcBBTerm->getNumSuccessors() == 1 && "Wrong number of succs");
      if (BranchInst *Br = dyn_cast<BranchInst>(srcBBTerm)) {
        Br->setSuccessor(0, dstBB);
      }
      errs() << *srcBB;
    }

    Value *createFunction (Module *M, Function *F) {
      Function::ArgumentListType& Args = F->getArgumentList();
      std::vector<Type*> Params;
      for (auto b = Args.begin(), e = Args.end(); b != e; ++b) {
        Params.push_back(b->getType());
      }
      
      return M->getOrInsertFunction(F->getName(), F->getFunctionType(), F->getAttributes());
    }



    BasicBlock *createJointBB (Function &F) {
      // Create joint BB
      LLVMContext& Context = F.getParent()->getContext();
      BasicBlock *jointBB = BasicBlock::Create (Context, "JointBB", &F);
      //F.getBasicBlockList().push_back(jointBB);
      return jointBB;
    }

    BranchInst *createCondBr (BasicBlock* jointBB, TerminatorInst *BBTerm, 
                               BasicBlock *Succ1, BasicBlock *Succ2, IRBuilder<> &Builder) {
      // Insert branch into joint BB
      errs() << *jointBB;
      //IRBuilder<> Builder(jointBB);
      if (BranchInst *Br = dyn_cast<BranchInst>(BBTerm)) {
        errs() << "Creating Cond...\n";
        // We do this to silence a warning because we don't use
        // the return of CreateCondBr.
        return Builder.CreateCondBr(Br->getCondition(), Succ1, Succ2);
        errs() << *jointBB;
      }

      return NULL;
    }


    std::vector<Value*> createPHIArgs (size_t nrArgs, BasicBlock *jointBB, 
                                         BasicBlock *BB, IRBuilder<> &Builder) {
      // When we split a basic block, we make sure that the
      // first instruction of the second half of the split block
      // is a function call. What we want to do is to create PHINodes
      // with arguments that have the same types as the types of the
      // function parameters.
      std::vector<Value*> Args;
      //IRBuilder<> Builder(jointBB->getFirstInsertionPt());
      //Builder.SetInsertPoint(jointBB->getFirstInsertionPt());
      //errs() << "TERMINATOR " << *jointBB->getTerminator() << "\n";
      errs() << "JOINT ";
      errs() << *jointBB;
      
      if (CallInst *C = dyn_cast<CallInst>(BB->getFirstNonPHI())) {
        for (size_t i = 0; i < nrArgs; i++) {
          if (GlobalValue *GV = dyn_cast<GlobalValue>(C->getArgOperand(i))) {
            Args.push_back(GV);
            errs() << "GVGVGVGVG\n";
          } else if (AllocaInst *A = dyn_cast<AllocaInst>(C->getArgOperand(i))) {
            Args.push_back(A);
            errs() << "ALLOCA\n";
          } else {
            Type *FType = C->getArgOperand(i)->getType();
            Args.push_back(Builder.CreatePHI(FType, nrArgs));
          }
        }
      }
      return Args;
    }

    void addPHIOperands (std::vector<Value*> &Args, BasicBlock *newTrueBB,
                        BasicBlock *newFalseBB, BasicBlock *oldTrueBB,
                        BasicBlock *oldFalseBB) {

      CallInst *CT = dyn_cast<CallInst>(newTrueBB->getFirstNonPHI());
      CallInst *CF = dyn_cast<CallInst>(newFalseBB->getFirstNonPHI());
      assert((CT != NULL && CF != NULL) && "No call at BB's beginning!");
      errs() << "F ";
      //oldTrueBB->getParent()->dump();
      std::vector<int> toRemove;
      for (size_t j = 0; j < Args.size(); j++) {
        errs() << "args? " << Args.size() << "\n";
          if (PHINode *Phi = dyn_cast<PHINode>(Args[j])) {
            errs() << "CT " << *CT->getArgOperand(j) << "\n";
            errs() << "CF " << *CF->getArgOperand(j) << "\n";
            Phi->addIncoming(CT->getArgOperand(j), oldTrueBB);
            Phi->addIncoming(CF->getArgOperand(j), oldFalseBB);
          }
      }

      for (size_t i = 0; i < Args.size(); i++) {
        if (PHINode *Phi = dyn_cast<PHINode>(Args[i])) {
          if (Phi->getNumOperands() == 0)
            Phi->eraseFromParent();
        }
        //PHIs[toRemove[i]]->eraseFromParent();
        //PHIs.erase(PHIs.begin() + toRemove[i]);
      }

      for (size_t j = 0; j < Args.size(); j++) {
        errs () << "PHIIIII: " << *Args[j] << "\n"; 

      }
    }

    void cleanUpCalls (CallInst *F, BasicBlock *TBB, BasicBlock *FBB) {
      CallInst *CT = dyn_cast<CallInst>(TBB->getFirstNonPHI());
      CallInst *CF = dyn_cast<CallInst>(FBB->getFirstNonPHI());
      assert((CT != NULL && CF != NULL) && "No call at BB's beginning!");
      CT->replaceAllUsesWith(F);
      CT->eraseFromParent();
      CF->replaceAllUsesWith(F);          
      CF->eraseFromParent();
    }


    // replaceUsesOfWithAfter
    void replaceUsesOfWithAfter(Value *V, Value *R, BasicBlock *BB) {
    errs() << "Redefinition: replaceUsesOfWithAfter: " << *V << " ==> "
                      << *R << " after " << BB->getName() << "\n";

    std::vector<Instruction*> Replace;
    for (auto UI = V->use_begin(), UE = V->use_end(); UI != UE; ++UI)
      if (Instruction *I = dyn_cast<Instruction>(*UI)) {
        // If the instruction's parent dominates BB, mark the instruction to
        // be replaced.
        if (I != R && DT_->dominates(BB, I->getParent())) {
          Replace.push_back(I);
        }
        // If parent does not dominate BB, check if the use is a phi and replace
        // the incoming value.
        else if (PHINode *Phi = dyn_cast<PHINode>(*UI))
          for (unsigned Idx = 0; Idx < Phi->getNumIncomingValues(); ++Idx)
            if (Phi->getIncomingValue(Idx) == V &&
                DT_->dominates(BB, Phi->getIncomingBlock(Idx)))
              Phi->setIncomingValue(Idx, R);
      }

      // Replace V with R on all marked instructions.
      for (auto& I : Replace) {
        errs() << "Replacing " << *V << " ==> " << *R << " in " << *I << "\n";
        I->replaceUsesOfWith(V, R);
      }
    }


    void fixDominators (BasicBlock *Pred1, BasicBlock *Pred2, BasicBlock *Succ, IRBuilder<> &Builder) {
      //DT_->verifyDomTree();
      errs() << "SUCC " << Succ->getParent()->getName(); 
      for (auto b = Succ->begin(), e = Succ->end(); b != e; ++b)
        errs() << *b << "\n";
      //IRBuilder<> Builder(Succ);

      //Builder.SetInsertPoint(Succ->getFirstInsertionPt());
      //Builder.SetInsertPoint(Succ->getFirstInsertionPt());
      for (auto I = Pred1->begin(), E = Pred1->end(); I != E; ++I) {
        if (!I->isUsedOutsideOfBlock(Pred1))
          continue;

        // We'll always have two arguments: an instruction and an Undef value.
        errs() << "PHI UNDEF: " << *I->getType() <<  "  " << *I << "\n";
        //PHINode *Phi = NULL;
        //Value *Undef = NULL;
        //if (ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(I)) {
        //  errs() << "PHI TYPE: " << *EVI->getOperand(0)->getType() <<  "  " << *I << "\n";
        //  Phi = Builder.CreatePHI(EVI->getOperand(0)->getType(), 2);
        //  Undef = UndefValue::get(EVI->getOperand(0)->getType());
        //  Phi->addIncoming(EVI, Pred1);
        //} else {
          PHINode *Phi = Builder.CreatePHI(I->getType(), 2);
          Value *Undef = UndefValue::get(I->getType());
          Phi->addIncoming(I, Pred1);
        //}
        Phi->addIncoming(Undef, Pred2);
        
        for (auto UI = I->use_begin(), UE = I->use_end(); UI != UE; ++UI) {
          if (Instruction *II = dyn_cast<Instruction>(*UI)) {
            if (Phi->getParent() != II->getParent() && DT_->dominates(Phi, II)) {
              errs() << "Phi's parent " << Phi->getParent()->getName() << " II's parent " << II->getParent()->getName() << "\n";
              errs() << "Replacing " << *I << " with " << *Phi << " in " << *II << "\n";

              II->replaceUsesOfWith(I, Phi);
              replaceUsesOfWithAfter(I, Phi, Succ);
            }
          }
        }
      }
    }

    //inline void changeIdom (BasicBlock *BB, BasicBlock *Idom) {
    //  DT_->changeImmediateDominator(BB, Idom);
   // }
    
    virtual bool runOnFunction(Function &F) {
      //if (F.getName() == "main")
      //  return true;
      //F.viewCFG();
      NumberOfFunctions++;
      DT_ = &getAnalysis<DominatorTree>();
      DF_ = &getAnalysis<DominanceFrontier>();
      std::vector<Function*> TrueBBFunctions;
      std::vector<Function*> FalseBBFunctions;
      std::vector<BasicBlock*> BBList;

      // Create a list of Basic Blocks to iterate over.
      // This strategy avoids invalidating iterators.
      for (auto bgn = F.begin(), end = F.end(); bgn != end; ++bgn)
        BBList.push_back(bgn);
      //std::copy(F.begin(), F.end(), BBList.begin());
      bool changed = false;
      do {

        for (auto BB : BBList) {
          changed = false;
          // Handle newly created BBs.
          if (BB->getTerminator() == NULL)
            continue;


          // We only analyze blocks that have two successors.
          TerminatorInst *BBTerm = BB->getTerminator();
          if (BBTerm->getNumSuccessors() != 2)
            continue;

          // We don't evaluate 'invoke' terminated basic blocks.
          InvokeInst *In = NULL;
          In = dyn_cast<InvokeInst>(BBTerm);
          if (In != NULL) {
            errs() << "NULL, motherfucker!";
            continue;
          } 


          // Get the successors of the block we're iterating over.
          BasicBlock *oldTrueBB = BBTerm->getSuccessor(0);
          BasicBlock *oldFalseBB = BBTerm->getSuccessor(1);

          //getCandidates(TrueBBFunctions, oldTrueBB);
          //getCandidates(FalseBBFunctions, oldFalseBB);


          // Get the functions that we can optimize.
          getFunctionsToOptimize(TrueBBFunctions, FalseBBFunctions,
                                 oldTrueBB, oldFalseBB);



          errs() << "Candidates: " << Worklist.size() << "\n";
          for (size_t i = 0; i < Worklist.size() && !changed; i++) {
            Function *f = Worklist[i];
            // Apply the optimization to the functions found above.
            errs() << f->getName() << "\n";
            BasicBlock *newTrueBB = splitBB(oldTrueBB, f);
            assert(newTrueBB != NULL && "Couldn't find the split point.");
            BasicBlock *newFalseBB = splitBB(oldFalseBB, f);
            assert(newFalseBB != NULL && "Couldn't find the split point.");

            //if (newTrueBB == NULL || newFalseBB == NULL)
            //  changed = false;

            if (newTrueBB && newFalseBB)
              changed = true;

            

            // Create the joint BB and add its successors.
            BasicBlock *jointBB = createJointBB (F);
            
            IRBuilder<> Builder(jointBB);
            
            
            // Make the BBs that were split point the joint BB.
            insertUncondBranch (oldTrueBB, jointBB);
            insertUncondBranch (oldFalseBB, jointBB);
            
            errs() << "JOINT: " << *jointBB << "\n";
            

            //jointBB->getInstList().insert(jointBB->end(), B);

            // Create a new function copying the prototype of the candidate function.
            Value *newF = createFunction (F.getParent(), f);



            DT_->addNewBlock(jointBB, BB);

            DT_->changeImmediateDominator(newFalseBB, jointBB);
            DT_->changeImmediateDominator(newTrueBB, jointBB);
            //Builder.SetInsertPoint(B);
            fixDominators(oldTrueBB, oldFalseBB, jointBB, Builder);
            errs() << "XXXXXXXXXXXXXXX\n";
            errs() << F.getName();
            fixDominators(oldFalseBB, oldTrueBB, jointBB, Builder);



            size_t args = f->getArgumentList().size();
            std::vector<Value*> FArgs = createPHIArgs(args, jointBB, newTrueBB, Builder);
            assert(!FArgs.empty() && "There has to be an argument!");

            
            errs() << "Progress!\n";
            // Create function call
            std::vector<Value*> newArgs(FArgs.begin(), FArgs.end());
            

            errs() << "FFFF: " << f->isDeclaration() << *f << "\n";

            //Builder.SetInsertPoint(B);
            //Builder.SetInsertPoint(B);
            CallInst* NF = Builder.CreateCall(newF, newArgs, "");

            BranchInst *B = createCondBr(jointBB, BBTerm, newTrueBB, newFalseBB, Builder);
            if (B == NULL)
              errs() << "BBTERM: " << *BBTerm << "\n";
            assert(B != NULL && "oops!");


            addPHIOperands(FArgs, newTrueBB, newFalseBB, oldTrueBB, oldFalseBB);



            cleanUpCalls (NF, newTrueBB, newFalseBB);

            

            errs() << "Got here!\n";
            //F.dump();

          }

          Worklist.clear();
          TrueBBFunctions.clear();
          FalseBBFunctions.clear();

          if (changed)
            break;
        }
      } while (changed);
      //F.dump();
      //F.viewCFG();
      isAlive++;
      return true;
    }

    // We do modify the program, so we don't preserve the analyses.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DominatorTree>();
      AU.addRequired<DominanceFrontier>();
    }
  };
}

char FunctionFusion::ID = 0;
static RegisterPass<FunctionFusion> Y("func-fusion", "Function Fusion Pass");
