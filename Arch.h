#pragma once
#ifndef ARCH_H
#define ARCH_H

#include <vector>
#include "Object.h"

#define MAX_BLOCK_CAPACITY 1

// 替换 Arch.h 中的 class Block
class Block
{
private:
    std::string type; // 【新增】Block类型，如 SLICE, DSP, BRAM, IO
    std::vector<Instance *> contain;

public:
    void setType(std::string t) { this->type = t; }
    std::string getType() { return this->type; }

    bool addInst(Instance *i_inst) {
        // 大作业中，不能简单用数量判断，暂时只做存储
        this->contain.push_back(i_inst);
        return true;
    }
    
    // 按对象移除（因为一个Block可能有多个元件了）
    bool removeInst(Instance *i_inst) {
        for (auto it = contain.begin(); it != contain.end(); ++it) {
            if (*it == i_inst) {
                contain.erase(it);
                return true;
            }
        }
        return false;
    }

    bool clearInst() { this->contain.clear(); return true; }
    std::vector<Instance *> getInsts() { return this->contain; }
    int getInstsCount() { return (int)this->contain.size(); }
};

class FPGA
{
private:
    std::vector<std::vector<Block *>> fpga_blocks;
    int size_x, size_y;

public:
    FPGA()
    {
        this->size_x = 0;
        this->size_y = 0;
    };
    ~FPGA();

    // 初始化芯片
    void initialize();
    // 设定芯片的规模
    void setSize(int i_size_x, int i_size_y);
    // 获得芯片规模
    int getSizeX() { return this->size_x; }
    int getSizeY() { return this->size_y; }
    // 向指定位置添加 inst 对象
    bool addInst(int i_x, int i_y, Instance *i_inst);
    // 清除指定位置的 inst 对象
    void clearInst(int i_x, int i_y) { this->fpga_blocks[i_x][i_y]->clearInst(); }
    // 访问 FPGA 中特定的 Block
    Block *getBlock(int i_x, int i_y);
    // 报告芯片的基本信息
    void reportFPGA();
};

#endif