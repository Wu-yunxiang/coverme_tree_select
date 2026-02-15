// LLVM 核心类
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/DominatorTree.h"  // 使用支配树
#include "llvm/IR/CFG.h"

// C++ 标准库
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <queue>

using namespace llvm;

cl::opt<std::string> funcname("funcname", cl::desc("Specify function name"), cl::value_desc("funcname"));
extern int tree_node_count;

struct InsertPenPass : public ModulePass {
    static char ID;
    InsertPenPass() : ModulePass(ID) {}

    // 存储每个分支/选择出口的必经条件：(id, bool) -> 有序的 (id, bool) 列表
    std::map<std::pair<int, bool>, std::vector<std::pair<int, bool>>> branchExitConds;

    bool runOnModule(Module &M) override {
        for (Function &F : M) {
            if (F.getName() == funcname) {
                int brCount = 0;
                std::map<Instruction*, int> instToId;          // 指令 -> ID
                std::vector<Instruction*> allBranches;        // 所有分支/选择指令

                // 第一阶段：分配ID并收集所有分支/选择指令（条件分支和select）
                for (BasicBlock &BB : F) {
                    for (Instruction &I : BB) {
                        Instruction *inst = &I;
                        // 跳过间接跳转和switch（保持原接口）
                        if (isa<IndirectBrInst>(inst) || isa<SwitchInst>(inst)) continue;
                        
                        if (BranchInst *BI = dyn_cast<BranchInst>(inst)) {
                            if (BI->isConditional()) {
                                instToId[inst] = brCount++;
                                allBranches.push_back(inst);
                            }
                        } else if (isa<SelectInst>(inst)) {
                            instToId[inst] = brCount++;
                            allBranches.push_back(inst);
                        }
                    }
                }

                // 第二阶段：计算每个基本块的必经条件
                DominatorTree DT;
                DT.recalculate(F);

                // 映射：基本块 -> 必经条件列表（按支配顺序排列）
                std::map<BasicBlock*, std::vector<std::pair<int, bool>>> BB_mustCond;

                for (BasicBlock &BB : F) {
                    // 计算反向可达集：所有能到达BB的块
                    std::set<BasicBlock*> reach;
                    std::queue<BasicBlock*> worklist;
                    reach.insert(&BB);
                    worklist.push(&BB);
                    while (!worklist.empty()) {
                        BasicBlock *cur = worklist.front();
                        worklist.pop();
                        for (BasicBlock *pred : predecessors(cur)) {
                            if (reach.insert(pred).second)
                                worklist.push(pred);
                        }
                    }

                    // 获取严格支配BB的所有节点（从根到BB，不包括BB本身）
                    std::vector<BasicBlock*> doms;
                    DomTreeNode *node = DT.getNode(&BB);
                    if (node) {
                        while (node->getIDom()) {
                            node = node->getIDom();
                            doms.push_back(node->getBlock());
                        }
                        std::reverse(doms.begin(), doms.end()); // 按执行顺序：根先
                    }

                    // 遍历严格支配者，找出必经条件
                    std::vector<std::pair<int, bool>> conds;
                    for (BasicBlock *X : doms) {
                        Instruction *term = X->getTerminator();
                        if (BranchInst *BI = dyn_cast<BranchInst>(term)) {
                            if (BI->isConditional() && instToId.count(BI)) {
                                int cnt = 0;
                                BasicBlock *uniqueSucc = nullptr;
                                for (unsigned i = 0; i < 2; ++i) {
                                    BasicBlock *succ = BI->getSuccessor(i);
                                    if (reach.count(succ)) {
                                        cnt++;
                                        uniqueSucc = succ;
                                    }
                                }
                                if (cnt == 1) {
                                    bool dir = (uniqueSucc == BI->getSuccessor(0)); // true 对应第一个后继
                                    conds.emplace_back(instToId[BI], dir);
                                }
                            }
                        }
                        // 其他类型分支（如switch）暂不处理，可扩展
                    }
                    BB_mustCond[&BB] = conds;
                }

                // 第三阶段：构建分支出口和select出口的必经条件映射
                for (Instruction *inst : allBranches) {
                    if (BranchInst *BI = dyn_cast<BranchInst>(inst)) {
                        if (BI->isConditional()) {
                            int id = instToId[BI];
                            BasicBlock *trueSucc = BI->getSuccessor(0);
                            BasicBlock *falseSucc = BI->getSuccessor(1);
                            branchExitConds[{id, true}] = BB_mustCond[trueSucc];
                            branchExitConds[{id, false}] = BB_mustCond[falseSucc];
                        }
                    } else if (SelectInst *SI = dyn_cast<SelectInst>(inst)) {
                        int id = instToId[SI];
                        BasicBlock *parent = SI->getParent();
                        // select 的两个“出口”都在同一个基本块，因此必经条件相同
                        branchExitConds[{id, true}] = BB_mustCond[parent];
                        branchExitConds[{id, false}] = BB_mustCond[parent];
                    }
                }

                // 第四阶段：原有的插桩逻辑（保持不变）
                std::ofstream myfile;
                myfile.open("to do (by configs)", std::ofstream::out | std::ofstream::trunc);

                for (Instruction *inst : allBranches) {
                    CmpInst *cmpInst = nullptr;
                    if (BranchInst *BI = dyn_cast<BranchInst>(inst)) {
                        cmpInst = cast<CmpInst>(BI->getCondition());
                    } else if (SelectInst *SI = dyn_cast<SelectInst>(inst)) {
                        cmpInst = cast<CmpInst>(SI->getCondition());
                    } else {
                        continue;
                    }

                    if (!cmpInst) continue;

                    Value *LHS = cmpInst->getOperand(0);
                    Value *RHS = cmpInst->getOperand(1);
                    std::vector<Value*> call_params;
                    IRBuilder<> builder(inst);
                    if (isa<ICmpInst>(cmpInst)) {
                        LHS = builder.CreateSIToFP(LHS, Type::getDoubleTy(M.getContext()), "__LHS");
                        RHS = builder.CreateSIToFP(RHS, Type::getDoubleTy(M.getContext()), "__RHS");
                    }
                    call_params.push_back(LHS);
                    call_params.push_back(RHS);

                    int brId = instToId[inst];
                    int cmpId = cmpInst->getPredicate();
                    int isInt = isa<ICmpInst>(cmpInst);

                    std::string str;
                    llvm::raw_string_ostream rso(str);
                    inst->print(rso);
                    myfile << brId << "\t" << cmpId << "\t" << isInt << "\t" << str << "\n";

                    ConstantInt* brId_32 = ConstantInt::get(Type::getInt32Ty(M.getContext()), brId, false);
                    ConstantInt* cmpId_32 = ConstantInt::get(Type::getInt32Ty(M.getContext()), cmpId, false);
                    ConstantInt* isInt_1 = ConstantInt::getBool(M.getContext(), isInt);
                    call_params.push_back(brId_32);
                    call_params.push_back(cmpId_32);
                    call_params.push_back(isInt_1);

                    std::vector<Type*> FuncTy_args = {
                        Type::getDoubleTy(M.getContext()),
                        Type::getDoubleTy(M.getContext()),
                        Type::getInt32Ty(M.getContext()),
                        Type::getInt32Ty(M.getContext()),
                        Type::getInt1Ty(M.getContext())
                    };

                    FunctionType* FuncTy = FunctionType::get(Type::getVoidTy(M.getContext()), FuncTy_args, false);
                    Function* func___pen = M.getFunction("__pen");
                    if (!func___pen) {
                        func___pen = Function::Create(FuncTy, Function::ExternalLinkage, "__pen", &M);
                        func___pen->setCallingConv(CallingConv::C);
                    }
                    builder.CreateCall(func___pen, call_params, "");
                }

                myfile.close();
                tree_node_count = brCount;
                return true;
            }
        }
        return false;
    }
};

char InsertPenPass::ID = 0;
static RegisterPass<InsertPenPass> X("InsertPenPass", 
    "Insert __pen calls at conditional branches");