#include "core/counters.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace renderdoc::core {

namespace {

const char* compTypeToString(CompType type) {
    switch (type) {
        case CompType::Float:     return "Float";
        case CompType::UInt:      return "UInt";
        case CompType::SInt:      return "SInt";
        case CompType::UNorm:     return "UNorm";
        case CompType::SNorm:     return "SNorm";
        case CompType::Depth:     return "Depth";
        case CompType::UScaled:   return "UScaled";
        case CompType::SScaled:   return "SScaled";
        case CompType::UNormSRGB: return "UNormSRGB";
        default:                  return "Unknown";
    }
}

const char* counterUnitToString(CounterUnit unit) {
    switch (unit) {
        case CounterUnit::Absolute:   return "Absolute";
        case CounterUnit::Seconds:    return "Seconds";
        case CounterUnit::Percentage: return "Percentage";
        case CounterUnit::Ratio:      return "Ratio";
        case CounterUnit::Bytes:      return "Bytes";
        case CounterUnit::Cycles:     return "Cycles";
        case CounterUnit::Hertz:      return "Hertz";
        case CounterUnit::Volt:       return "Volt";
        case CounterUnit::Celsius:    return "Celsius";
        default:                      return "Unknown";
    }
}

double extractCounterValue(const CounterResult& r, CompType resultType, uint32_t byteWidth) {
    switch (resultType) {
        case CompType::Float:
            return (byteWidth >= 8) ? r.value.d : static_cast<double>(r.value.f);
        case CompType::UInt:
        case CompType::UNorm:
        case CompType::UScaled:
            return (byteWidth >= 8) ? static_cast<double>(r.value.u64) : static_cast<double>(r.value.u32);
        case CompType::SInt:
        case CompType::SNorm:
        case CompType::SScaled:
            return static_cast<double>(static_cast<int32_t>(r.value.u32));
        default:
            return (byteWidth >= 8) ? static_cast<double>(r.value.u64) : static_cast<double>(r.value.u32);
    }
}

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

} // anonymous namespace

std::vector<CounterInfo> listCounters(const Session& session) {
    auto* ctrl = session.controller();

    rdcarray<GPUCounter> counters = ctrl->EnumerateCounters();

    std::vector<CounterInfo> result;
    result.reserve(counters.size());

    for (const auto& c : counters) {
        CounterDescription desc = ctrl->DescribeCounter(c);
        std::string name(desc.name.c_str());
        if (name.find("ERROR") != std::string::npos)
            continue;

        CounterInfo info;
        info.id = static_cast<uint32_t>(c);
        info.name = std::move(name);
        info.category = std::string(desc.category.c_str());
        info.description = std::string(desc.description.c_str());
        info.resultType = compTypeToString(desc.resultType);
        info.resultByteWidth = desc.resultByteWidth;
        info.unit = counterUnitToString(desc.unit);
        result.push_back(std::move(info));
    }

    return result;
}

CounterFetchResult fetchCounters(const Session& session,
                                  const std::vector<std::string>& counterNames,
                                  std::optional<uint32_t> eventId) {
    auto* ctrl = session.controller();

    // Enumerate and describe all counters
    rdcarray<GPUCounter> allCounters = ctrl->EnumerateCounters();
    struct CounterMeta {
        GPUCounter id;
        std::string name;
        CompType resultType;
        uint32_t byteWidth;
        std::string unit;
    };
    std::map<uint32_t, CounterMeta> metaMap;

    for (const auto& c : allCounters) {
        CounterDescription desc = ctrl->DescribeCounter(c);
        std::string name(desc.name.c_str());
        if (name.find("ERROR") != std::string::npos)
            continue;
        metaMap[static_cast<uint32_t>(c)] = {c, name, desc.resultType, desc.resultByteWidth,
                                              counterUnitToString(desc.unit)};
    }

    // Filter by names (case-insensitive substring match)
    rdcarray<GPUCounter> requestedCounters;
    if (counterNames.empty()) {
        for (const auto& [id, meta] : metaMap)
            requestedCounters.push_back(meta.id);
    } else {
        for (const auto& [id, meta] : metaMap) {
            std::string lowerName = toLower(meta.name);
            for (const auto& filter : counterNames) {
                if (lowerName.find(toLower(filter)) != std::string::npos) {
                    requestedCounters.push_back(meta.id);
                    break;
                }
            }
        }
    }

    if (requestedCounters.empty()) {
        return CounterFetchResult{};
    }

    // Fetch counter values
    rdcarray<CounterResult> results = ctrl->FetchCounters(requestedCounters);

    CounterFetchResult out;
    out.totalCounters = static_cast<uint32_t>(requestedCounters.size());

    uint32_t maxEvent = 0;
    for (const auto& r : results) {
        if (eventId.has_value() && r.eventId != *eventId)
            continue;

        uint32_t cid = static_cast<uint32_t>(r.counter);
        auto it = metaMap.find(cid);
        if (it == metaMap.end())
            continue;

        const auto& meta = it->second;
        CounterSample sample;
        sample.eventId = r.eventId;
        sample.counterId = cid;
        sample.counterName = meta.name;
        sample.value = extractCounterValue(r, meta.resultType, meta.byteWidth);
        sample.unit = meta.unit;
        out.rows.push_back(std::move(sample));

        if (r.eventId > maxEvent) maxEvent = r.eventId;
    }

    out.totalEvents = maxEvent;
    return out;
}

} // namespace renderdoc::core
