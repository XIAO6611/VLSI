#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <ctime>
#include <set>      // 【修复1】补回set头文件，解决 reportValid 产生的大量报错
#include <cstdio>   // 【修复2】补齐C标准输入输出，确保 printf 正常工作

#include "Solution.h"
#include "Object.h"

#define ENABLE_VISUALIZATION 
static std::string g_dataset_name = "unknown";
static std::vector<int> g_net_visited_token;
static int g_current_token = 0;

int readBenchMarkFile(std::string dir_path) {
    if (dir_path.back() != '/') dir_path += "/"; // 确保路径以 / 结尾
    std::string line, temp;

    std::cout << "[INFO] Parsing dataset from " << dir_path << std::endl;

    // 1. 读取 design.scl (设置FPGA大小和类型)
    // 在 Solution.cpp 中找到读取 design.scl 的地方，修改为：
    std::ifstream fscl(dir_path + "design.scl");
    if (!fscl.is_open()) return -1;
    bool sitemap_found = false; // 【新增】标记位
    while (std::getline(fscl, line)) {
        if (!sitemap_found && line.find("SITEMAP") != std::string::npos) {
            std::istringstream iss(line);
            std::string dummy;
            int width, height;
            iss >> dummy >> width >> height;
            
            glb_fpga.setSize(width, height);
            glb_fpga.initialize();
            sitemap_found = true; // 标记已找到
        } else if (sitemap_found && std::isdigit(line[0])) { 
            // 只有在找到了 SITEMAP 之后，才开始解析坐标行
            std::istringstream iss(line);
            int x, y; std::string type;
            iss >> x >> y >> type;
            if (x >= 0 && x < glb_fpga.getSizeX() && y >= 0 && y < glb_fpga.getSizeY()) {
                if (glb_fpga.getBlock(x, y)) glb_fpga.getBlock(x, y)->setType(type);
            }
        }
    }
    fscl.close();

    // 用于快速通过字符串查找对象
    std::unordered_map<std::string, Instance*> name_to_inst;

    // 2. 读取 design.nodes (实例化所有元件)
    std::ifstream fnodes(dir_path + "design.nodes");
    if (!fnodes.is_open()) return -1;
    int inst_counter = 0;
    while (std::getline(fnodes, line)) {
        std::istringstream iss(line);
        std::string inst_name, type;
        // 在 readBenchMarkFile 中找到读取 fnodes 的 while 循环，补充如下逻辑：
        if (iss >> inst_name >> type) {
            Instance* inst = new Instance();
            inst->setInstId(inst_counter++);
            inst->setName(inst_name);
            inst->setType(type);
            
            // 【新增】字符串到整型的预处理缓存
            if (type == "LUT6") inst->setFastType(0);
            else if (type.find("LUT") != std::string::npos) inst->setFastType(1);
            else if (type == "FDRE") inst->setFastType(2);
            else if (type == "CARRY8") inst->setFastType(3);
            else if (type.find("DSP") != std::string::npos) inst->setFastType(4);
            else if (type.find("RAM") != std::string::npos) inst->setFastType(5);
            else if (type == "IBUF" || type == "OBUF" || type == "BUFGCE") inst->setFastType(6);
            else inst->setFastType(-1);

            glb_inst_map[inst->getInstId()] = inst;
            name_to_inst[inst_name] = inst;
        }
    }
    fnodes.close();

    // 3. 读取 design.pl (设置固定坐标)
    std::ifstream fpl(dir_path + "design.pl");
    if (!fpl.is_open()) return -1;
    while (std::getline(fpl, line)) {
        std::istringstream iss(line);
        std::string inst_name, is_fixed;
        int x, y, z;
        if (iss >> inst_name >> x >> y >> z >> is_fixed) {
            if (name_to_inst.count(inst_name)) {
                Instance* inst = name_to_inst[inst_name];
                inst->setPosition(x, y, z);
                if (is_fixed == "FIXED") inst->setFixed(true);
                // 放入真实的格子中
                if (glb_fpga.getBlock(x, y)) glb_fpga.getBlock(x, y)->addInst(inst);
            }
        }
    }
    fpl.close();

    // 4. 读取 design.nets (建立线网连接)
    std::ifstream fnets(dir_path + "design.nets");
    if (!fnets.is_open()) return -1;
    Net* current_net = nullptr;
    int net_counter = 0;
    while (std::getline(fnets, line)) {
        if (line.find("net ") == 0) {
            current_net = new Net();
            current_net->setNetId(net_counter++);
            
            // 提取线网的真实名称 (如 net_1031)
            std::istringstream iss(line);
            std::string dummy, net_name;
            iss >> dummy >> net_name; 
            current_net->setName(net_name);
            
            // 【关键修复】必须保存到全局字典中！
            glb_net_map[current_net->getNetId()] = current_net;
            
        } else if (line.find("endnet") == 0) {
            current_net = nullptr;
            
        } else if (current_net != nullptr) {
            std::istringstream iss(line);
            std::string inst_name, pin_name;
            if (iss >> inst_name >> pin_name) {
                if (name_to_inst.count(inst_name)) {
                    Instance* inst = name_to_inst[inst_name];
                    // 【关键修复】精准绑定引脚和线网
                    inst->addPinNet(pin_name, current_net);
                    current_net->addInst(inst);
                }
            }
        }
    }
    fnets.close();

    // 5. 读取 design.wts (赋权重)
    std::ifstream fwts(dir_path + "design.wts");
    if (fwts.is_open()) {
        while (std::getline(fwts, line)) {
            std::istringstream iss(line);
            std::string net_name;
            double weight;
            if (iss >> net_name >> weight) {
                // 遍历寻找对应名字的 Net 并赋值
                for (auto const& [id, net] : glb_net_map) {
                    if (net->getName() == net_name) {
                        net->setWeight(weight);
                        break;
                    }
                }
            }
        }
        fwts.close();
    }

    std::cout << "[INFO] Parse Done. Total Insts: " << glb_inst_map.size() << ", Total Nets: " << glb_net_map.size() << std::endl;
    return 0;
}

int getRandomZ(Instance* inst, std::mt19937& rng) {
    int ft = inst->getFastType();
    if (ft == 0) return 2 * (rng() % 8) + 1; // LUT6
    if (ft == 1) return rng() % 16;         // LUT1-5
    if (ft == 2) return 16 + (rng() % 16);  // FDRE
    if (ft == 3) return 32;                 // CARRY8
    if (ft == 6) return rng() % 64;         // IO
    return 0;
}

bool isLegal(Instance* inst, int rx, int ry, int rz) {
    Block* block = glb_fpga.getBlock(rx, ry);
    if (block == nullptr) return false;
    
    std::string b_type = block->getType(); 
    int ft = inst->getFastType();

    // 1. 大类匹配约束 (直接用整数比较)
    if (ft == 4 && b_type != "DSP") return false;
    if (ft == 5 && b_type != "BRAM") return false;
    if (ft == 6 && b_type != "IO") return false;
    if ((ft >= 0 && ft <= 3) && b_type != "SLICE") return false;

    // 2. 槽位范围约束
    if (b_type == "SLICE") {
        if ((ft == 0 || ft == 1) && (rz < 0 || rz > 15)) return false;
        if (ft == 2 && (rz < 16 || rz > 31)) return false;
        if (ft == 3 && rz != 32) return false;
    } else if (b_type == "IO") {
        if (rz < 0 || rz > 63) return false;
    } else {
        if (rz != 0) return false;
    }

    // 检查当前槽位是否已被占用
    for (Instance* existing : block->getInsts()) {
        if (existing->getZ() == rz && existing != inst) return false;
    }

    // 3. 【ISPD 2016 核心】LUT 变态引脚共享约束
    if (ft == 0 || ft == 1) {
        if (ft == 0) {
            // LUT6 必须是奇数，且 z-1 位置不能有元件
            if (rz % 2 == 0) return false;   
            for (Instance* e : block->getInsts()) {
                if (e->getZ() == rz - 1 && e != inst) return false;
            }
        } else {
            // LUT1-5，检查配对槽位
            int partner_z = (rz % 2 == 0) ? (rz + 1) : (rz - 1);
            Instance* partner = nullptr;
            for (Instance* e : block->getInsts()) {
                if (e->getZ() == partner_z && e != inst) { partner = e; break; }
            }
            if (partner) {
                // 如果隔壁是 LUT6，非法
                if (partner->getFastType() == 0) return false; 
                
                std::set<Net*> shared_inputs;
                for (int p = 0; p <= 5; p++) {
                    if (inst->lut_inputs[p]) shared_inputs.insert(inst->lut_inputs[p]);
                    if (partner->lut_inputs[p]) shared_inputs.insert(partner->lut_inputs[p]);
                }
                if (shared_inputs.size() > 6) return false;
            }
        }
    }

    // 4. 【ISPD 2016 核心】FF 寄存器控制集约束
    if (ft == 2) {
        Net* c_net  = inst->ff_ctrl[0];
        Net* r_net  = inst->ff_ctrl[1];
        Net* ce_net = inst->ff_ctrl[2];

        for (Instance* e : block->getInsts()) {
            if (e->getFastType() == 2 && e != inst) {
                if (e->ff_ctrl[0] != c_net) return false;
                if (e->ff_ctrl[1] != r_net) return false;
                if (e->ff_ctrl[2] != ce_net) return false;
            }
        }
    }
    return true;
}

int reportAndSaveResult(std::string i_file_name, double elapsed_time) {
    int final_hpwl = reportWireLength(); // 计算最终线长
    
    std::fstream f;
    f.open(i_file_name, std::ios::out);
    if (!f.is_open()){
        std::printf("unable to open file %s\n", i_file_name.c_str());
        return -1;
    }
    
    // 【关键】在文件头写入对比数据
    f << "# HPWL: " << final_hpwl << "\n";
    f << "# Runtime: " << elapsed_time << " s\n";
    
    for (size_t i = 0; i < glb_inst_map.size(); i++){
        Instance* lo_inst_p = glb_inst_map[i];
        f << lo_inst_p->getName() << " " 
          << lo_inst_p->getX() << " " 
          << lo_inst_p->getY() << " " 
          << lo_inst_p->getZ() << "\n";
    }
    f.close();
    
    // 终端高亮输出
    std::cout << "========================================" << std::endl;
    std::cout << "[RESULT] Final HPWL : " << final_hpwl << std::endl;
    std::cout << "[RESULT] Runtime    : " << elapsed_time << " s" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

int reportWireLength(){
    int l_wirelength = 0;
    for (auto lo_net : glb_net_map){
        l_wirelength += lo_net.second->evalHPWL();
    }
    std::cout << "Wirelength: " << std::setw(5) << std::right << l_wirelength << std::endl;
    return l_wirelength;
}


// int reportValid(){
//     int l_error_count = 0;
//     for (auto lo_inst : glb_inst_map){
//         Instance* lo_inst_p = lo_inst.second;
//         std::pair<int, int> lo_inst_pos = lo_inst_p->getPosition();
//         Block* lo_block_p = glb_fpga.getBlock(lo_inst_pos.first, lo_inst_pos.second);
//         if (lo_block_p == nullptr){
//             std::printf("[ERROR] inst %d is not placed (%d, %d)\n", lo_inst_p->getInstId(), lo_inst_pos.first, lo_inst_pos.second);
//             l_error_count++;
//             continue;
//         }
//         if (lo_block_p->getInsts()[0] != lo_inst_p){
//             std::printf("[ERROR] inst %d is not in block (%d, %d)\n", lo_inst_p->getInstId(), lo_inst_pos.first, lo_inst_pos.second);
//             l_error_count++;
//         }
//     }
//     std::set<Instance*> lo_inst_attend;
//     for (int i = 0; i < glb_fpga.getSizeX(); i++){
//         for (int j = 0; j < glb_fpga.getSizeY(); j++){
//             Block* lo_block_p = glb_fpga.getBlock(i, j);
//             if (lo_block_p == nullptr) continue;
//             for (auto lo_inst : lo_block_p->getInsts()){
//                 if (lo_inst_attend.find(lo_inst) != lo_inst_attend.end()){
//                     std::printf("[ERROR] inst %d is repeated in block (%d, %d)\n", lo_inst->getInstId(), i, j);
//                     l_error_count++; 
//                 } 
//                 lo_inst_attend.insert(lo_inst);
//             }
//         } 
//     }
//     return l_error_count;
// }


int calcPartialHPWL(Instance* instA, Instance* instB) {
    // 首次调用时初始化数组长度，防止越界
    if (g_net_visited_token.empty()) {
        g_net_visited_token.resize(glb_net_map.size() + 100, 0);
    }
    
    g_current_token++; // 每次计算更新时间戳
    int hpwl = 0;

    auto process_inst = [&](Instance* inst) {
        if (!inst) return;
        for (auto net : inst->getNets()) {
            // 放宽到 10000，既能挡住最夸张的全局时钟线，又能保证普通大线网的精度
            if (net->getInsts().size() > 10000) continue; 

            int net_id = net->getNetId();
            if (g_net_visited_token[net_id] != g_current_token) {
                g_net_visited_token[net_id] = g_current_token;
                hpwl += net->evalHPWL();
            }
        }
    };

    process_inst(instA);
    process_inst(instB);
    
    return hpwl;
}


static std::pair<int,int> calcNetCenter(Instance* inst) {
    double sx = 0, sy = 0;
    int cnt = 0;
    for (Net* net : inst->getNets()) {
        // 【关键】：只让连接少于 20 个元件的线网产生引力！这能提速 100 倍！
        if (net->getInsts().size() > 20) continue; 

        for (Instance* nb : net->getInsts()) {
            if (nb == inst) continue;
            int nx = nb->getX(), ny = nb->getY();
            if (nx < 0 || ny < 0) continue;
            sx += nx; sy += ny; cnt++;
        }
    }
    if (cnt == 0) return {-1, -1};
    return {(int)(sx / cnt), (int)(sy / cnt)};
}


void solvePlacement() {
    std::cout << "[INFO] Running FPGA SA Placement..." << std::endl;
    std::mt19937 rng(42);

    // ── 收集可移动元件 ──────────────────────────────────────────
    std::vector<Instance*> movable_insts;
    for (auto& [id, inst] : glb_inst_map) {
        if (!inst->isFixed()) {
            movable_insts.push_back(inst);
            // 清除旧位置（如果有）
            if (inst->getX() >= 0) {
                Block* b = glb_fpga.getBlock(inst->getX(), inst->getY());
                if (b) b->removeInst(inst);
                inst->setPosition(-1, -1, -1);
            }
        }
    }

   // ── 1. 极速线性填充 (O(N) Deterministic Packing) ────────────────
    // 排序逻辑保持不变（按控制集和连线数排序），它为线性填充提供了完美的聚类基础！
    std::sort(movable_insts.begin(), movable_insts.end(), [](Instance* a, Instance* b){
        int fa = a->getFastType();
        int fb = b->getFastType();
        if (fa != fb) return fa < fb; 
        if (fa == 2) {
            if (a->ff_ctrl[0] != b->ff_ctrl[0]) return a->ff_ctrl[0] < b->ff_ctrl[0];
            if (a->ff_ctrl[1] != b->ff_ctrl[1]) return a->ff_ctrl[1] < b->ff_ctrl[1];
            if (a->ff_ctrl[2] != b->ff_ctrl[2]) return a->ff_ctrl[2] < b->ff_ctrl[2];
        }
        return a->getNets().size() > b->getNets().size();
    });

    std::cout << "[INFO] Generating initial placement (" << movable_insts.size() << " insts)..." << std::endl;
    
    // 【核心黑科技】：记录每一种资源当前扫到的 X 和 Y 坐标（游标）
    std::unordered_map<int, std::pair<int, int>> type_cursors;
    
    for (Instance* inst : movable_insts) {
        int ft = inst->getFastType();
        // 初始化该类型游标
        if (type_cursors.find(ft) == type_cursors.end()) type_cursors[ft] = {0, 0};
        
        int& cx = type_cursors[ft].first;
        int& cy = type_cursors[ft].second;
        bool placed = false;
        
        std::string req_btype = "SLICE";
        int z_start = 0, z_end = 0;
        if (ft == 4) req_btype = "DSP";
        else if (ft == 5) req_btype = "BRAM";
        else if (ft == 6) req_btype = "IO";
        
        if (ft == 0) { z_start=1; z_end=15; }       // LUT6
        else if (ft == 1) { z_start=0; z_end=15; }  // LUT1-5
        else if (ft == 2) { z_start=16; z_end=31; } // FDRE
        else if (ft == 3) { z_start=32; z_end=32; } // CARRY8
        else if (ft == 6) { z_start=0; z_end=63; }  // IO

        // 游标直接从上次结束的格子继续往后扫，绝不回头！时间复杂度绝对 O(N)！
        while (cx < glb_fpga.getSizeX() && !placed) {
            while (cy < glb_fpga.getSizeY() && !placed) {
                Block* blk = glb_fpga.getBlock(cx, cy);
                
                if (blk && blk->getType() == req_btype) {
                    for (int z = z_start; z <= z_end; ++z) {
                        if (ft == 0 && z % 2 == 0) continue; // LUT6必须奇数
                        if (isLegal(inst, cx, cy, z)) {
                            inst->setPosition(cx, cy, z);
                            blk->addInst(inst);
                            placed = true;
                            break; // 塞进去了！游标停留在当前格子，下一个同类元件继续塞！
                        }
                    }
                }
                if (!placed) cy++; // 当前格子塞不下了，Y往前走
            }
            if (!placed) { cy = 0; cx++; } // 这一列扫完了，去下一列
        }
        
        if (!placed) {
            std::cout << "[ERROR] 芯片满了，无法放置元件: " << inst->getName() << std::endl;
        }
    }

    // ── 2. 计算初始线长 ──────────────────────────────────────────
    long long current_hpwl = 0;
    for (auto& [id, net] : glb_net_map) current_hpwl += net->evalHPWL();
    long long best_hpwl = current_hpwl;
    std::cout << "[INFO] Initial HPWL: " << current_hpwl << std::endl;

    // 保存最优解快照
    struct Pos3D { int x, y, z; };
    auto save_best = [&]() {
        std::vector<std::pair<int,Pos3D>> snap;
        snap.reserve(movable_insts.size());
        for (auto inst : movable_insts)
            snap.push_back({inst->getInstId(), {inst->getX(), inst->getY(), inst->getZ()}});
        return snap;
    };
    auto restore_best = [&](const std::vector<std::pair<int,Pos3D>>& snap) {
        for (auto inst : movable_insts) {
            Block* b = glb_fpga.getBlock(inst->getX(), inst->getY());
            if (b) b->removeInst(inst);
        }
        for (auto& [id, pos] : snap) {
            Instance* inst = glb_inst_map[id];
            inst->setPosition(pos.x, pos.y, pos.z);
            glb_fpga.getBlock(pos.x, pos.y)->addInst(inst);
        }
    };
    auto best_snap = save_best();

    // ── 3. SA 参数（根据规模自动调整）───────────────────────────
    int N = (int)movable_insts.size();
    int max_dim = std::max(glb_fpga.getSizeX(), glb_fpga.getSizeY());
    
    double alpha;
    int L;

    // ========================================================
    // [模式选择]：想快速出结果，请注释掉“质量模式”，取消“极速模式”的注释
    // ========================================================

    // // --- 【极速模式：适合快速跑通四个数据集，验证流程】 ---
    // alpha = 0.85; 
    // L = std::min(N / 50, 5000); 

    // --- 【质量模式：适合最终大作业提交，追求极致线长】 ---
    if (N < 10000) { alpha = 0.98; L = N; }
    else if (N < 100000) { alpha = 0.95; L = N / 5; }
    else { alpha = 0.90; L = 50000; }
   
    // ========================================================

    double T_init = 1000.0;
    double T_min  = 0.05;

    // ── 4. 模拟退火主循环 ────────────────────────────────────────
    double T = T_init;
    int stagnant_rounds = 0;         // 连续无改善轮数
    const int RESTART_THRESH = 30; // 15轮无改善则重启

    std::cout << "[INFO] SA started. T_init=" << T_init << " alpha=" << alpha << " L=" << L << std::endl;
    std::uniform_real_distribution<double> dist_prob(0.0, 1.0); 
    while (T > T_min) {
        double ratio = std::log(T / T_min) / std::log(T_init / T_min);
        int window_size = std::max(2, (int)(max_dim * ratio * 0.8));
        bool improved = false;

        // 替换 solvePlacement 中的内层 for 循环
        for (int iter = 0; iter < L; iter++) {
            // 确保变量名统一叫 posA_x, posA_y, posA_z
            int idxA = rng() % movable_insts.size();
            Instance* instA = movable_insts[idxA];
            int posA_x = instA->getX();
            int posA_y = instA->getY();
            int posA_z = instA->getZ();

            int rx, ry, rz;
            Instance* instB = nullptr;

            // 【优化1】大幅降低盲目互换概率（70% -> 15%）
            if (dist_prob(rng) < 0.15) {
                // 15% 同类互换
                int idxB = rng() % movable_insts.size();
                instB = movable_insts[idxB];
                
                std::string tA = instA->getType();
                std::string tB = instB->getType();
                bool a_is_slice = (tA.find("LUT") != std::string::npos || tA == "FDRE" || tA == "CARRY8");
                bool b_is_slice = (tB.find("LUT") != std::string::npos || tB == "FDRE" || tB == "CARRY8");
                
                if (a_is_slice != b_is_slice) continue; 

                rx = instB->getX();
                ry = instB->getY();
                rz = instB->getZ();
            } else {
                // 85% 概率：向心移动 或 局部抖动
                // 【优化2】极大地提高重心引力的触发概率
                if (dist_prob(rng) < 0.7) {
                    // 向着自己连接的其他元件的重心移动
                    auto [cx, cy] = calcNetCenter(instA);
                    if (cx >= 0) {
                        int rad = window_size / 3 + 1; // 进一步收缩抖动范围，增强向心力
                        rx = cx + (rng() % (2 * rad + 1)) - rad;
                        ry = cy + (rng() % (2 * rad + 1)) - rad;
                    } else {
                        rx = posA_x; ry = posA_y;
                    }
                } else {
                    // 普通局部移动
                    int min_x = std::max<int>(0, posA_x - window_size);
                    int max_x = std::min<int>(glb_fpga.getSizeX() - 1, posA_x + window_size);
                    int min_y = std::max<int>(0, posA_y - window_size);
                    int max_y = std::min<int>(glb_fpga.getSizeY() - 1, posA_y + window_size);
                    rx = min_x + rng() % (max_x - min_x + 1);
                    ry = min_y + rng() % (max_y - min_y + 1);
                }
                
                // 确保不出界
                rx = std::max<int>(0, std::min<int>(glb_fpga.getSizeX() - 1, rx));
                ry = std::max<int>(0, std::min<int>(glb_fpga.getSizeY() - 1, ry));
                rz = getRandomZ(instA, rng); 
                
                // 【优化3：绝杀黑科技】主动寻找空槽位
                Block* blockB = glb_fpga.getBlock(rx, ry);
                if (blockB) {
                    bool slot_occupied = false;
                    for (Instance* e : blockB->getInsts()) {
                        if (e->getZ() == rz) { instB = e; slot_occupied = true; break; }
                    }
                    
                    // 如果原本随机选的槽位有人了，不要强行Swap，试着在这个格子里找个空床位睡下
                    if (slot_occupied && dist_prob(rng) < 0.8) {
                        int ft = instA->getFastType();
                        int z_start = 0, z_end = 0;
                        if (ft == 0) { z_start=1; z_end=15; }       // LUT6
                        else if (ft == 1) { z_start=0; z_end=15; }  // LUT1-5
                        else if (ft == 2) { z_start=16; z_end=31; } // FDRE
                        else if (ft == 3) { z_start=32; z_end=32; } // CARRY8
                        else if (ft == 6) { z_start=0; z_end=63; }  // IO
                        
                        // 遍历找空位
                        for (int z_try = z_start; z_try <= z_end; z_try++) {
                            if (ft == 0 && z_try % 2 == 0) continue; // LUT6 必须奇数
                            bool is_empty = true;
                            for (Instance* e : blockB->getInsts()) {
                                if (e->getZ() == z_try) { is_empty = false; break; }
                            }
                            if (is_empty) {
                                rz = z_try;         // 找到空位了！
                                instB = nullptr;    // 目标设为空，变成纯粹的移动
                                break;
                            }
                        }
                    }
                }
            }

            if (rx == posA_x && ry == posA_y && rz == posA_z) continue;
            if (instB && instB->isFixed()) continue; 

            Block* ba = glb_fpga.getBlock(posA_x, posA_y);
            Block* bb = glb_fpga.getBlock(rx, ry);

            // 第一步：先将它们从当前的 Block 中“假装拔出”，防止 isLegal 误判槽位被占
            if (ba) ba->removeInst(instA);
            if (bb && instB) bb->removeInst(instB);

            // 第二步：检查交换后的目标位置是否合法
            bool legalA = isLegal(instA, rx, ry, rz);
            bool legalB = instB ? isLegal(instB, posA_x, posA_y, posA_z) : true;

            if (!legalA || !legalB) {
                // 如果不合法，立刻插回原位，放弃这次移动
                if (ba) ba->addInst(instA);
                if (bb && instB) bb->addInst(instB);
                continue; 
            }

            // 第三步：位置合法，计算移动前的线长
            int before = calcPartialHPWL(instA, instB);
            
            // 执行坐标修改
            instA->setPosition(rx, ry, rz);
            if (instB) instB->setPosition(posA_x, posA_y, posA_z);
            
            // 计算移动后的线长及增量
            int after = calcPartialHPWL(instA, instB);
            int delta = after - before;

            // 第四步：SA 接受/拒绝准则
            if (delta <= 0 || dist_prob(rng) < std::exp(-delta / T)) {
                // 接受：将元件插入新的 Block 中
                if (bb) bb->addInst(instA);
                if (ba && instB) ba->addInst(instB);
                
                current_hpwl += delta;
                if (current_hpwl < best_hpwl) {
                    best_hpwl = current_hpwl;
                    best_snap = save_best();
                    improved = true;
                }
            } else {
                // 拒绝：坐标回滚
                instA->setPosition(posA_x, posA_y, posA_z);
                if (instB) instB->setPosition(rx, ry, rz);
                
                // 将元件重新插回原来的 Block 中
                if (ba) ba->addInst(instA);
                if (bb && instB) bb->addInst(instB);
            }
        }
        T *= alpha;
        std::cout << "[SA Progress] T=" << T << " Best HPWL=" << best_hpwl << std::endl;
    }

    // ── 5. 回滚最优解 ────────────────────────────────────────────
    restore_best(best_snap);
    std::cout << "[INFO] SA Done. Best HPWL: " << best_hpwl << std::endl;
}
