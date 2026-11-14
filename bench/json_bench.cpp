// JsonCore / wjson parse + access + dump micro benchmark.
// Build with one of:
//   -DBENCH_WJSON     (old agl/wjson, global class JSON)
//   -DBENCH_JSONCORE  (JsonCore native JsonElement)
//   -DBENCH_WRAPPER   (Terminal::Json adapter over JsonCore)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifdef BENCH_WJSON
#include "wjson.h"
#elif defined(BENCH_JSONCORE)
#include "JsonCore.hpp"
#elif defined(BENCH_WRAPPER)
#include "Core/TerminalJson.hpp"
#else
#error "define one benchmark backend"
#endif

namespace bench {

std::string MakeInput(int count) {
    std::string s;
    s.reserve(static_cast<size_t>(count) * 128);
    s += "{\"meta\":{\"count\":" + std::to_string(count) + ",\"version\":1},\"elements\":[";
    for (int i = 0; i < count; ++i) {
        if (i) s += ',';
        s += "{\"id\":" + std::to_string(i);
        s += ",\"name\":\"item-" + std::to_string(i) + "\"";
        s += ",\"score\":";
        s += std::to_string((i * 17) % 1000) + ".25";
        s += ",\"enabled\":";
        s += (i % 2) ? "true" : "false";
        s += ",\"tags\":[\"alpha\",\"beta\",\"gamma\"]";
        s += ",\"nested\":{\"x\":" + std::to_string(i % 31) + ",\"y\":" + std::to_string(i % 23) + "}}";
    }
    s += "]}";
    return s;
}

template <typename Fn>
double TimeMs(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Fn>
void Run(const char* name, int iterations, Fn&& fn) {
    std::vector<double> samples;
    samples.reserve(3);
    for (int r = 0; r < 3; ++r) samples.push_back(TimeMs(fn));
    double best = samples[0];
    for (double v : samples) best = std::min(best, v);
    std::cout << std::left << std::setw(12) << name
              << " total=" << std::setw(9) << std::fixed << std::setprecision(1) << best
              << " ms/run=" << std::setprecision(4) << (best / iterations)
              << " iter=" << iterations << "\n";
}

} // namespace bench

int main() {
    using namespace bench;
    constexpr int kParseIterations = 120;
    constexpr int kDumpIterations = 80;
    const std::string input = MakeInput(1500);

#if defined(BENCH_WJSON)
    std::cout << "backend=wjson input_bytes=" << input.size() << "\n";
    double sum = 0.0;
    Run("parse+walk", kParseIterations, [&] {
        double local = 0.0;
        for (int i = 0; i < kParseIterations; ++i) {
            JSON root = JSON::parse(input);
            auto& arr = root.GetObject().at("elements").GetArray();
            for (auto& e : arr) {
                auto& o = e.GetObject();
                local += o.at("score").GetNumber();
                local += static_cast<double>(o.at("id").GetNumber());
                local += static_cast<double>(o.at("name").GetString().size());
            }
        }
        sum += local;
    });
    JSON root = JSON::parse(input);
    std::size_t bytes = 0;
    Run("dump", kDumpIterations, [&] {
        for (int i = 0; i < kDumpIterations; ++i) bytes += root.toString().size();
    });
    std::cout << "checksum=" << std::setprecision(1) << sum << " dumped_bytes=" << bytes << "\n";

#elif defined(BENCH_JSONCORE)
    std::cout << "backend=JsonCore(native) input_bytes=" << input.size() << "\n";
    double sum = 0.0;
    Run("parse+walk", kParseIterations, [&] {
        double local = 0.0;
        for (int i = 0; i < kParseIterations; ++i) {
            ::JSON::JsonElement root = ::JSON::Parse(input);
            auto* top = root.asUnorderedObject();
            auto* arr = top->find("elements")->second.asArray();
            for (auto& e : *arr) {
                auto* o = e.asUnorderedObject();
                local += o->find("score")->second.getNumber();
                local += static_cast<double>(o->find("id")->second.getNumber());
                local += static_cast<double>(o->find("name")->second.getString().size());
            }
        }
        sum += local;
    });
    ::JSON::JsonElement root = ::JSON::Parse(input);
    std::size_t bytes = 0;
    Run("dump", kDumpIterations, [&] {
        for (int i = 0; i < kDumpIterations; ++i) bytes += root.dump().size();
    });
    std::cout << "checksum=" << std::setprecision(1) << sum << " dumped_bytes=" << bytes << "\n";

#elif defined(BENCH_WRAPPER)
    std::cout << "backend=Terminal::Json(wrapper) input_bytes=" << input.size() << "\n";
    double sum = 0.0;
    Run("parse+walk", kParseIterations, [&] {
        double local = 0.0;
        for (int i = 0; i < kParseIterations; ++i) {
            Terminal::Json root = Terminal::Json::parse(input);
            for (const auto& e : root.get("elements").GetArray()) {
                local += e.get("score").GetNumber();
                local += e.get("id").GetNumber();
                local += static_cast<double>(e.get("name").GetString().size());
            }
        }
        sum += local;
    });
    Terminal::Json root = Terminal::Json::parse(input);
    std::size_t bytes = 0;
    Run("dump", kDumpIterations, [&] {
        for (int i = 0; i < kDumpIterations; ++i) bytes += root.dump().size();
    });
    std::cout << "checksum=" << std::setprecision(1) << sum << " dumped_bytes=" << bytes << "\n";
#endif
    return 0;
}
