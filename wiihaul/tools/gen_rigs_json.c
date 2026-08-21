/*
 * One-off generator: prints config/rigs.json from the compiled-in
 * default rosters in source/truck.c, so the shipped file starts out
 * byte-for-byte consistent with what main.c falls back to when there is
 * no SD card to read JSON from. Not part of the game or the test suite.
 *
 * Usage: gcc -std=c99 -Isource tools/gen_rigs_json.c source/truck.c \
 *            -lm -o /tmp/gen && /tmp/gen > config/rigs.json
 *
 * After running it, hand-check the diff — this is a starting point for
 * a human-editable file, not something to re-run blindly over an
 * edited config/rigs.json.
 */
#include <stdio.h>
#include "truck.h"

static const char *trailer_type_json(int type)
{
    switch (type) {
    case TRAILER_BOX:     return "box";
    case TRAILER_FLATBED: return "flatbed";
    case TRAILER_TANKER:  return "tanker";
    case TRAILER_LOWBOY:  return "lowboy";
    case TRAILER_PUP:     return "pup";
    default:              return "box";
    }
}

int main(void)
{
    int i, g;

    truck_specs_reset_defaults();

    printf("{\n  \"trucks\": [\n");
    for (i = 0; i < truck_spec_count; i++) {
        const TruckSpec *t = &truck_specs[i];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", t->name);
        printf("      \"mass_kg\": %.6g,\n", t->mass_kg);
        printf("      \"power_hp\": %.6g,\n", t->power_hp);
        printf("      \"wheelbase\": %.6g,\n", t->wheelbase);
        printf("      \"body_length\": %.6g,\n", t->body_length);
        printf("      \"body_width\": %.6g,\n", t->body_width);
        printf("      \"hitch_setback\": %.6g,\n", t->hitch_setback);
        printf("      \"max_steer_deg\": %.6g,\n", t->max_steer_deg);
        printf("      \"reverse_top_mps\": %.6g,\n", t->reverse_top_mps);
        printf("      \"brake_decel_ref\": %.6g,\n", t->brake_decel_ref);
        printf("      \"ref_mass_kg\": %.6g,\n", t->ref_mass_kg);
        printf("      \"cd_a\": %.6g,\n", t->cd_a);
        printf("      \"nominal_rpm\": %.6g,\n", t->nominal_rpm);
        printf("      \"gear_top\": [");
        for (g = 0; g < t->n_gears; g++)
            printf("%s%.8g", g ? ", " : "", t->gear_top[g]);
        printf("]\n");
        printf("    }%s\n", (i + 1 < truck_spec_count) ? "," : "");
    }
    printf("  ],\n");

    printf("  \"trailers\": [\n");
    for (i = 0; i < trailer_spec_count; i++) {
        const TrailerSpec *t = &trailer_specs[i];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", t->name);
        printf("      \"type\": \"%s\",\n", trailer_type_json(t->type));
        printf("      \"length\": %.6g,\n", t->length);
        printf("      \"hitch_offset\": %.6g,\n", t->hitch_offset);
        printf("      \"width\": %.6g,\n", t->width);
        printf("      \"height\": %.6g,\n", t->height);
        printf("      \"empty_mass_kg\": %.6g,\n", t->empty_mass_kg);
        printf("      \"max_cargo_kg\": %.6g,\n", t->max_cargo_kg);
        printf("      \"jackknife_limit_deg\": %.6g,\n", t->jackknife_limit_deg);
        printf("      \"slosh_strength\": %.6g\n", t->slosh_strength);
        printf("    }%s\n", (i + 1 < trailer_spec_count) ? "," : "");
    }
    printf("  ],\n");

    printf("  \"rigs\": [\n");
    for (i = 0; i < rig_spec_count; i++) {
        const RigSpec *r = &rig_specs[i];
        int j;
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", r->name);
        printf("      \"truck\": \"%s\",\n", truck_specs[r->truck_idx].name);
        printf("      \"trailers\": [");
        for (j = 0; j < r->n_trailers; j++)
            printf("%s\"%s\"", j ? ", " : "",
                  trailer_specs[r->trailer_idx[j]].name);
        printf("],\n");
        printf("      \"cargo_mass_kg\": %.6g,\n", r->cargo_mass_kg);
        printf("      \"difficulty_stars\": %d\n", r->difficulty_stars);
        printf("    }%s\n", (i + 1 < rig_spec_count) ? "," : "");
    }
    printf("  ]\n");
    printf("}\n");
    return 0;
}
