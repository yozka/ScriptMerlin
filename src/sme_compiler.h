#pragma once

#include "sme_ast.h"
#include "sme_state.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace sme {

// Имена, известные всем функциям единицы компиляции.
// Индекс — слот в таблице нативов или глобалов движка.
struct CompileEnv {
    std::unordered_map<std::string, uint16_t> natives;
    std::unordered_map<std::string, uint16_t> globals;
};

// Два прохода на функцию: сначала собрать локалы (включая неявные из присваиваний),
// потом породить регистровый байткод. Размер кадра после этого фиксирован.
std::vector<Chunk> compile(const Program& program, const CompileEnv& env);

}  // пространство имён sme
