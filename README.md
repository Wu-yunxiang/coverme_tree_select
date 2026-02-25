# CoverMe Tree Select

基于树搜索策略的代码覆盖率估算工具。

### 1. 快速构建
```bash
rm -rf build && cmake -S . -B build && cmake --build build
```

### 2. 运行测试
```bash
python3 src/coverage_algorithm.py -n 5 --stepSize 300
```

### 3. 查看结果
测试生成的有效输入将保存在 `output/effective_input.txt` 中。
