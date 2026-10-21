#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using AgcDriver::DriverDetail::DrawPipeline;

struct Probe {
    std::atomic<bool>& entered;
    std::atomic<bool>& destroyed;
    Probe(std::atomic<bool>& enteredFlag, std::atomic<bool>& destroyedFlag) : entered(enteredFlag), destroyed(destroyedFlag) {}
    ~Probe() {
        entered.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        destroyed.store(true);
    }
};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void DrainWaitsForDestruction() {
    auto& pipeline = DrawPipeline::Queue0();
    for (int round = 0; round < 10; ++round) {
        std::atomic<bool> entered{false};
        std::atomic<bool> destroyed{false};
        pipeline.Enqueue([probe = std::make_shared<Probe>(entered, destroyed)] {}, {{0x1000, 0x1010}});
        while (!entered.load()) std::this_thread::yield();
        Require(pipeline.Busy(), "pipeline not busy while the closure destructor runs");
        Require(!destroyed.load(), "closure destructor finished before Drain was called");
        pipeline.Drain(DrawPipeline::DrainReason::Flush);
        Require(destroyed.load(), "Drain returned before the commit closure was destroyed");
        Require(!pipeline.Busy(), "pipeline still busy after Drain");
        Require(!pipeline.Overlaps(0x1000, 0x10), "retired commit still overlaps");
        Require(!pipeline.PendingLabel(0x1000, 4).has_value(), "retired commit still has a pending label");
    }
}

}

int main() {
    if (DrawPipeline::Depth() == 0) {
        std::puts("draw pipeline disabled, nothing to retire");
        return 0;
    }
    try {
        DrainWaitsForDestruction();
        std::puts("Drain returned only after the commit closure was destroyed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
