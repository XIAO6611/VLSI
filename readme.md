# FPGA Placement Solver (ISPD 2016)

基于模拟退火（Simulated Annealing）的 FPGA 布局工具，适用于 **ISPD 2016 时序驱动布局竞赛** 的约束格式。支持异构资源（SLICE、DSP、BRAM、IO）以及 LUT/FF 引脚共享、寄存器控制集等复杂合法化规则。

## 项目结构

```
.
├── Arch.cpp / Arch.h         # FPGA 芯片架构与 Block 定义
├── Object.cpp / Object.h     # 元件 (Instance) 与线网 (Net)
├── Global.cpp / Global.h     # 全局映射表（inst_map, net_map, fpga）
├── Solution.cpp / Solution.h # 布局主算法（初始放置 + 模拟退火）
├── main.cpp                  # 入口，读取数据，调用布局，输出结果
├── run_experiments.py        # 批量运行脚本（自动编译 + 测试所有数据集）
├── visualize.py              # 生成放置结果的可视化图片（散点图/密度热力图）
└── README.md                 # 本文件
```

## 功能特性

- 支持 **ISPD 2016 格式**：`design.scl`, `design.nodes`, `design.pl`, `design.nets`, `design.wts`
- 初始放置：**线性复杂度确定性填充**，按资源类型游标扫描，保证合法
- 合法化检查：完整实现了 LUT6/LUT1-5 共享输入引脚、寄存器控制集匹配、槽位互斥等规则
- 优化算法：模拟退火，**向心移动** + **局部抖动** + **主动寻找空槽位**
- 增量线长评估：仅重算受影响线网，加速 SA 迭代
- 自动输出：生成 `placement.txt`，包含每个元件的最终坐标 (x, y, z)
- Python 工具：批量运行所有数据集，自动收集 HPWL / Runtime，生成 Markdown 报告 + 可视化图片

## 环境依赖

### C++ 编译
- 支持 C++17 的编译器（GCC 7+, Clang 5+, MSVC 2017+）
- 无额外第三方库（仅使用 STL）

### Python 脚本（可选）
- Python 3.6+
- `matplotlib`, `numpy`（用于可视化）
  ```bash
  pip install matplotlib numpy
  ```

## 编译与运行

### 1. 手动编译

```bash
g++ -O3 -std=c++17 *.cpp -o placer
```

### 2. 运行单个数据集

```bash
./placer /path/to/dataset/ output.txt
```

- 输入路径为**数据集文件夹**（如 `./data/FPGA-example1/`）
- 输出文件包含放置结果，首行注释了 HPWL 和运行时间

### 3. 批量测试（推荐）

将多个数据集放入 `./data/` 目录（每个数据集一个子文件夹），运行：

```bash
python run_experiments.py
```

脚本会自动：
- 编译 C++ 代码
- 遍历 `./data/` 下所有子目录
- 对每个数据集运行布局，记录 HPWL 与耗时
- 生成 `./output/summary.md` 和 `./output/summary.txt`
- 为每个数据集生成可视化图片（`./output/<dataset>_visual.png`）

## 算法简述

### 初始放置
1. 将所有可移动元件按 **类型 → 控制集 → 连线度** 排序。
2. 为每种资源类型维护一个游标 `(cx, cy)`，顺序扫描 FPGA 网格。
3. 找到第一个能合法放置的 Block 和 Z 槽位，放置元件并推进游标。
4. 该步骤时间复杂度 **O(N)**，且保证所有合法元件都能被放置（若芯片容量充足）。

### 模拟退火
- **温度调度**：几何冷却 `T = T * alpha`，根据电路规模自动选择 `alpha`（0.90~0.98）和马尔可夫链长度 `L`。
- **邻域操作**（85% 概率）：
  - 向心移动（70%）：将元件移向其连接的其他元件的几何重心，加速收敛。
  - 随机抖动（30%）：在随温度缩小的窗口内随机移动。
- **交换操作**（15% 概率）：在相同资源类型的元件间随机交换位置。
- **增量评估**：只计算移动涉及的两个元件的所有线网 HPWL 变化，避免全量重算。
- **接受准则**：Metropolis 准则 `exp(-delta/T)`。
- **精英保留**：始终记录历史最优解，退火结束后回滚。

### 合法化细节
- 支持 SLICE（LUT6, LUT1-5, FDRE, CARRY8）、DSP、BRAM、IO 四类 Block。
- LUT6 必须放置在奇数槽位，且低一位槽位空闲（引脚共享约束）。
- LUT1-5 与相邻 LUT 共享输入引脚时，总不同线网数不超过 6。
- 同一 SLICE 内的所有 FDRE 必须具有相同的 C/R/CE 控制信号。

## 结果输出格式

输出文件 `output.txt` 示例：
```
# HPWL: 123456
# Runtime: 4.567 s
inst_1 5 3 0
inst_2 8 2 16
...
```

- 每行：`<实例名> <x> <y> <z>`
- `z` 表示在 Block 内的槽位索引（0~63，依据类型决定范围）

## 配置调优

可以在 `solvePlacement()` 函数中修改退火参数：

```cpp
// 质量模式（默认，适合最终提交）
alpha = 0.98; L = N;   // 小规模
alpha = 0.95; L = N/5; // 中规模

// 极速模式（快速验证）
alpha = 0.85; L = std::min(N/50, 5000);
```

## 可视化

使用 `visualize.py` 单独生成图片：

```bash
python visualize.py --data_dir ./data/FPGA-example1/ \
                    --placement ./output/example1_placement.txt \
                    --output ./output/example1.png \
                    --mode both   # scatter / density / both
```

- `--mode scatter`：散点图（不同资源用不同颜色）
- `--mode density`：二维密度热力图
- `--mode both`：叠加显示

## 注意事项

- 输入数据必须符合 ISPD 2016 格式，且路径末尾需带 `/`（脚本会自动处理）。
- 若芯片容量不足，初始放置会输出错误信息并跳过部分元件，请检查 `design.scl` 的大小是否匹配元件总数。
- 对于超大规模电路（>10 万元件），建议将 `calcPartialHPWL` 中的线网大小阈值（`10000`）适当降低，以平衡精度与速度。

