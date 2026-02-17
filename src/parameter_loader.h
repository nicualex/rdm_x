#pragma once
// ────────────────────────────────────────────────────────────────────────
// ParameterLoader — Parses the Vaya RDM map CSV into a parameter list
// ────────────────────────────────────────────────────────────────────────
#ifndef PARAMETER_LOADER_H
#define PARAMETER_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

struct RDMParameter {
    uint16_t    pid          = 0;
    std::string name;
    std::string commandClass;  // "GET_COMMAND (0x20)", "SET_COMMAND (0x30)", etc.
    bool        isMandatory  = false;  // "Y" in "Vaya Must Have" column
    std::string description;
};

// Load the CSV and return all GET_COMMAND parameters.
// `csvPath` is the filesystem path to Vaya_RDM_map.csv.
std::vector<RDMParameter> LoadParameters(const std::string& csvPath);

#endif // PARAMETER_LOADER_H
