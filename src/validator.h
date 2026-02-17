#pragma once
// ────────────────────────────────────────────────────────────────────────
// Validator — Iterates parameters and classifies responses
// ────────────────────────────────────────────────────────────────────────
#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "parameter_loader.h"
#include "rdm.h"
#include <cstdint>
#include <string>
#include <vector>

class EnttecPro;

enum class ValidationStatus { GREEN, YELLOW, RED };

struct ValidationResult {
    uint16_t         pid         = 0;
    std::string      name;
    bool             isMandatory = false;
    ValidationStatus status      = ValidationStatus::RED;
    std::string      value;        // hex-encoded response data
    RDMResponseType  responseType = RDMResponseType::TIMEOUT;
};

// Validate all GET_COMMAND parameters against the given fixture UID.
// `srcUID` is this controller's UID.
// Results are returned in the same order as `params`.
std::vector<ValidationResult> ValidateFixture(
    EnttecPro& pro,
    uint64_t srcUID,
    uint64_t destUID,
    const std::vector<RDMParameter>& params);

// Convert raw bytes to a hex string like "0A 1B FF"
std::string BytesToHex(const uint8_t* data, int len);

#endif // VALIDATOR_H
