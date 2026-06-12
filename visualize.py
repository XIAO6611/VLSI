import os
import sys
import argparse
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np

plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei'] # 优先使用黑体，其次使用微软雅黑
plt.rcParams['axes.unicode_minus'] = False # 正常显示负号

def load_design_data(data_dir):
    """解析 nodes 获取元件类型，解析 scl 获取芯片边界"""
    inst_type_map = {}
    nodes_file = os.path.join(data_dir, "design.nodes")
    if os.path.exists(nodes_file):
        with open(nodes_file, 'r') as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 2:
                    inst_type_map[parts[0]] = parts[1]
    
    width, height = 0, 0
    scl_file = os.path.join(data_dir, "design.scl")
    if os.path.exists(scl_file):
        with open(scl_file, 'r') as f:
            for line in f:
                if line.startswith("SITEMAP"):
                    parts = line.strip().split()
                    if len(parts) >= 3:
                        width = int(parts[1])
                        height = int(parts[2])
                    break
    return inst_type_map, width, height

def visualize_placement(data_dir, placement_file, output_img, mode='both', density_bins=50):
    """
    mode: 'scatter', 'density', 'both'
    density_bins: 二维直方图的 bin 数量（每个方向）
    """
    print(f"[INFO] 正在渲染可视化: {placement_file} -> {output_img} (mode={mode})")
    inst_type_map, width, height = load_design_data(data_dir)
    
    if width == 0 or height == 0:
        print("[ERROR] 无法从 design.scl 中读取 FPGA 尺寸！")
        return False

    # 收集每个类别的坐标
    cat_coords = {
        "SLICE": ([], []),
        "DSP": ([], []),
        "BRAM": ([], []),
        "IO": ([], [])
    }
    color_map = {"SLICE": "blue", "DSP": "red", "BRAM": "green", "IO": "orange"}

    with open(placement_file, 'r') as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.strip().split()
            if len(parts) >= 4:
                inst_name = parts[0]
                x, y = int(parts[1]), int(parts[2])
                itype = inst_type_map.get(inst_name, "")
                
                if "LUT" in itype or "FDRE" in itype or "CARRY8" in itype:
                    cat = "SLICE"
                elif "DSP" in itype:
                    cat = "DSP"
                elif "RAM" in itype:
                    cat = "BRAM"
                elif "BUF" in itype or "IO" in itype:
                    cat = "IO"
                else:
                    continue
                
                cat_coords[cat][0].append(x)
                cat_coords[cat][1].append(y)

    # 创建图形
    fig, ax = plt.subplots(figsize=(10, 10 * (height / width) if width else 10))
    ax.set_xlim(-5, width + 5)
    ax.set_ylim(-5, height + 5)
    ax.set_title(f"FPGA Placement Visualization\n{os.path.basename(placement_file)}", fontsize=12)
    ax.set_xlabel("X Coordinate")
    ax.set_ylabel("Y Coordinate")
    
    # 绘制芯片边界
    ax.add_patch(patches.Rectangle((0, 0), width, height, fill=False, edgecolor='black', linewidth=2))

    if mode in ('scatter', 'both'):
        # 散点图（SLICE用小点，其他用大点）
        for cat, (xs, ys) in cat_coords.items():
            if not xs:
                continue
            if cat == "SLICE":
                alpha = 0.4
                size = 2
            else:
                alpha = 0.8
                size = 20
            ax.scatter(xs, ys, c=color_map[cat], label=cat, s=size, alpha=alpha, edgecolors='none')

    if mode in ('density', 'both'):
        # 二维直方图（密度图），将所有元件统一视为“密度”
        all_x = []
        all_y = []
        for (xs, ys) in cat_coords.values():
            all_x.extend(xs)
            all_y.extend(ys)
        if all_x:
            # 使用 hexbin 或 hist2d，这里使用 hist2d 更直观
            h = ax.hist2d(all_x, all_y, bins=density_bins, range=[[-0.5, width-0.5], [-0.5, height-0.5]],
                          cmap='plasma', alpha=0.7, cmin=1)
            plt.colorbar(h[3], ax=ax, label='元件数量')
    
    # 图例
    if mode == 'scatter':
        ax.legend(loc='upper right')
    elif mode == 'both':
        # 手动创建一个图例，包含散点类别和密度标识
        from matplotlib.lines import Line2D
        legend_elements = []
        for cat, color in color_map.items():
            if cat_coords[cat][0]:
                legend_elements.append(Line2D([0], [0], marker='o', color='w', label=cat,
                                              markerfacecolor=color, markersize=8))
        legend_elements.append(Line2D([0], [0], color='gray', lw=4, label='高密度区 (颜色越亮越密集)'))
        ax.legend(handles=legend_elements, loc='upper right')

    plt.tight_layout()
    plt.savefig(output_img, dpi=300)
    plt.close()
    print(f"[INFO] 可视化保存成功: {output_img}")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="FPGA Placement Visualizer")
    parser.add_argument("--data_dir", required=True, help="Path to dataset directory")
    parser.add_argument("--placement", required=True, help="Path to placement result file (.txt)")
    parser.add_argument("--output", required=True, help="Path to output image file (.png)")
    parser.add_argument("--mode", default="both", choices=["scatter", "density", "both"],
                        help="Visualization mode: scatter, density, or both (default: both)")
    parser.add_argument("--bins", type=int, default=50, help="Number of bins for density heatmap (default: 50)")
    args = parser.parse_args()
    
    visualize_placement(args.data_dir, args.placement, args.output, args.mode, args.bins)