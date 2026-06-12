#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono> // 保留队友的计时库

#include "Global.h"
#include "Solution.h"

int main(int argv, char** argc){
    if (argv != 3){ // 现在只需要输入和输出两个参数
        std::printf("usage: ./main <input_dir> <output.txt>\n");
        return -2;
    }
    std::string l_input_file_name(argc[1]); // 这里现在传的是文件夹路径，如 ./data/FPGA-example1/
    int result = 0;

    result = readBenchMarkFile(l_input_file_name);
    if (result != 0){
        std::printf("read benchmark file failed\n");
        return -1;
    }

    // ================= 开始计时 =================
    auto start_time = std::chrono::high_resolution_clock::now();

    // 直接调用唯一的布局算法
    solvePlacement();

    // ================= 结束计时 =================
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    std::string l_output_file_name(argc[2]);
    result = reportAndSaveResult(l_output_file_name, elapsed.count());
    // result = reportValid();

    // std::cout << "FINAL_RESULT " << final_hpwl << " " << elapsed.count() << std::endl;

    for (auto lo_inst : glb_inst_map) delete lo_inst.second;
    for (auto lo_net : glb_net_map) delete lo_net.second;
    glb_inst_map.clear();
    glb_net_map.clear();
    
    std::printf("[INFO] Program exited with: %d errors\n", result);
    return result == 0 ? 0 : -1;
}