#pragma once
#ifndef ARCH_H
#define ARCH_H

#include <vector>
#include "Object.h"

#define MAX_BLOCK_CAPACITY 1

// Arch.h 中的 Block 类
class Block
{
private:
    std::string type;
    std::vector<Instance *> contain;
    bool z_occupied[64] = {false};   // 新增：快速槽位占用表

public:

    void setType(std::string t) { this->type = t; }
    std::string getType() { return this->type; }

    bool isZFree(int z) const { 
        return (z >= 0 && z < 64) ? !z_occupied[z] : false; 
    }
    void setZOccupied(int z, bool occupied) {
        if (z >= 0 && z < 64) z_occupied[z] = occupied;
    }

    bool addInst(Instance *i_inst) {
        int z = i_inst->getZ();
        if (!isZFree(z)) return false;
        contain.push_back(i_inst);
        setZOccupied(z, true);
        return true;
    }

    bool removeInst(Instance *i_inst) {
        for (auto it = contain.begin(); it != contain.end(); ++it) {
            if (*it == i_inst) {
                setZOccupied(i_inst->getZ(), false);
                contain.erase(it);
                return true;
            }
        }
        return false;
    }

    bool clearInst() { 
        for (auto inst : contain) setZOccupied(inst->getZ(), false);
        contain.clear(); 
        return true; 
    }

    const std::vector<Instance*>& getInsts() const { return contain; }  // 改为 const 引用避免拷贝
    int getInstsCount() const { return (int)contain.size(); }
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