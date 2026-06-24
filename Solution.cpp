#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <ctime>
#include <set>
#include <cstdio>

#include "Solution.h"
#include "Object.h"

#define ENABLE_VISUALIZATION 
static std::string g_dataset_name = "unknown";
static std::vector<int> g_net_visited_token;
static int g_current_token = 0;

int readBenchMarkFile(std::string dir_path) {
    if (dir_path.back() != '/') dir_path += "/";
    std::string line, temp;

    std::cout << "[INFO] Parsing dataset from " << dir_path << std::endl;

    std::ifstream fscl(dir_path + "design.scl");
    if (!fscl.is_open()) return -1;
    bool sitemap_found = false;
    while (std::getline(fscl, line)) {
        if (!sitemap_found && line.find("SITEMAP") != std::string::npos) {
            std::istringstream iss(line);
            std::string dummy;
            int width, height;
            iss >> dummy >> width >> height;
            glb_fpga.setSize(width, height);
            glb_fpga.initialize();
            sitemap_found = true;
        } else if (sitemap_found && std::isdigit(line[0])) {
            std::istringstream iss(line);
            int x, y; std::string type;
            iss >> x >> y >> type;
            if (x >= 0 && x < glb_fpga.getSizeX() && y >= 0 && y < glb_fpga.getSizeY()) {
                if (glb_fpga.getBlock(x, y)) glb_fpga.getBlock(x, y)->setType(type);
            }
        }
    }
    fscl.close();

    std::unordered_map<std::string, Instance*> name_to_inst;

    std::ifstream fnodes(dir_path + "design.nodes");
    if (!fnodes.is_open()) return -1;
    int inst_counter = 0;
    while (std::getline(fnodes, line)) {
        std::istringstream iss(line);
        std::string inst_name, type;
        if (iss >> inst_name >> type) {
            Instance* inst = new Instance();
            inst->setInstId(inst_counter++);
            inst->setName(inst_name);
            inst->setType(type);
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
                if (glb_fpga.getBlock(x, y)) glb_fpga.getBlock(x, y)->addInst(inst);
            }
        }
    }
    fpl.close();

    std::unordered_map<std::string, Net*> name_to_net;
    std::ifstream fnets(dir_path + "design.nets");
    if (!fnets.is_open()) return -1;
    Net* current_net = nullptr;
    int net_counter = 0;
    while (std::getline(fnets, line)) {
        if (line.find("net ") == 0) {
            current_net = new Net();
            current_net->setNetId(net_counter++);
            std::istringstream iss(line);
            std::string dummy, net_name;
            iss >> dummy >> net_name;
            current_net->setName(net_name);
            glb_net_map[current_net->getNetId()] = current_net;
            name_to_net[net_name] = current_net;
        } else if (line.find("endnet") == 0) {
            current_net = nullptr;
        } else if (current_net != nullptr) {
            std::istringstream iss(line);
            std::string inst_name, pin_name;
            if (iss >> inst_name >> pin_name) {
                if (name_to_inst.count(inst_name)) {
                    Instance* inst = name_to_inst[inst_name];
                    inst->addPinNet(pin_name, current_net);
                    current_net->addInst(inst);
                }
            }
        }
    }
    fnets.close();

    std::ifstream fwts(dir_path + "design.wts");
    if (fwts.is_open()) {
        while (std::getline(fwts, line)) {
            std::istringstream iss(line);
            std::string net_name; double weight;
            if (iss >> net_name >> weight) {
                if (name_to_net.count(net_name)) {
                    name_to_net[net_name]->setWeight(weight);
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
    if (ft == 0) return 2 * (rng() % 8) + 1;
    if (ft == 1) return rng() % 16;
    if (ft == 2) return 16 + (rng() % 16);
    if (ft == 3) return 32;
    if (ft == 6) return rng() % 64;
    return 0;
}

bool isLegal(Instance* inst, int rx, int ry, int rz) {
    Block* block = glb_fpga.getBlock(rx, ry);
    if (block == nullptr) return false;
    std::string b_type = block->getType();
    int ft = inst->getFastType();

    if (ft == 4 && b_type != "DSP") return false;
    if (ft == 5 && b_type != "BRAM") return false;
    if (ft == 6 && b_type != "IO") return false;
    if ((ft >= 0 && ft <= 3) && b_type != "SLICE") return false;

    if (b_type == "SLICE") {
        if ((ft == 0 || ft == 1) && (rz < 0 || rz > 15)) return false;
        if (ft == 2 && (rz < 16 || rz > 31)) return false;
        if (ft == 3 && rz != 32) return false;
    } else if (b_type == "IO") {
        if (rz < 0 || rz > 63) return false;
    } else {
        if (rz != 0) return false;
    }

    for (Instance* existing : block->getInsts()) {
        if (existing->getZ() == rz && existing != inst) return false;
    }

    if (ft == 0 || ft == 1) {
        if (ft == 0) {
            if (rz % 2 == 0) return false;
            for (Instance* e : block->getInsts()) {
                if (e->getZ() == rz - 1 && e != inst) return false;
            }
        } else {
            int partner_z = (rz % 2 == 0) ? (rz + 1) : (rz - 1);
            Instance* partner = nullptr;
            for (Instance* e : block->getInsts()) {
                if (e->getZ() == partner_z && e != inst) { partner = e; break; }
            }
            if (partner) {
                if (partner->getFastType() == 0) return false;
                Net* shared_inputs[12];
                int shared_cnt = 0;
                for (int p = 0; p <= 5; p++) {
                    if (Net* n = inst->lut_inputs[p]) {
                        bool found = false;
                        for (int k = 0; k < shared_cnt; k++) if (shared_inputs[k] == n) { found = true; break; }
                        if (!found) shared_inputs[shared_cnt++] = n;
                    }
                    if (Net* n = partner->lut_inputs[p]) {
                        bool found = false;
                        for (int k = 0; k < shared_cnt; k++) if (shared_inputs[k] == n) { found = true; break; }
                        if (!found) shared_inputs[shared_cnt++] = n;
                    }
                }
                if (shared_cnt > 6) return false;
            }
        }
    }

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
    int final_hpwl = reportWireLength();
    std::fstream f;
    f.open(i_file_name, std::ios::out);
    if (!f.is_open()){
        std::printf("unable to open file %s\n", i_file_name.c_str());
        return -1;
    }
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

static std::pair<int,int> calcNetCenter(Instance* inst) {
    double sx = 0, sy = 0;
    int cnt = 0;
    for (Net* net : inst->getNets()) {
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

struct Slot {
    int x, y, z;
    Block* block;
    bool used;
};


void solvePlacement() {
    std::cout << "[INFO] Running Optimized Placement Engine (Adaptive Parameters + Hill Climbing)..." << std::endl;
    std::mt19937 rng(42);

    // ── 0. 数据预处理 ──
    std::vector<Instance*> movable_insts;
    int max_inst_id = 0, max_net_id = 0;
    for (auto& [id, inst] : glb_inst_map) {
        max_inst_id = std::max(max_inst_id, inst->getInstId());
        if (!inst->isFixed()) {
            movable_insts.push_back(inst);
            if (inst->getX() >= 0) {
                Block* b = glb_fpga.getBlock(inst->getX(), inst->getY());
                if (b) b->removeInst(inst);
                inst->setPosition(-1, -1, -1);
            }
        }
    }
    for (auto& [id, net] : glb_net_map) max_net_id = std::max(max_net_id, net->getNetId());
    int N = movable_insts.size();
    if (g_net_visited_token.empty()) g_net_visited_token.assign(max_net_id + 100, 0);

    // 根据电路规模自适应参数 (吸收深度优化建议)
    bool is_small = (N < 10000);
    int analytical_iters = is_small ? 200 : 300;     // 【优化】大幅增加力导向迭代次数，让元件充分散开
    double lr = is_small ? 1.0 : 1.5;                // 【优化】初始学习率微调
    int grid_dim = is_small ? 16 : 32;
    double density_weight_max = is_small ? 1.2 : 1.5;// 【优化】斥力权重从 0.3 提升到 1.2，强制拉开密集区
    int BUCKET_SIZE = is_small ? 16 : 32;
    int window = std::max(500, N / 50);              // 【优化】动态扩大合法化搜索窗口 (从 200 扩大到 500+)
    double T0 = is_small ? 50.0 : 200.0;             // 【优化】提高初始退火温度，增加逃离局部最优的能力
    int num_steps = is_small ? 20 : 35;
    // int L = is_small ? std::min(N * 50, 500000) : std::min(N * 15, 1000000); // 【优化】单步退火迭代次数暴增
    int L = is_small ? N * 40 : N * 15;



    // =========================================================================
    // ── 1. 网格化电场解析布局 ──
    // =========================================================================
    std::cout << "[INFO] Phase 1: Analytical Placement (grid density)..." << std::endl;
    std::vector<std::pair<double, double>> cont_pos(max_inst_id + 1, {0.0, 0.0});

    double f_sx = glb_fpga.getSizeX(), f_sy = glb_fpga.getSizeY();
    for (Instance* inst : movable_insts) {
        cont_pos[inst->getInstId()] = {
            f_sx * 0.1 + (double)(rng() % (int)(f_sx * 0.8)),
            f_sy * 0.1 + (double)(rng() % (int)(f_sy * 0.8))
        };
    }

    std::vector<std::pair<double, double>> net_centers(max_net_id + 1, {0.0, 0.0});
    std::vector<int> net_pin_cnt(max_net_id + 1, 0);
    double bin_w = f_sx / grid_dim, bin_h = f_sy / grid_dim;
    std::vector<std::vector<double>> density_map(grid_dim, std::vector<double>(grid_dim, 0.0));

    for (int iter = 0; iter < analytical_iters; iter++) {
        std::fill(net_centers.begin(), net_centers.end(), std::pair<double, double>{0.0, 0.0});
        std::fill(net_pin_cnt.begin(), net_pin_cnt.end(), 0);
        for (int i = 0; i < grid_dim; i++) std::fill(density_map[i].begin(), density_map[i].end(), 0.0);

        for (auto& [id, net] : glb_net_map) {
            if (net->getInsts().size() > 200) continue;
            double cx = 0, cy = 0; int cnt = 0;
            for (Instance* nb : net->getInsts()) {
                if (nb->isFixed()) { cx += nb->getX(); cy += nb->getY(); }
                else { cx += cont_pos[nb->getInstId()].first; cy += cont_pos[nb->getInstId()].second; }
                cnt++;
            }
            if (cnt > 0) { net_centers[id] = {cx / cnt, cy / cnt}; net_pin_cnt[id] = cnt; }
        }

        for (Instance* inst : movable_insts) {
            int id = inst->getInstId();
            int gx = std::max(0, std::min(grid_dim - 1, (int)(cont_pos[id].first / bin_w)));
            int gy = std::max(0, std::min(grid_dim - 1, (int)(cont_pos[id].second / bin_h)));
            density_map[gx][gy] += 1.0;
        }

        double density_weight = density_weight_max * (iter / (double)analytical_iters);
        for (Instance* inst : movable_insts) {
            int i_id = inst->getInstId();
            double fx = 0, fy = 0; int net_cnt = 0;
            for (Net* net : inst->getNets()) {
                int n_id = net->getNetId();
                if (net_pin_cnt[n_id] <= 1) continue;
                fx += (net_centers[n_id].first - cont_pos[i_id].first);
                fy += (net_centers[n_id].second - cont_pos[i_id].second);
                net_cnt++;
            }
            if (net_cnt > 0) { fx /= net_cnt; fy /= net_cnt; }

            int gx = std::max(0, std::min(grid_dim - 1, (int)(cont_pos[i_id].first / bin_w)));
            int gy = std::max(0, std::min(grid_dim - 1, (int)(cont_pos[i_id].second / bin_h)));
            double dx_force = 0, dy_force = 0;
            if (gx > 0) dx_force += (density_map[gx-1][gy] - density_map[gx][gy]);
            if (gx < grid_dim - 1) dx_force -= (density_map[gx+1][gy] - density_map[gx][gy]);
            if (gy > 0) dy_force += (density_map[gx][gy-1] - density_map[gx][gy]);
            if (gy < grid_dim - 1) dy_force -= (density_map[gx][gy+1] - density_map[gx][gy]);

            fx += density_weight * dx_force;
            fy += density_weight * dy_force;

            cont_pos[i_id].first += lr * fx;
            cont_pos[i_id].second += lr * fy;
            cont_pos[i_id].first = std::max(0.0, std::min(f_sx - 1.1, cont_pos[i_id].first));
            cont_pos[i_id].second = std::max(0.0, std::min(f_sy - 1.1, cont_pos[i_id].second));
        }
        lr *= 0.95;
    }

    // FF 控制集聚合
    std::unordered_map<std::string, std::pair<double, double>> ctrl_pos_sum;
    std::unordered_map<std::string, int> ctrl_pos_cnt;
    for (Instance* inst : movable_insts) {
        if (inst->getFastType() == 2) {
            std::string key = std::to_string((size_t)inst->ff_ctrl[0]) + "_" + std::to_string((size_t)inst->ff_ctrl[1]) + "_" + std::to_string((size_t)inst->ff_ctrl[2]);
            ctrl_pos_sum[key].first += cont_pos[inst->getInstId()].first;
            ctrl_pos_sum[key].second += cont_pos[inst->getInstId()].second;
            ctrl_pos_cnt[key]++;
        }
    }
    for (Instance* inst : movable_insts) {
        if (inst->getFastType() == 2) {
            std::string key = std::to_string((size_t)inst->ff_ctrl[0]) + "_" + std::to_string((size_t)inst->ff_ctrl[1]) + "_" + std::to_string((size_t)inst->ff_ctrl[2]);
            cont_pos[inst->getInstId()].first = ctrl_pos_sum[key].first / ctrl_pos_cnt[key];
            cont_pos[inst->getInstId()].second = ctrl_pos_sum[key].second / ctrl_pos_cnt[key];
        }
    }

    // =========================================================================
    // ── 2. 空间哈希分桶合法化 (改进版：二维距离优先) ──
    // =========================================================================
    std::cout << "[INFO] Phase 2: Spatial Hashing Legalization (2D nearest)..." << std::endl;

    int bx = (glb_fpga.getSizeX() + BUCKET_SIZE - 1) / BUCKET_SIZE;
    int by = (glb_fpga.getSizeY() + BUCKET_SIZE - 1) / BUCKET_SIZE;

    // 强制 FF 组分配
    std::unordered_map<std::string, std::vector<Instance*>> ff_groups;
    for (Instance* inst : movable_insts) {
        if (inst->getFastType() == 2) {
            std::string key = std::to_string((size_t)inst->ff_ctrl[0]) + "_" +
                              std::to_string((size_t)inst->ff_ctrl[1]) + "_" +
                              std::to_string((size_t)inst->ff_ctrl[2]);
            ff_groups[key].push_back(inst);
        }
    }
    std::unordered_set<Instance*> placed_ff;
    for (auto& [key, group] : ff_groups) {
        int need = group.size();
        Block* target = nullptr;
        int target_x = -1, target_y = -1;
        for (int x = 0; x < glb_fpga.getSizeX() && !target; ++x) {
            for (int y = 0; y < glb_fpga.getSizeY() && !target; ++y) {
                Block* blk = glb_fpga.getBlock(x, y);
                if (blk->getType() != "SLICE") continue;
                int free_ff = 0;
                for (int z = 16; z <= 31; ++z) if (blk->isZFree(z)) free_ff++;
                if (free_ff >= need) {
                    target = blk;
                    target_x = x; target_y = y;
                }
            }
        }
        if (target) {
            int idx = 0;
            for (int z = 16; z <= 31 && idx < need; ++z) {
                if (target->isZFree(z)) {
                    Instance* inst = group[idx++];
                    inst->setPosition(target_x, target_y, z);
                    target->addInst(inst);
                    placed_ff.insert(inst);
                }
            }
        }
    }

    // 桶数据结构
    std::vector<std::vector<std::vector<std::vector<Slot>>>> bucket_slots(7,
        std::vector<std::vector<std::vector<Slot>>>(bx,
            std::vector<std::vector<Slot>>(by, std::vector<Slot>())));
    std::vector<std::vector<std::vector<std::vector<Instance*>>>> bucket_insts(7,
        std::vector<std::vector<std::vector<Instance*>>>(bx,
            std::vector<std::vector<Instance*>>(by, std::vector<Instance*>())));

    // 槽位分桶
    for (int x = 0; x < glb_fpga.getSizeX(); ++x) {
        for (int y = 0; y < glb_fpga.getSizeY(); ++y) {
            Block* blk = glb_fpga.getBlock(x, y);
            if (!blk) continue;
            int gx = x / BUCKET_SIZE, gy = y / BUCKET_SIZE;
            std::string btype = blk->getType();
            if (btype == "SLICE") {
                for (int z = 1; z <= 15; z += 2) bucket_slots[0][gx][gy].push_back({x, y, z, blk, false});
                for (int z = 0; z <= 15; z++)   bucket_slots[1][gx][gy].push_back({x, y, z, blk, false});
                for (int z = 16; z <= 31; z++)  bucket_slots[2][gx][gy].push_back({x, y, z, blk, false});
                bucket_slots[3][gx][gy].push_back({x, y, 32, blk, false});
            } else if (btype == "DSP") {
                bucket_slots[4][gx][gy].push_back({x, y, 0, blk, false});
            } else if (btype == "BRAM") {
                bucket_slots[5][gx][gy].push_back({x, y, 0, blk, false});
            } else if (btype == "IO") {
                for (int z = 0; z <= 63; z++) bucket_slots[6][gx][gy].push_back({x, y, z, blk, false});
            }
        }
    }

    // 元件分桶
    for (Instance* inst : movable_insts) {
        int ft = inst->getFastType();
        if (ft < 0 || ft > 6) continue;
        if (placed_ff.count(inst)) continue;
        double cx = cont_pos[inst->getInstId()].first;
        double cy = cont_pos[inst->getInstId()].second;
        int gx = std::max(0, std::min(bx - 1, (int)(cx / BUCKET_SIZE)));
        int gy = std::max(0, std::min(by - 1, (int)(cy / BUCKET_SIZE)));
        bucket_insts[ft][gx][gy].push_back(inst);
    }

    // 桶内匹配：二维距离优先
    int placed_count = 0;
    std::vector<Instance*> overflow_insts;

    for (int ft = 0; ft < 7; ++ft) {
        for (int gx = 0; gx < bx; ++gx) {
            for (int gy = 0; gy < by; ++gy) {
                auto& insts = bucket_insts[ft][gx][gy];
                auto& slots = bucket_slots[ft][gx][gy];
                if (insts.empty() || slots.empty()) continue;

                // 对槽位按 x 排序（用于二分），但对每个元件单独计算距离
                std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.x < b.x; });
                for (Instance* inst : insts) {
                    double cx = cont_pos[inst->getInstId()].first;
                    double cy = cont_pos[inst->getInstId()].second;
                    // 二分查找近似位置
                    auto it = std::lower_bound(slots.begin(), slots.end(), cx,
                        [](const Slot& s, double val) { return s.x < val; });
                    int base = it - slots.begin();
                    int start = std::max(0, base - window);
                    int end_idx = std::min((int)slots.size() - 1, base + window);

                    // 收集候选距离并排序
                    std::vector<std::pair<int, int>> candidates;
                    for (int i = start; i <= end_idx; ++i) {
                        if (slots[i].used) continue;
                        int dist = std::abs(slots[i].x - (int)cx) + std::abs(slots[i].y - (int)cy);
                        candidates.push_back({dist, i});
                    }
                    std::sort(candidates.begin(), candidates.end());

                    Slot* best_slot = nullptr;
                    for (auto& [d, idx] : candidates) {
                        Slot& s = slots[idx];
                        if (!s.block->isZFree(s.z)) continue;
                        if (isLegal(inst, s.x, s.y, s.z)) {
                            best_slot = &s;
                            break;
                        }
                    }
                    if (best_slot) {
                        inst->setPosition(best_slot->x, best_slot->y, best_slot->z);
                        best_slot->block->addInst(inst);
                        best_slot->used = true;
                        placed_count++;
                    } else {
                        overflow_insts.push_back(inst);
                    }
                }
            }
        }
    }
std::cout << "[DEBUG] Bucket matched: " << placed_count << ", overflow: " << overflow_insts.size() << std::endl;

    // =========================================================================
    // 【优化重构部分】：高效中心圈扩散搜索（替换原来的全图盲目 O(X*Y) 扫描）
    // =========================================================================
    
    // 1. 溢出处理（跨桶）
    for (Instance* inst : overflow_insts) {
        int ft = inst->getFastType();
        double cx = cont_pos[inst->getInstId()].first;
        double cy = cont_pos[inst->getInstId()].second;
        int gx0 = std::max(0, std::min(bx - 1, (int)(cx / BUCKET_SIZE)));
        int gy0 = std::max(0, std::min(by - 1, (int)(cy / BUCKET_SIZE)));
        bool placed = false;

        // 优先在邻近的空间哈希桶内进行搜索
        for (int r = 1; r <= 8 && !placed; ++r) {
            for (int dx = -r; dx <= r && !placed; ++dx) {
                for (int dy = -r; dy <= r && !placed; ++dy) {
                    int gx = gx0 + dx, gy = gy0 + dy;
                    if (gx < 0 || gx >= bx || gy < 0 || gy >= by) continue;
                    for (Slot& s : bucket_slots[ft][gx][gy]) {
                        if (!s.used && s.block->isZFree(s.z) && isLegal(inst, s.x, s.y, s.z)) {
                            inst->setPosition(s.x, s.y, s.z);
                            s.block->addInst(inst);
                            s.used = true;
                            placed = true;
                            placed_count++;
                            break;
                        }
                    }
                }
            }
        }

        // 邻近桶未找到，启动高效的正方形壳层向外圈层扩散搜索（终极 Fallback）
        if (!placed) {
            int start_x = std::max(0, std::min(glb_fpga.getSizeX() - 1, (int)cx));
            int start_y = std::max(0, std::min(glb_fpga.getSizeY() - 1, (int)cy));
            int max_r = std::max(glb_fpga.getSizeX(), glb_fpga.getSizeY());

            int z_start = 0, z_end = 0;
            if (ft == 0) { z_start = 1; z_end = 15; }
            else if (ft == 1) { z_start = 0; z_end = 15; }
            else if (ft == 2) { z_start = 16; z_end = 31; }
            else if (ft == 3) { z_start = 32; z_end = 32; }
            else if (ft == 6) { z_start = 0; z_end = 63; }
            else { z_start = 0; z_end = 0; }

            for (int r = 0; r <= max_r && !placed; ++r) {
                if (r == 0) {
                    Block* blk = glb_fpga.getBlock(start_x, start_y);
                    if (blk) {
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, start_x, start_y, z)) {
                                inst->setPosition(start_x, start_y, z);
                                blk->addInst(inst);
                                placed = true;
                                break;
                            }
                        }
                    }
                    continue;
                }
                
                // 扩散搜索：扫描当前半径 r 的正方形的上下边界边界
                for (int dx = -r; dx <= r && !placed; ++dx) {
                    int x = start_x + dx;
                    if (x < 0 || x >= glb_fpga.getSizeX()) continue;
                    for (int sign : {-1, 1}) {
                        int y = start_y + sign * r;
                        if (y < 0 || y >= glb_fpga.getSizeY()) continue;
                        Block* blk = glb_fpga.getBlock(x, y);
                        if (!blk) continue;
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, x, y, z)) {
                                inst->setPosition(x, y, z);
                                blk->addInst(inst);
                                placed = true;
                                break;
                            }
                        }
                        if (placed) break;
                    }
                }
                
                // 扩散搜索：扫描当前半径 r 的正方形的左右边界
                for (int dy = -r + 1; dy <= r - 1 && !placed; ++dy) {
                    int y = start_y + dy;
                    if (y < 0 || y >= glb_fpga.getSizeY()) continue;
                    for (int sign : {-1, 1}) {
                        int x = start_x + sign * r;
                        if (x < 0 || x >= glb_fpga.getSizeX()) continue;
                        Block* blk = glb_fpga.getBlock(x, y);
                        if (!blk) continue;
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, x, y, z)) {
                                inst->setPosition(x, y, z);
                                blk->addInst(inst);
                                placed = true;
                                break;
                            }
                        }
                        if (placed) break;
                    }
                }
            }
        }
        if (!placed) std::cerr << "[ERROR] Failed to place " << inst->getName() << std::endl;
    }

    // 2. 最终强制修复（确保所有依然缺失坐标的元件获得兜底合法位置）
    int still_unplaced = 0;
    for (Instance* inst : movable_insts) {
        if (inst->getX() == -1) {
            still_unplaced++;
            int ft = inst->getFastType();
            double cx = cont_pos[inst->getInstId()].first;
            double cy = cont_pos[inst->getInstId()].second;
            int start_x = std::max(0, std::min(glb_fpga.getSizeX() - 1, (int)cx));
            int start_y = std::max(0, std::min(glb_fpga.getSizeY() - 1, (int)cy));
            int max_r = std::max(glb_fpga.getSizeX(), glb_fpga.getSizeY());

            int z_start = 0, z_end = 0;
            if (ft == 0) { z_start = 1; z_end = 15; }
            else if (ft == 1) { z_start = 0; z_end = 15; }
            else if (ft == 2) { z_start = 16; z_end = 31; }
            else if (ft == 3) { z_start = 32; z_end = 32; }
            else if (ft == 6) { z_start = 0; z_end = 63; }
            else { z_start = 0; z_end = 0; }

            bool fixed_placed = false;
            for (int r = 0; r <= max_r && !fixed_placed; ++r) {
                if (r == 0) {
                    Block* blk = glb_fpga.getBlock(start_x, start_y);
                    if (blk) {
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, start_x, start_y, z)) {
                                inst->setPosition(start_x, start_y, z);
                                blk->addInst(inst);
                                placed_count++;
                                fixed_placed = true;
                                break;
                            }
                        }
                    }
                    continue;
                }
                // 扫描上下边
                for (int dx = -r; dx <= r && !fixed_placed; ++dx) {
                    int x = start_x + dx;
                    if (x < 0 || x >= glb_fpga.getSizeX()) continue;
                    for (int sign : {-1, 1}) {
                        int y = start_y + sign * r;
                        if (y < 0 || y >= glb_fpga.getSizeY()) continue;
                        Block* blk = glb_fpga.getBlock(x, y);
                        if (!blk) continue;
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, x, y, z)) {
                                inst->setPosition(x, y, z);
                                blk->addInst(inst);
                                placed_count++;
                                fixed_placed = true;
                                break;
                            }
                        }
                        if (fixed_placed) break;
                    }
                }
                // 扫描左右边
                for (int dy = -r + 1; dy <= r - 1 && !fixed_placed; ++dy) {
                    int y = start_y + dy;
                    if (y < 0 || y >= glb_fpga.getSizeY()) continue;
                    for (int sign : {-1, 1}) {
                        int x = start_x + sign * r;
                        if (x < 0 || x >= glb_fpga.getSizeX()) continue;
                        Block* blk = glb_fpga.getBlock(x, y);
                        if (!blk) continue;
                        for (int z = z_start; z <= z_end; ++z) {
                            if (ft == 0 && z % 2 == 0) continue;
                            if (blk->isZFree(z) && isLegal(inst, x, y, z)) {
                                inst->setPosition(x, y, z);
                                blk->addInst(inst);
                                placed_count++;
                                fixed_placed = true;
                                break;
                            }
                        }
                        if (fixed_placed) break;
                    }
                }
            }
        }
    }
    if (still_unplaced > 0) std::cout << "[INFO] Fixed " << still_unplaced << " unplaced instances in final fallback." << std::endl;

    long long current_hpwl = 0;
    for (auto& [id, net] : glb_net_map) { net->updateCache(); current_hpwl += net->cached_hpwl; }
    std::cout << "[INFO] Legalization Done. Placed: " << placed_count << ", HPWL: " << current_hpwl << std::endl;




    // =========================================================================
    // ── 3. 模拟退火 (空槽跳跃 + 自适应小电路参数) ──
    // =========================================================================
    std::cout << "[INFO] Phase 3: Simulated Annealing (empty‑slot jump)..." << std::endl;

    double T = T0;
    std::uniform_real_distribution<double> dist_prob(0.0, 1.0);
    for (int step = 0; step < num_steps; step++) {
        int rad = std::max(1, (is_small ? 6 : 20) - step/2);
        // 完整替换 Phase 3 的内层 for 循环，解决 ft 未定义报错
        for (int iter = 0; iter < L; iter++) {
            Instance* instA = movable_insts[rng() % N];
            int old_x = instA->getX(), old_y = instA->getY(), old_z = instA->getZ();
            if (old_x < 0 || old_y < 0) continue;   // 防御

            // 【修复点 1】：将 ft 的获取提到最上面，让后面的智能策略可以合法使用
            int ft = instA->getFastType();

            // ================= 智能探索策略 =================
            int rx, ry;
            double move_prob = dist_prob(rng);






            if (move_prob < 0.7) {
            // if (move_prob < 0.2) {
                // 策略 1 (20%概率)：重心引力。向相连的元件聚拢，极大加速线长收敛
                auto [cx, cy] = calcNetCenter(instA);
                if (cx >= 0) {
                    rx = std::max(0, std::min(glb_fpga.getSizeX() - 1, cx + (int)(rng() % 3) - 1));
                    ry = std::max(0, std::min(glb_fpga.getSizeY() - 1, cy + (int)(rng() % 3) - 1));
                } else {
                    rx = std::max(0, std::min(glb_fpga.getSizeX() - 1, old_x + (int)(rng() % (2*rad+1)) - rad));
                    ry = std::max(0, std::min(glb_fpga.getSizeY() - 1, old_y + (int)(rng() % (2*rad+1)) - rad));
                }
            } 
            else if (move_prob < 0.95) {
            // else if (move_prob < 0.6) {
                // 策略 2 (40%概率)：局部随机。在当前温度半径内探索，避免局部死锁
                rx = std::max(0, std::min(glb_fpga.getSizeX() - 1, old_x + (int)(rng() % (2*rad+1)) - rad));
                ry = std::max(0, std::min(glb_fpga.getSizeY() - 1, old_y + (int)(rng() % (2*rad+1)) - rad));
            } 
            else {
                // 策略 3 (40%概率)：全局同类交换。直接找一个同类型元件尝试互换位置，打破宏观拥堵
                int target_idx = rng() % N;
                Instance* target = movable_insts[target_idx];
                if (target->getFastType() == ft) {
                    rx = target->getX();
                    ry = target->getY();
                } else {
                    rx = std::max(0, std::min(glb_fpga.getSizeX() - 1, old_x + (int)(rng() % (2*rad+1)) - rad));
                    ry = std::max(0, std::min(glb_fpga.getSizeY() - 1, old_y + (int)(rng() % (2*rad+1)) - rad));
                }
            }
            // ===============================================









            Block* blockB = glb_fpga.getBlock(rx, ry);
            if (!blockB) continue;

            // 【修复点 2】：原先这里的 "int ft = ..." 已删掉类型修饰，直接复用上方变量
            int z_start = 0, z_end = 0;
            if (ft == 0) { z_start=1; z_end=15; } else if (ft == 1) { z_start=0; z_end=15; }
            else if (ft == 2) { z_start=16; z_end=31; } else if (ft == 3) { z_start=32; z_end=32; }
            else if (ft == 6) { z_start=0; z_end=63; } else continue;

            // 收集候选 Z (空槽优先，且以一定概率考虑交换)
            int valid_zs[64], valid_cnt = 0;
            for (int z = z_start; z <= z_end; z++) {
                if (ft == 0 && z % 2 == 0) continue;
                if (blockB->isZFree(z)) {
                    valid_zs[valid_cnt++] = z;
                } else {
                    // 50% 的概率尝试交换
                    if (rng() % 2 == 0) {
                        for (Instance* e : blockB->getInsts()) {
                            if (e->getZ() == z && !e->isFixed() && e->getFastType() == ft) {
                                valid_zs[valid_cnt++] = z;
                                break;
                            }
                        }
                    }
                }
            }
            if (valid_cnt == 0) continue;
            int rz = valid_zs[rng() % valid_cnt];
            if (rx == old_x && ry == old_y && rz == old_z) continue;

            Instance* instB = nullptr;
            if (!blockB->isZFree(rz)) {
                for (Instance* e : blockB->getInsts()) if (e->getZ() == rz) { instB = e; break; }
            }

            Block* ba = glb_fpga.getBlock(old_x, old_y);
            Block* bb = blockB;
            if (ba) ba->removeInst(instA);
            if (bb && instB) bb->removeInst(instB);
            instA->setPosition(rx, ry, rz);
            if (instB) instB->setPosition(old_x, old_y, old_z);

            bool legalA = isLegal(instA, rx, ry, rz);
            bool legalB = instB ? isLegal(instB, old_x, old_y, old_z) : true;
            if (legalA && legalB) {
                g_current_token++;
                std::vector<Net*> affected_nets;
                int delta = 0;
                auto collect = [&](Instance* inst) {
                    if (!inst) return;
                    for (Net* net : inst->getNets()) {
                        if (net->getInsts().size() > 50) continue;
                        int nid = net->getNetId();
                        if (nid >= 0 && nid < (int)g_net_visited_token.size() && g_net_visited_token[nid] != g_current_token) {
                            g_net_visited_token[nid] = g_current_token;
                            affected_nets.push_back(net);
                            delta -= net->cached_hpwl;
                        }
                    }
                };
                collect(instA); collect(instB);
                std::vector<int> new_hpwls;
                for (Net* net : affected_nets) {
                    int val = net->evalHPWL_pure();
                    new_hpwls.push_back(val);
                    delta += val;
                }
                if (delta <= 0 || dist_prob(rng) < std::exp(-delta / T)) {
                    for (size_t i = 0; i < affected_nets.size(); ++i) affected_nets[i]->cached_hpwl = new_hpwls[i];
                    current_hpwl += delta;
                    if (bb) bb->addInst(instA);
                    if (ba && instB) ba->addInst(instB);
                } else {
                    instA->setPosition(old_x, old_y, old_z);
                    if (instB) instB->setPosition(rx, ry, rz);
                    if (ba) ba->addInst(instA);
                    if (bb && instB) bb->addInst(instB);
                }
            } else {
                instA->setPosition(old_x, old_y, old_z);
                if (instB) instB->setPosition(rx, ry, rz);
                if (ba) ba->addInst(instA);
                if (bb && instB) bb->addInst(instB);
            }
        }
        T *= 0.90;
        if (!is_small) std::cout << "[DEBUG] SA Step " << step+1 << "/" << num_steps << " HPWL = " << current_hpwl << std::endl;
    }

    // =========================================================================
    // ── 4. 后处理：局部贪心爬山 (Hill Climbing) ──
    // =========================================================================
    std::cout << "[INFO] Phase 4: Local Hill Climbing refinement..." << std::endl;
    for (int pass = 0; pass < 3; pass++) {
        for (Instance* inst : movable_insts) {
            int old_x = inst->getX(), old_y = inst->getY(), old_z = inst->getZ();
            if (old_x < 0) continue;
            int best_delta = 0;
            int best_x = old_x, best_y = old_y, best_z = old_z;
            int ft = inst->getFastType();
            // 搜索 3x3 邻域
            for (int dx = -2; dx <= 2; dx++) {
                for (int dy = -2; dy <= 2; dy++) {
                    int nx = old_x + dx, ny = old_y + dy;
                    if (nx < 0 || nx >= glb_fpga.getSizeX() || ny < 0 || ny >= glb_fpga.getSizeY()) continue;
                    Block* blk = glb_fpga.getBlock(nx, ny);
                    if (!blk) continue;
                    // 类型必须匹配
                    if ((ft >= 0 && ft <= 3) && blk->getType() != "SLICE") continue;
                    if (ft == 4 && blk->getType() != "DSP") continue;
                    if (ft == 5 && blk->getType() != "BRAM") continue;
                    if (ft == 6 && blk->getType() != "IO") continue;

                    int z_start = 0, z_end = 0;
                    if (ft == 0) { z_start=1; z_end=15; } else if (ft == 1) { z_start=0; z_end=15; }
                    else if (ft == 2) { z_start=16; z_end=31; } else if (ft == 3) { z_start=32; z_end=32; }
                    else if (ft == 6) { z_start=0; z_end=63; } else { z_start=0; z_end=0; }
                    for (int z = z_start; z <= z_end; z++) {
                        if (ft == 0 && z % 2 == 0) continue;
                        if (!blk->isZFree(z)) continue;
                        if (!isLegal(inst, nx, ny, z)) continue;
                        // 计算增量
                        int delta = 0;
                        for (Net* net : inst->getNets()) {
                            if (net->getInsts().size() > 50) continue;
                            int old_hpwl = net->cached_hpwl;
                            int old_x_t = inst->getX(), old_y_t = inst->getY(), old_z_t = inst->getZ();
                            inst->setPosition(nx, ny, z);
                            int new_hpwl = net->evalHPWL_pure();
                            inst->setPosition(old_x_t, old_y_t, old_z_t);
                            delta += (new_hpwl - old_hpwl);
                        }
                        if (delta < best_delta) {
                            best_delta = delta;
                            best_x = nx; best_y = ny; best_z = z;
                        }
                    }
                }
            }
            if (best_delta < 0) {
                Block* old_block = glb_fpga.getBlock(old_x, old_y);
                Block* new_block = glb_fpga.getBlock(best_x, best_y);
                old_block->removeInst(inst);
                inst->setPosition(best_x, best_y, best_z);
                new_block->addInst(inst);
                for (Net* net : inst->getNets()) {
                    if (net->getInsts().size() > 50) continue;
                    net->cached_hpwl = net->evalHPWL_pure();
                }
                current_hpwl += best_delta;
            }
        }
        if (is_small) std::cout << "[DEBUG] Hill climbing pass " << pass+1 << " HPWL = " << current_hpwl << std::endl;
    }

    long long final_hpwl = 0;
    for (auto& [id, net] : glb_net_map) final_hpwl += net->evalHPWL();
    std::cout << "[INFO] Workflow Finished. Final HPWL: " << final_hpwl << std::endl;
}
