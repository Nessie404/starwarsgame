/*
 * Host-side tests for WiiHaul's JSON config loader.
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_config.c source/truck.c source/config.c \
 *       -lm -o wiihaul-test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "truck.h"
#include "config.h"

static int failures = 0;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
        }                                                       \
    } while (0)

static const char *SAMPLE_RIGS =
"{"
"  \"trucks\": [ {"
"    \"name\": \"TESTCAB\", \"mass_kg\": 8000, \"power_hp\": 400,"
"    \"wheelbase\": 4.0, \"body_length\": 6.4, \"body_width\": 2.5,"
"    \"hitch_setback\": 0.9, \"max_steer_deg\": 36, \"reverse_top_mps\": 3.4,"
"    \"brake_decel_ref\": 4.4, \"ref_mass_kg\": 15000, \"cd_a\": 6.2,"
"    \"nominal_rpm\": 0.5,"
"    \"gear_top\": [2.2, 2.9, 3.9, 5.2, 6.9, 9.2, 12.2, 16.3, 21.7, 28.85]"
"  } ],"
"  \"trailers\": [ {"
"    \"name\": \"TESTVAN\", \"type\": \"box\", \"length\": 12.4,"
"    \"hitch_offset\": 0, \"width\": 2.6, \"height\": 4.0,"
"    \"empty_mass_kg\": 6800, \"max_cargo_kg\": 20000,"
"    \"jackknife_limit_deg\": 90, \"slosh_strength\": 0"
"  } ],"
"  \"rigs\": [ {"
"    \"name\": \"TESTRIG\", \"truck\": \"TESTCAB\","
"    \"trailers\": [\"TESTVAN\"], \"cargo_mass_kg\": 9000,"
"    \"difficulty_stars\": 2"
"  } ]"
"}";

static void test_load_rigs(void)
{
    char err[128] = "";
    int ok;

    truck_specs_reset_defaults();
    ok = config_load_rigs_text(SAMPLE_RIGS, err, sizeof(err));
    CHECK(ok, "valid rigs.json should load (%s)", err);
    CHECK(truck_spec_count == 1, "should have exactly 1 truck (%d)",
         truck_spec_count);
    CHECK(trailer_spec_count == 1, "should have exactly 1 trailer (%d)",
         trailer_spec_count);
    CHECK(rig_spec_count == 1, "should have exactly 1 rig (%d)",
         rig_spec_count);
    CHECK(strcmp(truck_specs[0].name, "TESTCAB") == 0,
         "truck name should round-trip (got '%s')", truck_specs[0].name);
    CHECK(fabsf(truck_specs[0].power_hp - 400.0f) < 0.01f,
         "power_hp should round-trip (got %.2f)", truck_specs[0].power_hp);
    CHECK(truck_specs[0].n_gears == 10, "should read all 10 gears (got %d)",
         truck_specs[0].n_gears);
    CHECK(trailer_specs[0].type == TRAILER_BOX,
         "trailer type 'box' should map to TRAILER_BOX (got %d)",
         trailer_specs[0].type);
    CHECK(rig_specs[0].truck_idx == 0, "rig should resolve its truck ref");
    CHECK(rig_specs[0].n_trailers == 1 && rig_specs[0].trailer_idx[0] == 0,
         "rig should resolve its trailer ref");
    CHECK(fabsf(rig_specs[0].cargo_mass_kg - 9000.0f) < 0.01f,
         "cargo_mass_kg should round-trip");
}

static void test_reject_bad_rigs(void)
{
    char err[128] = "";
    int ok;

    truck_specs_reset_defaults();
    ok = config_load_rigs_text("{ not json", err, sizeof(err));
    CHECK(!ok, "malformed JSON should be rejected");
    CHECK(err[0] != '\0', "a rejection should carry an error message");

    ok = config_load_rigs_text(
        "{\"trucks\":[],\"trailers\":[],\"rigs\":[]}", err, sizeof(err));
    CHECK(!ok, "empty rosters should be rejected, not silently accepted");
}

static void test_load_settings(void)
{
    HaulSettings s;
    char err[128];
    int ok;

    haul_settings_defaults(&s);
    ok = config_load_settings_text(&s,
        "{ \"steer_rate_on_dps\": 111.0, \"jackknife_warn_frac\": 0.6 }",
        err, sizeof(err));
    CHECK(ok, "a partial settings document should still load (%s)", err);
    CHECK(fabsf(s.steer_rate_on_dps - 111.0f) < 0.01f,
         "an overridden field should take the new value");
    CHECK(fabsf(s.jackknife_warn_frac - 0.6f) < 0.01f,
         "a second overridden field should also take");
    CHECK(s.cam_height_m > 0.0f,
         "a field absent from the document should keep its default "
         "rather than becoming zero (cam_height_m=%.2f)", s.cam_height_m);
}

static void test_load_controls(void)
{
    ControlConfig c;
    char err[128];
    int ok;

    control_config_defaults(&c);
    ok = config_load_controls_text(&c,
        "{ \"show_input_overlay\": false, \"actions\": {"
        "  \"accel\": { \"keyboard\": [\"W\", \"UP\"], "
        "               \"gamecube\": [\"a\"], \"xbox_label\": \"RT\" }"
        "} }", err, sizeof(err));
    CHECK(ok, "a valid controls.json should load (%s)", err);
    CHECK(c.show_input_overlay == 0, "show_input_overlay should override");
    CHECK(c.keyboard[CONTROL_ACCEL][0] == 'W',
         "accel's first keyboard bind should be W");
    CHECK(c.keyboard[CONTROL_ACCEL][1] == GAME_KEY_UP,
         "accel's second keyboard bind should be the UP arrow");
    CHECK(c.gamecube[CONTROL_ACCEL] == GC_INPUT_A,
         "accel's gamecube bind should be A");
    CHECK(strcmp(c.xbox_label[CONTROL_ACCEL], "RT") == 0,
         "accel's xbox label should override (got '%s')",
         c.xbox_label[CONTROL_ACCEL]);
    /* an action untouched by the document keeps its compiled default */
    CHECK(c.keyboard[CONTROL_BRAKE][0] == GAME_KEY_SPACE,
         "an action absent from the document should keep its default");
}

/*
 * Parity tests: the shipped config/ JSON files must actually match
 * what main.c falls back to when there is no SD card to read them from
 * (the compiled-in defaults). WiiKart's HANDOFF.md calls this out from
 * hard experience — a roster that drifts between "the JSON" and "the
 * compiled fallback" builds clean, passes any test that only reads one
 * side, and is still wrong for whichever path a test didn't check. Run
 * from the wiihaul/ directory (tools/run-host-tests.sh cds there).
 */
static void test_rigs_json_matches_compiled_defaults(void)
{
    TruckSpec compiled_trucks[MAX_TRUCK_SPECS];
    TrailerSpec compiled_trailers[MAX_TRAILER_SPECS];
    RigSpec compiled_rigs[MAX_RIG_SPECS];
    int n_trucks, n_trailers, n_rigs;
    char err[256] = "";

    truck_specs_reset_defaults();
    n_trucks = truck_spec_count; n_trailers = trailer_spec_count;
    n_rigs = rig_spec_count;
    memcpy(compiled_trucks, truck_specs, sizeof(compiled_trucks));
    memcpy(compiled_trailers, trailer_specs, sizeof(compiled_trailers));
    memcpy(compiled_rigs, rig_specs, sizeof(compiled_rigs));

    CHECK(config_load_rigs_file("config/rigs.json", err, sizeof(err)),
         "config/rigs.json should load cleanly (%s)", err);

    CHECK(truck_spec_count == n_trucks, "truck count should match "
         "(json=%d compiled=%d)", truck_spec_count, n_trucks);
    CHECK(trailer_spec_count == n_trailers, "trailer count should match "
         "(json=%d compiled=%d)", trailer_spec_count, n_trailers);
    CHECK(rig_spec_count == n_rigs, "rig count should match "
         "(json=%d compiled=%d)", rig_spec_count, n_rigs);

    CHECK(memcmp(truck_specs, compiled_trucks,
                sizeof(TruckSpec) * (size_t)n_trucks) == 0,
         "every truck field should match its compiled default exactly");
    CHECK(memcmp(trailer_specs, compiled_trailers,
                sizeof(TrailerSpec) * (size_t)n_trailers) == 0,
         "every trailer field should match its compiled default exactly");
    CHECK(memcmp(rig_specs, compiled_rigs,
                sizeof(RigSpec) * (size_t)n_rigs) == 0,
         "every rig field should match its compiled default exactly");
}

static void test_settings_json_matches_compiled_defaults(void)
{
    HaulSettings compiled, loaded;
    char err[256] = "";

    haul_settings_defaults(&compiled);
    loaded = compiled;
    CHECK(config_load_settings_file(&loaded, "config/settings.json", err,
                                    sizeof(err)),
         "config/settings.json should load cleanly (%s)", err);
    CHECK(memcmp(&loaded, &compiled, sizeof(HaulSettings)) == 0,
         "settings.json should match haul_settings_defaults() exactly");
}

static void test_controls_json_matches_compiled_defaults(void)
{
    ControlConfig compiled, loaded;
    char err[256] = "";

    control_config_defaults(&compiled);
    loaded = compiled;
    CHECK(config_load_controls_file(&loaded, "config/controls.json", err,
                                    sizeof(err)),
         "config/controls.json should load cleanly (%s)", err);
    CHECK(memcmp(&loaded, &compiled, sizeof(ControlConfig)) == 0,
         "controls.json should match control_config_defaults() exactly");
}

int main(void)
{
    test_load_rigs();
    test_reject_bad_rigs();
    test_load_settings();
    test_load_controls();
    test_rigs_json_matches_compiled_defaults();
    test_settings_json_matches_compiled_defaults();
    test_controls_json_matches_compiled_defaults();

    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
