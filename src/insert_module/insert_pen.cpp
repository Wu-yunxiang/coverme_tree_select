// LLVM 核心类
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/DominatorTree.h"
#include "llvm/IR/CFG.h"

// C++ 标准库
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <queue>
#include <algorithm>
#include <functional> // 必须包含，用于 std::function

using namespace llvm;

cl::opt<std::string> funcname("funcname", cl::desc("Specify function name"), cl::value_desc("funcname"));
extern int brCount;  // 全局分支/select计数
extern int argCount;
struct InsertPenPass : public ModulePass {
    static char ID;
    InsertPenPass() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        for (Function &F : M) {
            if (F.getName() == funcname) {
                int argCount = F.arg_size();
                // ---------- 第一阶段：收集所有分支/select指令并分配ID ----------
                std::map<Instruction*, int> instToId;          // 指令 -> 出口基ID
                std::vector<Instruction*> allBranches;         // 所有分支/select指令
                std::map<Instruction*, BasicBlock*> instToBB;  // 指令所在基本块

                for (BasicBlock &BB : F) {
                    for (Instruction &I : BB) {
                        Instruction *inst = &I;
                        // 排除间接跳转和Switch，只处理条件分支和Select
                        if (isa<IndirectBrInst>(inst) || isa<SwitchInst>(inst)) continue;
                        
                        if (BranchInst *BI = dyn_cast<BranchInst>(inst)) {
                            if (BI->isConditional()) {
                                instToId[inst] = brCount++;
                                allBranches.push_back(inst);
                                instToBB[inst] = &BB;
                            }
                        } else if (SelectInst *SI = dyn_cast<SelectInst>(inst)) {
                            instToId[inst] = brCount++;
                            allBranches.push_back(inst);
                            instToBB[inst] = &BB;
                        }
                    }
                }
                int totalBr = brCount;          // 总分支/select指令数
                // int totalExits = 2 * totalBr; // 总出口数 (变量保留但在此逻辑中主要作为Offset使用)
                int ROOT = -1;                  // 虚拟根节点ID，标记无父节点的情况

                // ---------- 第二阶段：建立基本块索引和支配树 ----------
                std::map<BasicBlock*, int> blockIdx;
                std::vector<BasicBlock*> blocks;
                for (BasicBlock &BB : F) {
                    blockIdx[&BB] = blocks.size();
                    blocks.push_back(&BB);
                }
                int numBlocks = blocks.size();

                DominatorTree DT;
                DT.recalculate(F);

                // ---------- 第三阶段：计算每个基本块的直接父节点（控制它的分支出口）----------
                // 优化算法：基于支配树的 DFS 遍历
                // parentBlock 存储每个基本块对应的“最近必经条件出口ID”
                std::vector<int> parentBlock(numBlocks, ROOT); 

                // 定义 DFS 函数：向下传递当前的必经条件 ID
                std::function<void(DomTreeNode*, int)> dfsFindParent = 
                    [&](DomTreeNode *Node, int currentParentID) {
                    
                    BasicBlock *BB = Node->getBlock();
                    int myParentID = currentParentID; // 默认继承父节点的必经条件

                    // 如果不是支配树根节点，检查是否被 IDom 的某个分支严格控制
                    if (Node->getIDom()) {
                        BasicBlock *domBB = Node->getIDom()->getBlock();
                        Instruction *terminator = domBB->getTerminator();

                        // 只有当支配块以条件分支结束，且我们记录了该分支（分配了ID）时才进行判断
                        if (BranchInst *BI = dyn_cast<BranchInst>(terminator)) {
                            if (BI->isConditional() && instToId.count(BI)) {
                                BasicBlock *trueSucc = BI->getSuccessor(0);
                                BasicBlock *falseSucc = BI->getSuccessor(1);

                                // 利用支配关系判断：
                                // 如果 True 分支的后继支配当前块 BB，且 False 分支后继不支配，则必经 True 边
                                bool governedByTrue = DT.dominates(trueSucc, BB);
                                bool governedByFalse = DT.dominates(falseSucc, BB);

                                int brId = instToId[BI];

                                if (governedByTrue && !governedByFalse) {
                                    myParentID = brId; // 更新为 True 出口 ID
                                } else if (!governedByTrue && governedByFalse) {
                                    myParentID = brId + totalBr; // 更新为 False 出口 ID
                                }
                                // 否则（如汇聚点），保持 inheritedParentID
                            }
                        }
                    }

                    // 记录结果
                    if (blockIdx.count(BB)) {
                        parentBlock[blockIdx[BB]] = myParentID;
                    }

                    // 递归处理子节点
                    for (DomTreeNode *Child : *Node) {
                        dfsFindParent(Child, myParentID);
                    }
                };

                // 从支配树根节点开始遍历
                if (DT.getRootNode()) {
                    dfsFindParent(DT.getRootNode(), ROOT);
                }

                // ---------- 第四阶段：输出边信息 ----------
                std::ofstream edgeFile;
                edgeFile.open("to do (by configs)", std::ofstream::out | std::ofstream::trunc);
                
                for (Instruction *inst : allBranches) {
                    int id = instToId[inst];
                    BasicBlock *BB = instToBB[inst];
                    
                    // 获取该指令所在基本块的必经父节点
                    int parent = parentBlock[blockIdx[BB]];

                    // 只有当存在有效的父节点（不是根）时才输出
                    if (parent != ROOT) {
                        int trueExit = id;
                        int falseExit = id + totalBr;
                        
                        edgeFile << parent << "\t" << trueExit << "\n";
                        edgeFile << parent << "\t" << falseExit << "\n";
                    }
                }
                edgeFile.close();

                // ---------- 第五阶段：原有的插桩逻辑（保持不变） ----------
                for (Instruction *inst : allBranches) {
                    CmpInst *cmpInst = nullptr;
                    if (BranchInst *BI = dyn_cast<BranchInst>(inst)) {
                        cmpInst = dyn_cast<CmpInst>(BI->getCondition());
                    } else if (SelectInst *SI = dyn_cast<SelectInst>(inst)) {
                        cmpInst = dyn_cast<CmpInst>(SI->getCondition());
                    } else {
                        continue;
                    }

                    if (!cmpInst) continue;

                    Value *LHS = cmpInst->getOperand(0);
                    Value *RHS = cmpInst->getOperand(1);
                    std::vector<Value*> call_params;
                    IRBuilder<> builder(inst);
                    
                    // 准备操作数，转换为 double
                    Value *LHS_Double = LHS;
                    Value *RHS_Double = RHS;

                    // 处理整数比较的转换
                    if (isa<ICmpInst>(cmpInst)) {
                        // 如果类型不是 double，进行转换
                        if (!LHS->getType()->isDoubleTy()) {
                             LHS_Double = builder.CreateSIToFP(LHS, Type::getDoubleTy(M.getContext()), "__LHS");
                        }
                        if (!RHS->getType()->isDoubleTy()) {
                             RHS_Double = builder.CreateSIToFP(RHS, Type::getDoubleTy(M.getContext()), "__RHS");
                        }
                    } 
                    // 处理浮点数比较（如果是 float 需要扩展到 double）
                    else if (LHS->getType()->isFloatTy()) {
                        LHS_Double = builder.CreateFPExt(LHS, Type::getDoubleTy(M.getContext()), "__LHS");
                        RHS_Double = builder.CreateFPExt(RHS, Type::getDoubleTy(M.getContext()), "__RHS");
                    }

                    call_params.push_back(LHS_Double);
                    call_params.push_back(RHS_Double);

                    int brId = instToId[inst];
                    int cmpId = cmpInst->getPredicate();
                    int isInt = isa<ICmpInst>(cmpInst);

                    ConstantInt* brId_32 = ConstantInt::get(Type::getInt32Ty(M.getContext()), brId, false);
                    ConstantInt* cmpId_32 = ConstantInt::get(Type::getInt32Ty(M.getContext()), cmpId, false);
                    ConstantInt* isInt_1 = ConstantInt::getBool(M.getContext(), isInt); // i1
                    // 注意：插桩函数签名通常要求 i32 用于 bool 参数，或者保持 i1，这里根据原代码逻辑适配
                    // 原代码示例：Type::getInt1Ty(M.getContext()) 对应参数
                    
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

                return true;
            }
        }
        return false;
    }
};

char InsertPenPass::ID = 0;
static RegisterPass<InsertPenPass> X("InsertPenPass", 
    "Insert __pen calls at conditional branches");