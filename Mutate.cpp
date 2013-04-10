// -*- tags-file-name:"../../../tags" -*-
// Copyright (C) 2013 Eric Schulte
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

static cl::opt<unsigned>
Inst1("inst1", cl::init(0), cl::desc("first statement to mutate"));

static cl::opt<unsigned>
Inst2("inst2", cl::init(0), cl::desc("second statement to mutate"));

// Find a value of Type T which can be used at Instruction I.  Search
// in this order.
// 1. values in Basic Block before I
// 2. arguments to the function containing I
// 3. global values
// 4. null of the correct type
// 5. return a 0 that the caller can stick where the sun don't shine
Value *findInstanceOfType(Instruction *I, Type *T){
  // local inside the Basic Block
  BasicBlock *B = I->getParent();
  for (BasicBlock::iterator prev = B->begin(); cast<Value>(prev) != I; ++prev){
    if(prev->getType() == T){
      errs()<<"found local replacement: "<<prev<<"\n";
      return cast<Value>(prev); } }
  // arguments to the function
  Function *F = B->getParent();
  for (Function::arg_iterator arg = F->arg_begin(), E = F->arg_end();
       arg != E; ++arg){
    if(arg->getType() == T){
      errs()<<"found arg replacement: "<<arg<<"\n";
      return cast<Value>(arg); } }
  // global values
  Module *M = F->getParent();
  for (Module::global_iterator g = M->global_begin(), E = M->global_end();
       g != E; ++g){
    if(g->getType() == T){
      errs()<<"found global replacement: "<<g<<"\n";
      return cast<Value>(g); } }
  // null
  if(!isa<FunctionType>(T)){
    return Constant::getNullValue(T);
  }
  errs()<<"findInstanceOfType failed to find anything, you're screwed\n";
  return 0;
}

// Replace the operands of Instruction I with in-scope values of the
// same type.
// 
// NOTE: this might be relevant
//   RemapInstruction(C, ValueMap, RF_NoModuleLevelChanges);
void replaceOperands(Instruction *I){
  // loop through operands,
  for (User::op_iterator i = I->op_begin(), e = I->op_end(); i != e; ++i) {
    Value *v = *i;

    // don't touch global values
    if (!isa<GlobalValue>(v)) {

      // don't touch arguments to the current function
      // TODO: convert Arg list to a set
      // if(!I->getParent()->getParent()->getArgumentList().contains(v))

      // Don't touch arguments which are in scope, we can check this
      // by walking the basic block up to this point.
      BasicBlock *B = I->getParent();
      bool isInScope = false;
      for (BasicBlock::iterator i = B->begin(); cast<Instruction>(i) != I; ++i)
        if(i == v) { isInScope = true; break; }

      if(!isInScope){
        // If we've made it this far we really do have to find a replacement
        v = findInstanceOfType(I, v->getType()); } } }
}

namespace {
  struct Count : public ModulePass {
    static char ID;
    Count() : ModulePass(ID) {}

    bool runOnModule(Module &M){
      count = 0;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        walkFunction(I);

      errs() << count << "\n";

      return false;
    }

    // We don't modify the program, so we preserve all analyses
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll(); }

  private:
    int unsigned count;
    void walkFunction(Function *F){
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        count += 1; }
  };
}

namespace {
  struct List : public ModulePass {
    static char ID;
    List() : ModulePass(ID) {}

    bool runOnModule(Module &M){
      count = 0;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        walkFunction(I);
      return false;
    }

    // We don't modify the program, so we preserve all analyses
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll(); }

  private:
    int unsigned count;
    void walkFunction(Function *F){
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        count += 1;
        errs() << count << "\t" << I->getType();
        for (User::op_iterator i = I->op_begin(), e = I->op_end(); i != e; ++i)
          errs() << "\t" << cast<Value>(i)->getType();
        errs() << "\n"; } }
  };
}

namespace {
  struct Cut : public ModulePass {
    static char ID;
    Cut() : ModulePass(ID) {}

    bool runOnModule(Module &M){
      count = 0;
      changed_p = false;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        if(walkFunction(I)) break;

      if(changed_p) errs() << "cut " << Inst1 << "\n";
      else          errs() << "cut failed\n";

      return changed_p; }

  private:
    int unsigned count;
    bool changed_p;
    bool walkFunction(Function *F){
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        count += 1;
        if(count == Inst1){
          // TODO: instead of replacing with null, replace with nearby value
          I->replaceAllUsesWith(Constant::getNullValue(I->getType()));
          if(!I->use_empty()){errs()<<"dangling uses after cut\n"; }
          I->eraseFromParent();
          changed_p = true;
          return true; } }
      return false; }

  };
}

namespace {
  struct Insert : public ModulePass {
    static char ID;
    Insert() : ModulePass(ID) {}

    bool runOnModule(Module &M){
      count = 0;
      changed_p = false;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        if(walkCollect(I)) break;
      count = 0;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        if(walkPlace(I)) break;

      if(changed_p) errs()<<"inserted "<<Inst2<<" before "<<Inst1<<"\n";
      else          errs()<<"insertion failed\n";

      return changed_p; }

  private:
    int unsigned count;
    bool changed_p;
    BasicBlock::iterator temp;

    bool walkCollect(Function *F){
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        count += 1;
        if(count == Inst2) {
          temp = I->clone();
          if (!temp->getType()->isVoidTy())
            temp->setName(I->getName()+".insert");
          return true; } }
      return false; }

    bool walkPlace(Function *F){
      for (Function::iterator B = F->begin(), E = F->end(); B != E; ++B) {
        for (BasicBlock::iterator I = B->begin(), E = B->end(); I != E; ++I) {
          count += 1;
          if(count == Inst1){
            temp->insertBefore(I); // insert temp before I
            replaceOperands(temp); // wire incoming edges of CFG into temp
            useResult(I);          // wire outgoing results of temp into CFG
            changed_p = true;
            return true; } } }
      return false; }

    // Use the result of Instruction I later in the Function in which it
    // is inserted.
    void useResult(Instruction *I){
      
    }
  };
}

namespace {
  struct Swap : public ModulePass {
    static char ID; // pass identification
    Swap() : ModulePass(ID) {}

    // Count up all operations in the module
    bool runOnModule(Module &M){
      count = 0;
      changed_p = false;
      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        walkCollect(I);

      count = 0;
      // confirm that the types match
      if(temp1->getType() != temp2->getType()){
        errs() << "type mismatch " <<
          temp1->getType() << " and " <<
          temp2->getType() << "\n";
        delete(temp1);
        delete(temp2);
        return changed_p; }

      for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
        if(walkPlace(I)) break;

      if(changed_p) errs() << "swapped " << Inst1 << " with " << Inst2 << "\n";
      else          errs() << "swap failed\n";

      return changed_p; }

  private:
    int unsigned count;
    bool changed_p;
    BasicBlock::iterator temp1, temp2;

    void walkCollect(Function *F){
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        count += 1;
        if (count == Inst1) {
          temp1 = I->clone();
          if (!temp1->getType()->isVoidTy()) {
            temp1->setName(I->getName()+".swap1"); } }
        if (count == Inst2) {
          temp2 = I->clone();
          if (!temp2->getType()->isVoidTy()){
            temp2->setName(I->getName()+".swap2"); } } } }

    bool walkPlace(Function *F){
      for (Function::iterator B = F->begin(), E = F->end(); B != E; ++B) {
        for (BasicBlock::iterator I = B->begin(), E = B->end(); I != E; ++I) {
          count += 1;
          if(count == Inst2){
            ReplaceInstWithInst(I->getParent()->getInstList(), I, temp1);
            replaceOperands(temp1);
            if(changed_p) return true;
            changed_p = true; }
          if(count == Inst1){
            ReplaceInstWithInst(I->getParent()->getInstList(), I, temp2);
            replaceOperands(temp2);
            if(changed_p) return true;
            changed_p = true; } } }
      return false; }
  };
}

char Count::ID = 0;
char List::ID = 0;
char Cut::ID = 0;
char Insert::ID = 0;
char Swap::ID = 0;
static RegisterPass<Count>  V("count",  "count the number of instructions");
static RegisterPass<List>   W("list",   "list instruction types w/counts");
static RegisterPass<Cut>    X("cut",    "cut instruction number inst1");
static RegisterPass<Insert> Y("insert", "insert inst2 before inst1");
static RegisterPass<Swap>   Z("swap",   "swap inst1 and inst2");
