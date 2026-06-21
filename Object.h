#pragma once
#ifndef OBJECT_H
#define OBJECT_H

#define MAX_INT 0x3f3f3f3f
#include <algorithm>
#include <set>
#include <iostream>
#include <map>     
#include <string>
#include <vector>

class Net;

class Instance
{
private:
    int x;
    int y;
    int z; // 【新增】Z坐标 (表示在一个Block中的具体槽位)
    bool fixed;
    int Inst_id;
    int fast_type;
    std::string name; // 【新增】元件名字，如 inst_1024
    std::string type; // 【新增】元件类型，如 LUT6, FDRE, DSP48E2
    std::vector<Net *> connect_net;

public:
    Net* lut_inputs[6] = {nullptr}; // 存 I0 到 I5
    Net* ff_ctrl[3] = {nullptr};    // 存 C, R, CE 分别对应 0, 1, 2

    Instance() { x = -1; y = -1; z = -1; fixed = false; Inst_id = -1; }
    Instance(int i_x, int i_y, int i_z, int i_inst_id, bool i_fixed = false) 
        : x(i_x), y(i_y), z(i_z), Inst_id(i_inst_id), fixed(i_fixed) {}

    
    void setFastType(int ft) { fast_type = ft; }
    int getFastType() { return fast_type; }
    void setFixed(bool i_fixed) { this->fixed = i_fixed; }
    bool isFixed() { return fixed; }
    
    // 恢复 getPosition 接口，返回 2D 坐标对
    std::pair<int, int> getPosition() { return std::pair<int, int>(this->x, this->y); }

    // 【新增】名称和类型相关
    void setName(std::string n) { this->name = n; }
    std::string getName() { return this->name; }
    void setType(std::string t) { this->type = t; }
    std::string getType() { return this->type; }

    
    void addPinNet(std::string pin, Net* n) { 
        if (pin.length() >= 2 && pin[0] == 'I') {
            int idx = pin[1] - '0';
            if (idx >= 0 && idx <= 5) lut_inputs[idx] = n;
        } else if (pin == "C") {
            ff_ctrl[0] = n;
        } else if (pin == "R") {
            ff_ctrl[1] = n;
        } else if (pin == "CE") {
            ff_ctrl[2] = n;
        }
        
        // vector 没有 insert(value)，改为 push_back 并去重
        if (std::find(connect_net.begin(), connect_net.end(), n) == connect_net.end()) {
            connect_net.push_back(n); 
        }
    }
    std::vector<Net *> getNets() { return connect_net; }
    void addNet(Net *i_net_id) { 
        if (std::find(connect_net.begin(), connect_net.end(), i_net_id) == connect_net.end()) {
            connect_net.push_back(i_net_id); 
        }
    };
    void setPosition(int i_x, int i_y, int i_z = 0) {
        if (this->isFixed()) return;
        this->x = i_x; this->y = i_y; this->z = i_z;
    }
    int getX() { return x; }
    int getY() { return y; }
    int getZ() { return z; }
    
    void setInstId(int i_inst_id) { this->Inst_id = i_inst_id; }
    int getInstId() { return this->Inst_id; }
};


class Net
{
private:
    int net_id;
    std::string name;     // 【新增】线网名称
    double weight = 1.0;  // 【新增】线网权重，默认 1.0
    std::vector<Instance *> connect_inst;

public:
    int cached_hpwl = 0;
    Net() { net_id = -1; }

    void setNetId(int i_net_id) { this->net_id = i_net_id; }
    int getNetId() { return this->net_id; }
    
    void setName(std::string n) { this->name = n; }
    std::string getName() { return this->name; }
    void setWeight(double w) { this->weight = w; }
    double getWeight() { return this->weight; }

    void addInst(Instance *i_inst) { 
        if (std::find(connect_inst.begin(), connect_inst.end(), i_inst) == connect_inst.end()) {
            connect_inst.push_back(i_inst); 
        }
    }
    
    const std::vector<Instance *>& getInsts() const { return this->connect_inst; }
    int evalHPWL();
    // 纯净的计算函数，只计算，不修改缓存
    int evalHPWL_pure() {
        int min_x = 1e9, max_x = -1e9;
        int min_y = 1e9, max_y = -1e9;
        
        // 由于 connect_inst 已经是 std::vector，这里的遍历是光速的
        for (Instance* inst : connect_inst) {
            int x = inst->getX(), y = inst->getY();
            if (x < 0 || y < 0) continue; // 未放置的跳过
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
        
        if (min_x == 1e9) return 0;
        return (max_x - min_x) + (max_y - min_y);
    }
    
    // 初始化和更新缓存的接口
    void updateCache() {
        cached_hpwl = evalHPWL_pure();
    }
};

#endif