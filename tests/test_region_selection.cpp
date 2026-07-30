#include "doctest.h"

#include "moe/bank/RegionSelector.h"

using namespace moe;
using namespace moe::bank;

namespace
{

Region makeRegion (int loKey, int hiKey, int loVel, int hiVel,
                   Articulation articulation = Articulation::sustain)
{
    Region region;
    region.sampleId     = 0;
    region.loKey        = loKey;
    region.hiKey        = hiKey;
    region.loVel        = loVel;
    region.hiVel        = hiVel;
    region.rootKey      = loKey;
    region.articulation = articulation;
    return region;
}

} // namespace

TEST_CASE ("velocity layers select the matching region")
{
    Instrument instrument;
    instrument.regions.push_back (makeRegion (60, 72, 1, 63));     // soft layer
    instrument.regions.push_back (makeRegion (60, 72, 64, 127));   // loud layer
    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note = 64;

    context.velocity = 30;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 0);

    context.velocity = 100;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 1);
}

TEST_CASE ("notes outside the mapped range produce nothing")
{
    Instrument instrument;
    instrument.regions.push_back (makeRegion (60, 72, 1, 127));
    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note = 40;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 0);

    context.note = 80;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 0);
}

TEST_CASE ("round-robin cycles and never repeats consecutively")
{
    Instrument instrument;

    for (int position = 1; position <= 4; ++position)
    {
        auto region = makeRegion (60, 60, 1, 127);
        region.seqLength   = 4;
        region.seqPosition = position;
        instrument.regions.push_back (region);
    }

    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note = 60;

    std::vector<int> order;
    for (int i = 0; i < 8; ++i)
    {
        REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
        order.push_back (selected[0].regionIndex);
    }

    CHECK (order == std::vector<int> { 0, 1, 2, 3, 0, 1, 2, 3 });
}

TEST_CASE ("round-robin counters are per key, so a chord does not skip variants")
{
    Instrument instrument;

    for (int note : { 60, 62 })
        for (int position = 1; position <= 2; ++position)
        {
            auto region = makeRegion (note, note, 1, 127);
            region.seqLength   = 2;
            region.seqPosition = position;
            instrument.regions.push_back (region);
        }

    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];
    SelectionContext context;

    // Playing 60 twice must alternate, regardless of what happened on 62.
    context.note = 60;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    const int first = selected[0].regionIndex;

    context.note = 62;
    selectRegions (instrument, context, state, selected, 4);

    context.note = 60;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex != first);
}

TEST_CASE ("keyswitches gate which articulation sounds")
{
    Instrument instrument;

    auto sustain = makeRegion (60, 72, 1, 127, Articulation::sustain);
    sustain.swLast = 24;

    auto staccato = makeRegion (60, 72, 1, 127, Articulation::staccato);
    staccato.swLast = 25;

    instrument.regions.push_back (sustain);
    instrument.regions.push_back (staccato);
    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note = 64;
    context.allowArticulationFallback = false;

    context.articulation  = Articulation::sustain;
    context.lastKeyswitch = 24;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 0);

    context.articulation  = Articulation::staccato;
    context.lastKeyswitch = 25;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 1);

    // Asking for an articulation whose keyswitch is not the active one yields
    // nothing when fallback is disabled.
    context.articulation  = Articulation::staccato;
    context.lastKeyswitch = 24;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 0);
}

TEST_CASE ("a missing articulation falls back rather than going silent")
{
    Instrument instrument;
    instrument.regions.push_back (makeRegion (60, 72, 1, 127, Articulation::sustain));
    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note         = 64;
    context.articulation = Articulation::spiccato;   // the bank has no spiccato

    context.allowArticulationFallback = true;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 1);

    context.allowArticulationFallback = false;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 0);
}

TEST_CASE ("overlapping dynamic layers both sound, with complementary gains")
{
    Instrument instrument;

    auto soft = makeRegion (60, 60, 1, 127);
    soft.xfCcNumber = 1;
    soft.xfOutLoCc  = 0;
    soft.xfOutHiCc  = 127;

    auto loud = makeRegion (60, 60, 1, 127);
    loud.xfCcNumber = 1;
    loud.xfInLoCc   = 0;
    loud.xfInHiCc   = 127;

    instrument.regions.push_back (soft);
    instrument.regions.push_back (loud);
    instrument.buildIndex();

    std::array<int, kNumMidiCcs> ccValues {};
    ccValues[1] = 64;   // halfway up the dynamics control

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note     = 60;
    context.velocity = 100;
    context.ccValues = ccValues.data();

    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 2);

    // Equal-power crossfade: at the midpoint both layers sit near 1/sqrt(2) of
    // their own contribution, and their powers sum to roughly one.
    const float softGain = selected[0].gain;
    const float loudGain = selected[1].gain;

    CHECK (softGain > 0.1f);
    CHECK (loudGain > 0.1f);

    const float velocityGain = instrument.regions[0].velocityGain (100);
    const float softShare = softGain / velocityGain;
    const float loudShare = loudGain / velocityGain;

    CHECK (softShare * softShare + loudShare * loudShare == doctest::Approx (1.0f).epsilon (0.05));
}

TEST_CASE ("CC gating excludes regions outside the controller range")
{
    Instrument instrument;

    auto gated = makeRegion (60, 60, 1, 127);
    gated.ccNumber = 20;
    gated.loCc     = 64;
    gated.hiCc     = 127;

    instrument.regions.push_back (gated);
    instrument.buildIndex();

    std::array<int, kNumMidiCcs> ccValues {};

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note     = 60;
    context.ccValues = ccValues.data();

    ccValues[20] = 10;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 0);

    ccValues[20] = 90;
    CHECK (selectRegions (instrument, context, state, selected, 4) == 1);
}

TEST_CASE ("release-triggered regions do not fire on note-on")
{
    Instrument instrument;

    auto attack = makeRegion (60, 60, 1, 127);
    auto release = makeRegion (60, 60, 1, 127);
    release.trigger = TriggerMode::release;

    instrument.regions.push_back (attack);
    instrument.regions.push_back (release);
    instrument.buildIndex();

    SelectionState state;
    SelectedRegion selected[4];

    SelectionContext context;
    context.note    = 60;
    context.trigger = TriggerMode::attack;

    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 0);

    context.trigger = TriggerMode::release;
    REQUIRE (selectRegions (instrument, context, state, selected, 4) == 1);
    CHECK (selected[0].regionIndex == 1);
}

TEST_CASE ("the index covers every key in a region's range")
{
    Instrument instrument;
    instrument.regions.push_back (makeRegion (36, 96, 1, 127));
    instrument.buildIndex();

    CHECK (instrument.lowestKey == 36);
    CHECK (instrument.highestKey == 96);

    for (int note = 36; note <= 96; ++note)
        CHECK (instrument.candidatesForKey (note).size() == 1);

    CHECK (instrument.candidatesForKey (35).empty());
    CHECK (instrument.candidatesForKey (97).empty());
}
