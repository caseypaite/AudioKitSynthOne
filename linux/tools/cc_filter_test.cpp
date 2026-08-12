//
//  cc_filter_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises CcFilter/CcQueue (host/CcFilter.h) against synthetic data --
//  no MIDI hardware, no audio backend, no started Engine needed. Prints
//  PASS/FAIL per case and exits non-zero on any failure, mirroring
//  tools/pad_filter_test.cpp.
//

#include "CcFilter.h"

#include <cstdio>

namespace {

int gFailures = 0;

void check(bool condition, const char *what) {
    if (condition) {
        std::printf("  PASS  %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++gFailures;
    }
}

} // namespace

int main() {
    // -- CcFilter: claim/unclaim/isClaimedCc, channel wildcard -----------
    {
        s1::CcFilter filter;
        check(!filter.isClaimedCc(0, 60), "fresh filter claims nothing");

        filter.claimCc(60);
        check(filter.isClaimedCc(0, 60), "claimed CC matches on channel 0");
        check(filter.isClaimedCc(9, 60), "claimed CC matches on any channel by default");
        check(!filter.isClaimedCc(0, 61), "unclaimed CC on the same channel doesn't match");

        filter.claimChannel(1);
        check(filter.isClaimedCc(1, 60), "claimed CC matches its restricted channel");
        check(!filter.isClaimedCc(0, 60), "claimed CC no longer matches other channels once restricted");

        filter.unclaimCc(60);
        check(!filter.isClaimedCc(1, 60), "unclaimed CC no longer matches");

        filter.claimCc(10);
        filter.claimCc(127);
        filter.claimChannel(-1);
        check(filter.isClaimedCc(3, 10), "second claimed CC matches (low word)");
        check(filter.isClaimedCc(3, 127), "claimed CC 127 matches (high word boundary)");
        check(!filter.isClaimedCc(3, 128), "out-of-range CC never matches"); // guarded internally

        filter.clear();
        check(!filter.isClaimedCc(3, 10), "clear() unclaims everything");
        check(!filter.isClaimedCc(3, 127), "clear() unclaims the high-word CC too");
    }

    // -- CcQueue: push/pop round-trip and capacity -----------------------
    {
        s1::CcQueue queue;
        s1::CcMessage in;
        in.channel = 1;
        in.cc = 115;
        in.value = 127;
        in.sourceId = 7;
        check(queue.push(in), "cc queue push succeeds");

        s1::CcMessage out;
        check(queue.pop(out), "cc queue pop succeeds");
        check(out.channel == 1 && out.cc == 115 && out.value == 127 && out.sourceId == 7,
             "cc queue round-trip preserves all fields");

        s1::CcMessage empty;
        check(!queue.pop(empty), "cc queue pop on empty queue fails");

        for (size_t i = 0; i < s1::CcQueue::kCapacity - 1; ++i) {
            check(queue.push(in), "cc queue push while not full succeeds");
        }
        check(!queue.push(in), "cc queue push when full fails");
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
