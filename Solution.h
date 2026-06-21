#pragma once
#ifndef SOLUTION_H
#define SOLUTION_H

#include <string>
#include "Global.h"
#include "Object.h"

class Solution
{
public:
    void place();
};

int readBenchMarkFile(std::string i_file_name);
int reportAndSaveResult(std::string i_file_name, double elapsed_time);
int reportWireLength();
int reportValid();
void solvePlacement();
#endif