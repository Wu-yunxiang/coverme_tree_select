# coverme_tree_select

一个基于 LLVM 插桩 + Python 优化（`scipy.optimize.basinhopping`）的分支覆盖实验项目。

## 1. 依赖

- Linux
- Python 3.10+
- CMake 3.16+
- LLVM/Clang/opt/llvm-config（要求 LLVM >= 18）
- Python 包：`numpy`、`scipy`

快速安装（示例）：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip
pip install numpy scipy
```

## 2. 配置目标程序

编辑 `target_input.txt`：

- 第 1 行：待测 C 文件**绝对路径**
- 第 2 行：待测函数名（会被插桩流程重命名）

示例：

```text
/home/wuyunxiang/coverme_tree_select/benchs/fdlibm53_e_pow/foo.c
foo_raw
```

## 3. 从零构建

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

构建完成后会生成：

- `build/lib/lib_coverage.so`（Python 侧加载）
- `output/instrumentation_meta.txt`
- `output/edges.txt`

## 4. 运行覆盖算法

```bash
python3 src/coverage_algorithm.py
```

可选参数：

```bash
python3 src/coverage_algorithm.py -n 5 --stepSize 300
```

输出示例：

- `Final coverage = xx.xx%`
- `Total process time = x.xx seconds`

## 5. 输出文件说明

- `output/new_coverage_params.txt`：触发新覆盖时记录的输入
- `output/effective_input.txt`：`seedId -> input` 映射记录
- `output/edges.txt`、`output/instrumentation_meta.txt`：插桩元数据

## 6. 快速排障

- 若报 `Target source does not exist`：检查 `target_input.txt` 绝对路径。
- 若报 `undefined symbol`：先重新构建：

```bash
cmake -S . -B build && cmake --build build
```

- 若覆盖率始终不变：确认目标函数确实被调用，且 `output/edges.txt` 非空。
