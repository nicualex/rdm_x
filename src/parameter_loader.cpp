// ────────────────────────────────────────────────────────────────────────
// ParameterLoader — CSV parser for the Vaya RDM parameter map
// ────────────────────────────────────────────────────────────────────────
#include "parameter_loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// ── Trim whitespace ─────────────────────────────────────────────────────
static std::string Trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// ── Simple CSV line splitter (handles quoted fields with commas) ────────
static std::vector<std::string> SplitCSVLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(Trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(Trim(field));
    return fields;
}

// ── Parse hex PID string to uint16_t ────────────────────────────────────
static uint16_t ParseHexPID(const std::string& s)
{
    // Remove "0x" prefix if present
    std::string cleaned = Trim(s);
    if (cleaned.size() >= 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X'))
        cleaned = cleaned.substr(2);
    if (cleaned.empty()) return 0;
    return static_cast<uint16_t>(strtoul(cleaned.c_str(), nullptr, 16));
}

// ── Load parameters from CSV ────────────────────────────────────────────
//
// CSV layout (from Vaya_RDM_map.csv):
//   Col A (0): (empty or category)
//   Col B (1): Vaya Must Have — "Y" if mandatory
//   Col C (2): Command Class — e.g. "GET_COMMAND (0x20)"
//   Col D (3): PID hex — e.g. "0060"
//   Col E (4): Purpose / Name
//   Col F (5): Payload Length
//   Col G (6): Description (may span multiple "lines" inside quotes)
//
std::vector<RDMParameter> LoadParameters(const std::string& csvPath)
{
    std::vector<RDMParameter> params;

    std::ifstream file(csvPath);
    if (!file.is_open()) return params;

    // Read the entire file into a string to handle multi-line quoted fields
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // We need to split into logical CSV records. A record ends at a newline
    // that is NOT inside a quoted field.
    std::vector<std::string> records;
    {
        std::string rec;
        bool inQ = false;
        for (char c : content) {
            if (c == '"') inQ = !inQ;
            if (c == '\n' && !inQ) {
                records.push_back(rec);
                rec.clear();
            } else {
                rec += c;
            }
        }
        if (!rec.empty()) records.push_back(rec);
    }

    // Skip the first two rows (headers)
    for (size_t r = 2; r < records.size(); ++r) {
        auto fields = SplitCSVLine(records[r]);
        if (fields.size() < 5) continue;

        // Col C (index 2) must contain "GET_COMMAND"
        std::string cmdClass = fields[2];
        if (cmdClass.find("GET_COMMAND") == std::string::npos)
            continue;

        // Col D (index 3): PID
        uint16_t pid = ParseHexPID(fields[3]);
        if (pid == 0) continue;  // skip the reserved/padding row

        RDMParameter p;
        p.pid          = pid;
        p.commandClass = cmdClass;
        p.name         = fields[4];   // "Purpose" column
        p.isMandatory  = (Trim(fields[1]) == "Y");
        if (fields.size() > 6)
            p.description = fields[6];

        params.push_back(std::move(p));
    }

    return params;
}
