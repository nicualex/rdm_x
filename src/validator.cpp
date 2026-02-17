// ────────────────────────────────────────────────────────────────────────
// Validator — Implementation
// ────────────────────────────────────────────────────────────────────────
#include "validator.h"
#include "enttec_pro.h"
#include <cstdio>

// ── Hex formatter ───────────────────────────────────────────────────────
std::string BytesToHex(const uint8_t* data, int len)
{
    std::string out;
    out.reserve(len * 3);
    for (int i = 0; i < len; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        out += buf;
    }
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// ── Validate fixture ────────────────────────────────────────────────────
std::vector<ValidationResult> ValidateFixture(
    EnttecPro& pro,
    uint64_t srcUID,
    uint64_t destUID,
    const std::vector<RDMParameter>& params)
{
    std::vector<ValidationResult> results;
    results.reserve(params.size());

    for (const auto& param : params) {
        ValidationResult vr;
        vr.pid         = param.pid;
        vr.name        = param.name;
        vr.isMandatory = param.isMandatory;

        // Skip discovery PIDs — they aren't standard GET targets
        if (param.pid <= 0x0003) {
            vr.status       = ValidationStatus::GREEN;
            vr.value        = "(discovery)";
            vr.responseType = RDMResponseType::ACK;
            results.push_back(std::move(vr));
            continue;
        }

        // Send GET_COMMAND
        RDMResponse resp = RDMGetCommand(pro, srcUID, destUID, param.pid);
        vr.responseType = resp.type;

        switch (resp.type) {
        case RDMResponseType::ACK:
            // Case C: valid data → GREEN
            vr.status = ValidationStatus::GREEN;
            if (!resp.data.empty())
                vr.value = BytesToHex(resp.data.data(), static_cast<int>(resp.data.size()));
            else
                vr.value = "(empty)";
            break;

        case RDMResponseType::ACK_TIMER:
            // Treat as success but note it
            vr.status = ValidationStatus::GREEN;
            vr.value  = "(ACK_TIMER)";
            break;

        case RDMResponseType::NACK:
        case RDMResponseType::TIMEOUT:
        case RDMResponseType::INVALID:
        default:
            // Case A: mandatory + fail → RED
            // Case B: optional + fail → YELLOW
            if (param.isMandatory)
                vr.status = ValidationStatus::RED;
            else
                vr.status = ValidationStatus::YELLOW;

            if (resp.type == RDMResponseType::NACK)
                vr.value = "NACK (0x" + BytesToHex(
                    reinterpret_cast<const uint8_t*>(&resp.nackReason), 2) + ")";
            else if (resp.type == RDMResponseType::TIMEOUT)
                vr.value = "TIMEOUT";
            else
                vr.value = "INVALID";
            break;
        }

        results.push_back(std::move(vr));
    }

    return results;
}
