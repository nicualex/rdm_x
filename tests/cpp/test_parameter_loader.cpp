// tests/cpp/test_parameter_loader.cpp
// Unit tests for LoadParameters() — CSV parsing via temporary files.
// No hardware is opened.
#include <gtest/gtest.h>
#include "parameter_loader.h"
#include <fstream>
#include <string>
#include <windows.h>

// ── RAII temp file helper ────────────────────────────────────────────────
struct TempCSV {
    std::string path;

    explicit TempCSV(const std::string& content) {
        char dir[MAX_PATH], buf[MAX_PATH];
        GetTempPathA(MAX_PATH, dir);
        GetTempFileNameA(dir, "rdm", 0, buf);
        path = buf;
        std::ofstream f(path, std::ios::trunc);
        f << content;
    }
    ~TempCSV() { DeleteFileA(path.c_str()); }

    // Non-copyable
    TempCSV(const TempCSV&) = delete;
    TempCSV& operator=(const TempCSV&) = delete;
};

// ── CSV fixtures ─────────────────────────────────────────────────────────
// The real Vaya_RDM_map.csv has:
//   Row 1: column header row  (skipped)
//   Row 2: sub-header row     (skipped)
//   Row 3+: data rows
// Columns (0-indexed): [0]=unused [1]=mandatory "Y"/"" [2]=commandClass
//                      [3]=PID hex [4]=name [5]=unused [6]=description

static const char* kHeadersOnly =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n";

static const char* kOneGetRow =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",Y,GET_COMMAND (0x20),0060,Get Device Info,19 bytes,See RDM Standard\n";

static const char* kNonGetRows =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",Y,SET_COMMAND (0x30),0082,Set Device Label,Variable,See RDM Standard\n"
    ",Y,DISCOVERY_COMMAND (0X10),0001,RDM Disc Unique Branch,,See RDM standard\n";

static const char* kMultipleRows =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",Y,GET_COMMAND (0x20),0050,Get supported parameters,Variable,See RDM Standard\n"
    ",,GET_COMMAND (0x20),0060,Get Device Info,19 bytes,See RDM Standard\n"
    ",Y,GET_COMMAND (0x20),00F0,Get DMX start address,2 bytes,Description\n";

static const char* kZeroPID =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",,(reserved),0000,(reserved) - pad byte,,\n"
    ",Y,GET_COMMAND (0x20),0060,Get Device Info,19 bytes,See RDM Standard\n";

static const char* kHexPrefixPID =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",Y,GET_COMMAND (0x20),0x00F0,Get DMX start address,2 bytes,Description\n";

static const char* kMixedMandatory =
    ",,Command Class,PID,Purpose,Payload,Description\n"
    ",Vaya Must have,,,,\n"
    ",Y,GET_COMMAND (0x20),0050,Supported Params,Variable,\n"
    ",,GET_COMMAND (0x20),0060,Device Info,19 bytes,\n"
    ",Y,GET_COMMAND (0x20),00F0,DMX Address,2 bytes,\n";

// ═══════════════════════════════════════════════════════════════════════════

TEST(LoadParameters, MissingFileReturnsEmpty) {
    auto params = LoadParameters("Z:/does/not/exist/bogus_rdm_map.csv");
    EXPECT_TRUE(params.empty());
}

TEST(LoadParameters, EmptyFileReturnsEmpty) {
    TempCSV f("");
    EXPECT_TRUE(LoadParameters(f.path).empty());
}

TEST(LoadParameters, HeadersOnlyReturnsEmpty) {
    TempCSV f(kHeadersOnly);
    EXPECT_TRUE(LoadParameters(f.path).empty());
}

TEST(LoadParameters, SingleGetCommandRowCount) {
    TempCSV f(kOneGetRow);
    EXPECT_EQ(LoadParameters(f.path).size(), 1u);
}

TEST(LoadParameters, SingleGetCommandPID) {
    TempCSV f(kOneGetRow);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].pid, 0x0060);
}

TEST(LoadParameters, SingleGetCommandName) {
    TempCSV f(kOneGetRow);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].name, "Get Device Info");
}

TEST(LoadParameters, SingleGetCommandMandatoryTrue) {
    TempCSV f(kOneGetRow);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_TRUE(params[0].isMandatory);
}

TEST(LoadParameters, NonGetCommandRowsExcluded) {
    TempCSV f(kNonGetRows);
    EXPECT_TRUE(LoadParameters(f.path).empty());
}

TEST(LoadParameters, MultipleRowsCount) {
    TempCSV f(kMultipleRows);
    EXPECT_EQ(LoadParameters(f.path).size(), 3u);
}

TEST(LoadParameters, MultipleRowsPIDsInOrder) {
    TempCSV f(kMultipleRows);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].pid, 0x0050);
    EXPECT_EQ(params[1].pid, 0x0060);
    EXPECT_EQ(params[2].pid, 0x00F0);
}

TEST(LoadParameters, MandatoryFlagParsed) {
    TempCSV f(kMixedMandatory);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 3u);
    EXPECT_TRUE(params[0].isMandatory);   // "Y"
    EXPECT_FALSE(params[1].isMandatory);  // empty
    EXPECT_TRUE(params[2].isMandatory);   // "Y"
}

TEST(LoadParameters, ZeroPIDRowSkipped) {
    TempCSV f(kZeroPID);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].pid, 0x0060);
}

TEST(LoadParameters, HexPrefixPIDParsed) {
    TempCSV f(kHexPrefixPID);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].pid, 0x00F0);
}

TEST(LoadParameters, CommandClassContainsGetCommand) {
    TempCSV f(kOneGetRow);
    auto params = LoadParameters(f.path);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_NE(params[0].commandClass.find("GET_COMMAND"), std::string::npos);
}
