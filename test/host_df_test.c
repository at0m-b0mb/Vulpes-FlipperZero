/* Host tests for the Vulpes direction-finding engine.
 *
 * The radio hands the app a single number and every claim it makes is this
 * engine's reading of a stream of them. None of that is visible in a
 * screenshot, so it is all checked here, on the host, on every push.
 *
 *   make -C test
 */
#include "../helpers/vul_df.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...)                              \
    do {                                              \
        checks++;                                     \
        if(!(cond)) {                                 \
            failures++;                               \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                      \
            printf("\n");                             \
        }                                             \
    } while(0)

#define EQ(a, b) CHECK((a) == (b), "expected %ld, got %ld", (long)(b), (long)(a))
#define NEAR(a, b, tol) \
    CHECK(labs((long)(a) - (long)(b)) <= (tol), "expected %ld +/- %ld, got %ld", (long)(b), (long)(tol), (long)(a))

static void section(const char* name) {
    printf("%s\n", name);
}

/* ------------------------------------------------------------------ */

static void test_percentile(void) {
    section("percentile");

    int16_t v[8] = {5, 1, 9, 3, 7, 2, 8, 4};
    EQ(vul_percentile(v, 8, 0), 1); /* min */
    EQ(vul_percentile(v, 8, 7), 9); /* max */
    EQ(vul_percentile(v, 8, 2), 3);
    EQ(vul_percentile(v, 8, 4), 5);

    /* k past the end clamps to the max rather than reading off the array. */
    EQ(vul_percentile(v, 8, 200), 9);

    int16_t one[1] = {-42};
    EQ(vul_percentile(one, 1, 0), -42);

    EQ(vul_percentile(NULL, 4, 0), VUL_DBM_INVALID);
    EQ(vul_percentile(v, 0, 0), VUL_DBM_INVALID);

    /* The input must not be disturbed - callers reuse their buffers. */
    int16_t before[8];
    memcpy(before, v, sizeof(v));
    (void)vul_percentile(v, 8, 3);
    EQ(memcmp(before, v, sizeof(v)), 0);

    /* Negatives, which is all the engine ever really sees. */
    int16_t dbm[5] = {-100, -60, -95, -102, -80};
    EQ(vul_percentile(dbm, 5, 0), -102);
    EQ(vul_percentile(dbm, 5, 4), -60);
    EQ(vul_percentile(dbm, 5, 2), -95);
}

static void test_floor(void) {
    section("noise floor");

    VulFloor f;
    vul_floor_init(&f, VUL_DBM_INVALID);
    CHECK(!f.seeded, "unseeded floor should not claim to be seeded");

    /* First real sample seeds it outright. */
    vul_floor_push(&f, -90);
    CHECK(f.seeded, "first push should seed");
    EQ(f.floor, -90);

    /* Snaps straight down to any new minimum. */
    vul_floor_push(&f, -104);
    EQ(f.floor, -104);

    /* A loud sample must not drag the floor up with it. */
    vul_floor_push(&f, -20);
    EQ(f.floor, -104);

    /* It creeps up only after a long stretch of samples near the floor, and
     * then only 1 dB at a time. */
    for(int i = 0; i < VUL_FLOOR_RISE_PUSHES - 1; i++) vul_floor_push(&f, -100);
    EQ(f.floor, -104);
    vul_floor_push(&f, -100);
    EQ(f.floor, -103);

    /* THE regression this rule exists for. A continuous carrier well above the
     * floor must not drag the floor up to meet it: if it did, the margin would
     * decay to nothing and the app would read COLD while sitting on top of the
     * transmitter it had just found. */
    vul_floor_init(&f, -100);
    for(int i = 0; i < VUL_FLOOR_RISE_PUSHES * 20; i++) vul_floor_push(&f, -60);
    EQ(f.floor, -100);

    /* Just inside the near window still counts as evidence about the noise. */
    vul_floor_init(&f, -100);
    for(int i = 0; i < VUL_FLOOR_RISE_PUSHES; i++) {
        vul_floor_push(&f, (int16_t)(-100 + VUL_FLOOR_NEAR_DB));
    }
    EQ(f.floor, -99);

    /* Just outside it does not. */
    vul_floor_init(&f, -100);
    for(int i = 0; i < VUL_FLOOR_RISE_PUSHES; i++) {
        vul_floor_push(&f, (int16_t)(-100 + VUL_FLOOR_NEAR_DB + 1));
    }
    EQ(f.floor, -100);

    /* Drift must never overtake the live reading: pushing a sample that is
     * already at the floor cannot push the floor above it. */
    vul_floor_init(&f, -80);
    for(int i = 0; i < VUL_FLOOR_RISE_PUSHES * 4; i++) vul_floor_push(&f, -80);
    EQ(f.floor, -80);

    /* Seeding from the survey's band floor is honoured. */
    vul_floor_init(&f, -98);
    CHECK(f.seeded, "explicit seed should count as seeded");
    EQ(f.floor, -98);

    /* Invalid samples are ignored, not treated as a very quiet reading. */
    vul_floor_push(&f, VUL_DBM_INVALID);
    EQ(f.floor, -98);
}

static void test_track(void) {
    section("sample track");

    VulTrack t;
    vul_track_init(&t);
    EQ(t.count, 0);
    EQ(vul_track_at(&t, 0), VUL_DBM_INVALID);

    for(int i = 0; i < 5; i++) vul_track_push(&t, (int16_t)(-100 + i));
    EQ(t.count, 5);
    EQ(vul_track_at(&t, 0), -96); /* newest */
    EQ(vul_track_at(&t, 4), -100); /* oldest */
    EQ(vul_track_at(&t, 5), VUL_DBM_INVALID);

    /* Wrap: count saturates and the oldest values fall off the back. */
    vul_track_init(&t);
    for(int i = 0; i < VUL_TRACK_LEN * 3; i++) vul_track_push(&t, (int16_t)(i & 0x7F));
    EQ(t.count, VUL_TRACK_LEN);
    EQ(vul_track_at(&t, 0), (VUL_TRACK_LEN * 3 - 1) & 0x7F);
    EQ(vul_track_at(&t, VUL_TRACK_LEN - 1), (VUL_TRACK_LEN * 2) & 0x7F);
}

static void test_level(void) {
    section("level estimate");

    VulTrack t;
    VulFloor f;
    VulLevel l;

    /* Too little history is reported as such rather than guessed at. */
    vul_track_init(&t);
    vul_floor_init(&f, -100);
    for(int i = 0; i < 4; i++) vul_track_push(&t, -50);
    vul_level(&t, &f, &l);
    CHECK(!l.valid, "4 samples should not produce a valid level");

    /* Steady carrier. */
    vul_track_init(&t);
    for(int i = 0; i < VUL_TRACK_LEN; i++) vul_track_push(&t, -60);
    vul_level(&t, &f, &l);
    CHECK(l.valid, "a full track should be valid");
    EQ(l.signal, -60);
    EQ(l.floor, -100);
    EQ(l.margin, 40);
    EQ(l.spread, 0);

    /* THE case this design exists for: a transmitter keyed 25% of the time.
     * The upper percentile reports the level it actually transmits at. A mean
     * would report -87 dBm, which is a level nothing ever emitted, and the
     * hunt would be chasing a number that does not correspond to anything. */
    vul_track_init(&t);
    for(int i = 0; i < VUL_TRACK_LEN; i++) vul_track_push(&t, (i % 4 == 0) ? -50 : -100);
    vul_level(&t, &f, &l);
    EQ(l.signal, -50);
    EQ(l.margin, 50);
    CHECK(l.spread > 0, "a bursty window should report a spread");

    /* One spike of interference must not define the reading, which is exactly
     * what a plain max-hold would let it do. */
    vul_track_init(&t);
    for(int i = 0; i < VUL_TRACK_LEN; i++) vul_track_push(&t, -80);
    vul_track_push(&t, -10); /* single outlier */
    vul_level(&t, &f, &l);
    EQ(l.signal, -80);

    /* Margin is clamped at zero: a signal below the tracked floor is "nothing
     * here", never a negative distance. */
    vul_track_init(&t);
    vul_floor_init(&f, -40);
    for(int i = 0; i < VUL_TRACK_LEN; i++) vul_track_push(&t, -90);
    vul_level(&t, &f, &l);
    EQ(l.margin, 0);

    /* With no seeded floor it falls back to the window's lower quartile. */
    vul_track_init(&t);
    vul_floor_init(&f, VUL_DBM_INVALID);
    for(int i = 0; i < VUL_TRACK_LEN; i++) vul_track_push(&t, (i % 4 == 0) ? -50 : -100);
    vul_level(&t, &f, &l);
    EQ(l.floor, -100);
}

static void test_trend(void) {
    section("trend");

    VulTrendInfo tr;
    int16_t m[VUL_TREND_LEN];

    /* Not enough points is Unknown, not a guess. */
    int16_t few[3] = {1, 2, 3};
    vul_trend(few, 3, &tr);
    EQ(tr.trend, VulTrendUnknown);
    vul_trend(NULL, 16, &tr);
    EQ(tr.trend, VulTrendUnknown);

    /* Clean ramp of 1 dB per track sample at 25 Hz = 25 dB/s. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = (int16_t)i;
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendWarmer);
    EQ(tr.slope_x10, 250);
    EQ(tr.resid_x10, 0); /* a perfect line has no residual */
    EQ(tr.confidence, 100);

    /* The same ramp downhill. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = (int16_t)(VUL_TREND_LEN - i);
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendColder);
    EQ(tr.slope_x10, -250);

    /* Dead flat. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = 30;
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendFlat);
    EQ(tr.slope_x10, 0);
    EQ(tr.confidence, 100);

    /* A crawl of 1 dB across the whole 0.64 s window is under the base
     * deadband and must not be sold as progress. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = (int16_t)(20 + i / 15);
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendFlat);

    /* THE property that separates this from a two-sample delta: a signal
     * thrashing +/-8 dB with no underlying drift must report Flat. A naive
     * "is it higher than last time" reads the last two samples of this and
     * says "warmer", every time, at random. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = (int16_t)(40 + ((i % 2) ? 8 : -8));
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendFlat);
    CHECK(tr.resid_x10 > 50, "alternating +/-8 dB should show a big residual, got %d", tr.resid_x10);
    CHECK(
        tr.deadband_x10 > VUL_TREND_DEAD_X10,
        "noise should widen the deadband, got %d",
        tr.deadband_x10);

    /* ...but a real climb buried in that same noise still gets through, so the
     * widened deadband is not just deafness. */
    for(int i = 0; i < VUL_TREND_LEN; i++) m[i] = (int16_t)(20 + 3 * i + ((i % 2) ? 8 : -8));
    vul_trend(m, VUL_TREND_LEN, &tr);
    EQ(tr.trend, VulTrendWarmer);

    /* Only the newest window is fitted: a long history that fell and then rose
     * reads as rising. */
    int16_t longer[40];
    for(int i = 0; i < 24; i++) longer[i] = (int16_t)(80 - 3 * i);
    for(int i = 24; i < 40; i++) longer[i] = (int16_t)(8 + 3 * (i - 24));
    vul_trend(longer, 40, &tr);
    EQ(tr.trend, VulTrendWarmer);
}

static void test_heat(void) {
    section("heat ladder");

    EQ(vul_heat(0), VulHeatCold);
    EQ(vul_heat(VUL_HEAT_COOL_DB - 1), VulHeatCold);
    EQ(vul_heat(VUL_HEAT_COOL_DB), VulHeatCool);
    EQ(vul_heat(VUL_HEAT_WARM_DB - 1), VulHeatCool);
    EQ(vul_heat(VUL_HEAT_WARM_DB), VulHeatWarm);
    EQ(vul_heat(VUL_HEAT_HOT_DB - 1), VulHeatWarm);
    EQ(vul_heat(VUL_HEAT_HOT_DB), VulHeatHot);
    EQ(vul_heat(VUL_HEAT_BURNING_DB - 1), VulHeatHot);
    EQ(vul_heat(VUL_HEAT_BURNING_DB), VulHeatBurning);
    EQ(vul_heat(VUL_HEAT_ONTOP_DB - 1), VulHeatBurning);
    EQ(vul_heat(VUL_HEAT_ONTOP_DB), VulHeatOnTop);
    EQ(vul_heat(200), VulHeatOnTop);

    /* The ladder must be monotonic across its whole range. */
    for(int16_t d = 0; d < 120; d++) {
        CHECK(vul_heat(d) >= vul_heat((int16_t)(d - 1)), "heat went backwards at %d dB", d);
    }

    /* Every rung has a name and they are all distinct. */
    for(int i = 0; i < VulHeatCount; i++) {
        CHECK(vul_heat_name((VulHeat)i) != NULL, "heat %d has no name", i);
        for(int j = i + 1; j < VulHeatCount; j++) {
            CHECK(
                strcmp(vul_heat_name((VulHeat)i), vul_heat_name((VulHeat)j)) != 0,
                "heat names %d and %d collide",
                i,
                j);
        }
    }
}

static void test_halvings(void) {
    section("distance proxy");

    EQ(vul_halvings_x10(40, 40), 0);
    EQ(vul_halvings_x10(46, 40), 10); /* 6 dB = one halving */
    EQ(vul_halvings_x10(52, 40), 20);
    EQ(vul_halvings_x10(34, 40), -10); /* backwards = further away */
    EQ(vul_halvings_x10(43, 40), 5); /* 3 dB = half a halving */

    /* ...and the same thing said as a distance ratio, which is what the screen
     * actually shows. 12 dB gained = 2 halvings = 4x closer. */
    EQ(vul_range_ratio_x10(0), 10); /* 1.0x: where you started */
    EQ(vul_range_ratio_x10(10), 20); /* one halving  = 2.0x */
    EQ(vul_range_ratio_x10(20), 40); /* two halvings = 4.0x */
    EQ(vul_range_ratio_x10(30), 80);
    NEAR(vul_range_ratio_x10(18), 35, 1); /* 2^1.8 = 3.48 */
    NEAR(vul_range_ratio_x10(5), 14, 1); /* 2^0.5 = 1.41 */

    /* Going backwards reads as a fraction, not a negative. */
    EQ(vul_range_ratio_x10(-10), 5); /* 0.5x: twice as far */
    EQ(vul_range_ratio_x10(-20), 3); /* 0.25x, rounded to one decimal */

    /* Monotonic, and clamped rather than overflowing at the extremes. */
    for(int16_t h = -80; h < 80; h++) {
        CHECK(
            vul_range_ratio_x10((int16_t)(h + 1)) >= vul_range_ratio_x10(h),
            "range ratio went backwards at %d",
            h);
    }
    CHECK(vul_range_ratio_x10(32767) <= 9999, "ratio must stay printable");
    CHECK(vul_range_ratio_x10(-32768) >= 1, "ratio must never reach zero");
}

static void test_burst(void) {
    section("burst classification");

    VulBurst b;
    VulSigInfo s;
    const int16_t FLOOR = -100;

    /* Not enough data yet: says so instead of guessing. */
    vul_burst_init(&b);
    for(int i = 0; i < 100; i++) vul_burst_push(&b, -60, FLOOR);
    vul_burst_result(&b, &s);
    EQ(s.kind, VulSigWaiting);

    /* An empty channel. */
    vul_burst_init(&b);
    for(int i = 0; i < 1000; i++) vul_burst_push(&b, -100, FLOOR);
    vul_burst_result(&b, &s);
    EQ(s.kind, VulSigNone);
    EQ(s.duty_pct, 0);

    /* A carrier that never stops: analog bug, video transmitter, jammer. */
    vul_burst_init(&b);
    for(int i = 0; i < 1000; i++) vul_burst_push(&b, -60, FLOOR);
    vul_burst_result(&b, &s);
    EQ(s.kind, VulSigContinuous);
    EQ(s.duty_pct, 100);
    EQ(s.bursts, 1); /* one rising edge, not a thousand */

    /* A tracker beaconing once a second: 40 ms on, 960 ms off. */
    vul_burst_init(&b);
    for(int cycle = 0; cycle < 10; cycle++) {
        for(int i = 0; i < 20; i++) vul_burst_push(&b, -70, FLOOR);
        for(int i = 0; i < 480; i++) vul_burst_push(&b, -100, FLOOR);
    }
    vul_burst_result(&b, &s);
    EQ(s.kind, VulSigPeriodic);
    EQ(s.bursts, 10);
    EQ(s.duty_pct, 4);
    EQ(s.period_ms, 1000);
    EQ(s.regularity, 100);

    /* Ragged human-driven traffic: a remote, a sensor, a doorbell. */
    vul_burst_init(&b);
    const int gaps[8] = {80, 700, 120, 950, 60, 830, 200, 640};
    for(int cycle = 0; cycle < 8; cycle++) {
        for(int i = 0; i < 20; i++) vul_burst_push(&b, -70, FLOOR);
        for(int i = 0; i < gaps[cycle]; i++) vul_burst_push(&b, -100, FLOOR);
    }
    vul_burst_result(&b, &s);
    EQ(s.kind, VulSigIntermittent);
    CHECK(s.regularity < VUL_PERIODIC_MIN_REGULARITY, "irregular gaps scored %u", s.regularity);

    /* The Schmitt trigger earns its keep: a signal sitting between the two
     * thresholds and dithering must not be counted as thousands of bursts. */
    vul_burst_init(&b);
    for(int i = 0; i < 1000; i++) {
        vul_burst_push(&b, (int16_t)(FLOOR + VUL_BURST_ON_DB + ((i % 2) ? 0 : -2)), FLOOR);
    }
    vul_burst_result(&b, &s);
    EQ(s.bursts, 1);
    EQ(s.kind, VulSigContinuous);

    /* Every kind has a name and a field hint. */
    const VulSigKind kinds[] = {
        VulSigNone, VulSigContinuous, VulSigPeriodic, VulSigIntermittent, VulSigWaiting};
    for(size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        CHECK(vul_sig_name(kinds[i]) != NULL, "kind %d has no name", kinds[i]);
        CHECK(vul_sig_hint(kinds[i]) != NULL, "kind %d has no hint", kinds[i]);
    }
}

static void test_rose(void) {
    section("bearing rose");

    VulRose r;
    VulRoseResult res;

    /* Nothing collected yet. */
    vul_rose_init(&r);
    vul_rose_result(&r, &res);
    CHECK(!res.conclusive, "an empty rose cannot be conclusive");

    /* A clear peak in one sector, everything else flat. */
    vul_rose_init(&r);
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) vul_rose_add(&r, i, 10);
    vul_rose_add(&r, 3, 30);
    vul_rose_result(&r, &res);
    EQ(res.sector, 3);
    EQ(res.heading_deg, 90);
    EQ(res.best, 30);
    EQ(res.median, 10);
    EQ(res.contrast, 20);
    EQ(res.visited, VUL_ROSE_SECTORS);
    CHECK(res.conclusive, "a 20 dB peak over 12 sectors should be conclusive");

    /* Each sector keeps its maximum, not its last reading. */
    vul_rose_init(&r);
    vul_rose_add(&r, 5, 40);
    vul_rose_add(&r, 5, 12);
    EQ(r.best[5], 40);
    EQ(r.visited, 1);

    /* A flat rose is the honest answer to "which way": no direction. This is
     * what an omni whip in an open field actually produces. */
    vul_rose_init(&r);
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) vul_rose_add(&r, i, 25);
    vul_rose_result(&r, &res);
    EQ(res.contrast, 0);
    CHECK(!res.conclusive, "a flat rose must not name a direction");

    /* Sharp contrast but half the circle unwalked is still not an answer. */
    vul_rose_init(&r);
    vul_rose_add(&r, 0, 10);
    vul_rose_add(&r, 1, 10);
    vul_rose_add(&r, 2, 60);
    vul_rose_result(&r, &res);
    CHECK(res.contrast >= VUL_ROSE_MIN_CONTRAST, "contrast should be large");
    CHECK(!res.conclusive, "an incomplete turn must not be conclusive");

    /* Just under the contrast threshold with a full turn: still refuses. */
    vul_rose_init(&r);
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) vul_rose_add(&r, i, 20);
    vul_rose_add(&r, 7, (int16_t)(20 + VUL_ROSE_MIN_CONTRAST - 1));
    vul_rose_result(&r, &res);
    CHECK(!res.conclusive, "contrast below threshold must not be conclusive");
    vul_rose_add(&r, 7, (int16_t)(20 + VUL_ROSE_MIN_CONTRAST));
    vul_rose_result(&r, &res);
    CHECK(res.conclusive, "contrast at threshold should be conclusive");

    /* Out-of-range sectors are dropped, not wrapped into a wrong bearing. */
    vul_rose_init(&r);
    vul_rose_add(&r, VUL_ROSE_SECTORS, 99);
    vul_rose_add(&r, 200, 99);
    EQ(r.visited, 0);

    /* Headings cover the circle exactly once. */
    for(uint8_t i = 0; i < VUL_ROSE_SECTORS; i++) {
        vul_rose_init(&r);
        vul_rose_add(&r, i, 10);
        vul_rose_result(&r, &res);
        EQ(res.heading_deg, i * 30);
    }
}

static void test_atten(void) {
    section("offset attenuation");

    /* Pegged: back off, unless already at the last step. */
    CHECK(vul_should_attenuate(-20, 0), "a pegged reading should attenuate");
    CHECK(vul_should_attenuate(-20, VUL_ATTEN_STEPS - 2), "should attenuate one step from the top");
    CHECK(
        !vul_should_attenuate(-20, VUL_ATTEN_STEPS - 1),
        "must not attenuate past the last step");
    CHECK(!vul_should_attenuate(VUL_ATTEN_PEG_DBM, 0), "at the threshold is not over it");
    CHECK(!vul_should_attenuate(-90, 0), "a weak signal should not attenuate");
    CHECK(!vul_should_attenuate(VUL_DBM_INVALID, 0), "no reading, no decision");

    /* Backing out again as the signal drops. */
    CHECK(vul_should_relax(-90, 1), "a faded signal should relax");
    CHECK(!vul_should_relax(-90, 0), "cannot relax below step 0");
    CHECK(!vul_should_relax(-40, 2), "a strong signal should stay attenuated");
    CHECK(!vul_should_relax(VUL_DBM_INVALID, 2), "no reading, no decision");

    /* The two must never both fire, or the step would oscillate every sample. */
    for(int16_t d = -110; d <= -5; d++) {
        for(uint8_t st = 0; st < VUL_ATTEN_STEPS; st++) {
            CHECK(
                !(vul_should_attenuate(d, st) && vul_should_relax(d, st)),
                "attenuate and relax both fired at %d dBm step %u",
                d,
                st);
        }
    }

    /* Step 0 must be on frequency, and the ladder must be increasing. */
    EQ(vul_atten_khz[0], 0);
    for(int i = 1; i < VUL_ATTEN_STEPS; i++) {
        CHECK(vul_atten_khz[i] > vul_atten_khz[i - 1], "attenuation ladder is not increasing");
    }
}

static void test_survey(void) {
    section("survey candidates");

    int16_t bins[VUL_SURVEY_BINS];
    VulCandidate cand[VUL_MAX_CANDIDATES];

    /* An empty band: the floor is the noise and nothing stands above it. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    EQ(vul_band_floor(bins, VUL_SURVEY_BINS), -100);
    EQ(vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES), 0);

    /* Two real signals, one too weak to chase. The lower quartile is immune to
     * the loud bins, which is the whole reason it is a quartile. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    bins[10] = -70; /* 30 dB over */
    bins[11] = -75; /* the same carrier, smeared by the RX filter */
    bins[30] = -80; /* 20 dB over */
    bins[50] = -95; /* 5 dB over: under the threshold */
    EQ(vul_band_floor(bins, VUL_SURVEY_BINS), -100);

    uint8_t n = vul_find_candidates(
        bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, 2);
    EQ(cand[0].bin, 10); /* strongest first */
    EQ(cand[0].dbm, -70);
    EQ(cand[0].margin, 30);
    EQ(cand[1].bin, 30);
    EQ(cand[1].margin, 20);

    /* Guard suppression: two equally loud adjacent bins are one transmitter. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    bins[20] = -60;
    bins[21] = -60;
    n = vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, 1);

    /* Peaks exactly VUL_CANDIDATE_GUARD apart still merge; one further apart
     * is reported separately. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    bins[20] = -60;
    bins[20 + VUL_CANDIDATE_GUARD] = -65;
    n = vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, 1);

    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    bins[20] = -60;
    bins[20 + VUL_CANDIDATE_GUARD + 1] = -65;
    n = vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, 2);

    /* The output list is capped and stays sorted strongest-first. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = -100;
    for(int k = 0; k < 10; k++) bins[k * 6] = (int16_t)(-60 - k); /* descending */
    n = vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, VUL_MAX_CANDIDATES);
    for(uint8_t i = 1; i < n; i++) {
        CHECK(cand[i].dbm <= cand[i - 1].dbm, "candidates out of order at %u", i);
    }

    /* Bins the radio refused to tune are skipped, not read as very quiet. */
    for(int i = 0; i < VUL_SURVEY_BINS; i++) bins[i] = VUL_DBM_INVALID;
    for(int i = 0; i < 20; i++) bins[i] = -100;
    bins[5] = -70;
    EQ(vul_band_floor(bins, VUL_SURVEY_BINS), -100);
    n = vul_find_candidates(bins, VUL_SURVEY_BINS, -100, VUL_CANDIDATE_MIN_DB, cand, VUL_MAX_CANDIDATES);
    EQ(n, 1);
    EQ(cand[0].bin, 5);

    /* Degenerate inputs. */
    EQ(vul_band_floor(NULL, 8), VUL_DBM_INVALID);
    EQ(vul_band_floor(bins, 0), VUL_DBM_INVALID);
    EQ(vul_find_candidates(NULL, 8, -100, 10, cand, 4), 0);
    EQ(vul_find_candidates(bins, 8, -100, 10, NULL, 4), 0);
    EQ(vul_find_candidates(bins, 8, -100, 10, cand, 0), 0);
    EQ(vul_find_candidates(bins, 8, VUL_DBM_INVALID, 10, cand, 4), 0);
}

/* An end-to-end walk: a bug at a fixed spot, the operator closing in. Free
 * space is 6 dB per halving of distance, so this is what the engine should see
 * on a real approach. */
static void test_approach(void) {
    section("simulated approach");

    VulTrack t;
    VulFloor f;
    VulLevel l;
    VulTrendInfo tr;
    int16_t margins[VUL_TREND_LEN];
    uint8_t mn = 0;

    vul_track_init(&t);
    vul_floor_init(&f, -100);

    /* Start at -85 dBm and gain 15 dB over the approach. */
    int16_t first_margin = 0, last_margin = 0;
    for(int step = 0; step < 64; step++) {
        int16_t dbm = (int16_t)(-85 + (step * 15) / 64);
        vul_track_push(&t, dbm);
        vul_floor_push(&f, dbm);
        vul_level(&t, &f, &l);
        if(l.valid) {
            if(mn < VUL_TREND_LEN) {
                margins[mn++] = l.margin;
            } else {
                memmove(margins, margins + 1, sizeof(int16_t) * (VUL_TREND_LEN - 1));
                margins[VUL_TREND_LEN - 1] = l.margin;
            }
            if(first_margin == 0) first_margin = l.margin;
            last_margin = l.margin;
        }
    }

    CHECK(last_margin > first_margin, "closing in should raise the margin");
    vul_trend(margins, mn, &tr);
    EQ(tr.trend, VulTrendWarmer);

    /* The floor must have held at its seed rather than climbing with the
     * signal - if it followed the signal up, the margin would never grow and
     * the whole hunt would read COLD while standing on the transmitter. */
    EQ(f.floor, -100);
    EQ(vul_heat(last_margin), VulHeatHot);

    /* And the distance proxy should say roughly one halving per 6 dB gained. */
    NEAR(vul_halvings_x10(last_margin, first_margin), (last_margin - first_margin) * 10 / 6, 1);
}

int main(void) {
    printf("Vulpes direction-finding engine\n\n");

    test_percentile();
    test_floor();
    test_track();
    test_level();
    test_trend();
    test_heat();
    test_halvings();
    test_burst();
    test_rose();
    test_atten();
    test_survey();
    test_approach();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
