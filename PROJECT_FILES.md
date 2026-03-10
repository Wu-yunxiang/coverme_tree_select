# CoverMe Tree Select 项目文件说明文档

本项目是一个基于 LLVM 插桩和 BasinHopping 优化算法的覆盖率引导模糊测试（Coverage-guided Fuzzing）工具。本文档详细说明了程序运行过程中涉及的关键文件及其组织格式。

## 1. 核心数据文件 (Output 目录)

所有运行结果和诊断信息均保存在 `output/` 目录下。

### 1.1 `seed_info.txt`
**用途**：记录探索过程中发现每一个新种子（Seed）时的详细诊断信息。
**格式**：
```text
Seed [编号]: [坐标1],[坐标2],...
  Target: [当前任务目标节点], NewlyCoveredNode: [实际触发覆盖的节点]
  Call count since Initial_X: [距离上一次随机起点后的函数调用次数]
  Closest 1: [邻居坐标] (Satisfied: [前缀满足数]/[总条件数], Dist: [分支距离], NewlyCovered: [该邻居新覆盖数])
    Initial_X of Closest: [产生该邻居时的随机探索起点]
    Ratio: [位置比例] (当前位置/历史总数)
... (重复前5个最近邻居)
Initial_X: [当前Seed的随机探索起点]
--------------------
```
**含义**：用于分析新种子是如何产生的，以及它与已有种子在空间上的距离和逻辑上的相似度。

### 1.2 `solve_info.txt`
**用途**：程序结束后的“事后分析”报告。
**格式**：
```text
Seed [编号] (Target Node: [节点ID]):
  Closest 1 Solve: Success/Failed
  ...
====================
```
**含义**：验证当时距离新种子最近的“老种子”是否具备直接求解出该目标的能力。如果 Solve 成功，说明该路径本可以更早被触达。

### 1.3 `effective_input.txt`
**用途**：保存所有触发了新覆盖的有效输入向量。
**格式**：每行一个输入向量，元素间用逗号分隔。例：`0.5,1.2,-3.4`。

### 1.4 `solve_data.tmp` (临时文件)
**用途**：在主循环和求解验证阶段之间传递数据。
**格式**：`SeedID|TargetNode|Closest1_X|Closest2_X...`
**含义**：二进制/文本混合的临时记录，程序正常结束后建议保留以供断点调试。

## 2. 源代码组件 (Src 目录)

### 2.1 `coverage_algorithm.py`
**脚本核心**：
- 使用 `scipy.optimize.basinhopping` 进行全局寻优。
- 定义了 `func_py` 作为优化目标函数，负责与 C++ 库进行数据交互。
- 维护 `all_seeds` 和 `all_initial_x` 历史记录。

### 2.2 `insert_module/` (C++ 后端)
- **`pen.cpp`**: 插桩核心逻辑。计算分支距离（Branch Distance），并根据 `isSelfMode` 或 `isGetBase` 切换不同的距离反馈机制。
- **`interface_for_py.cpp`**: 导出 C 接口。提供 `set_target_direct`（强制设置目标并清除覆盖记录）、`get_node_status`（获取节点实时状态）等函数。
- **`branch_tree.h`**: 维护被测程序的控制流图（CFG）和分支前缀依赖关系。

## 3. 运行逻辑概览

1.  **准备阶段**：程序启动时会自动清空上述所有 `.txt`和 `.tmp` 文件，确保实验数据独立。
2.  **探索阶段**：`basinhopping` 随机生成 `x0` (Initial_X)，尝试随机目标。每当发现 `FLAG_NEW_COVERAGE` 时，调用 `record_seed_info` 寻找最近邻居并记录数据。
3.  **验证阶段**：主循环达到覆盖率阈值后，读取 `solve_data.tmp`，对每个曾产生过新覆盖的位置，重新用其最近的 5 个邻居作为起点进行针对性求解，结果记录在 `solve_info.txt`。

---
*文档生成日期：2026-03-10*