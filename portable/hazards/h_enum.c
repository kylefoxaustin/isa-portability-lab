/*
 * portable/hazards/h_enum.c - enum-sizing portability hazard.
 *
 * The ARM bare-metal EABI defaults to -fshort-enums: an enum takes the
 * smallest integer type that holds its range (1, 2, or 4 bytes). RISC-V (like
 * most targets) makes enums int-sized (always 4). So sizeof(enum) - and the
 * size/layout of any struct containing one - diverges between the two targets.
 *
 * This is a real firmware footgun: register-description and protocol structs
 * lean on enums heavily, and a short-enum object linked against an int-enum
 * object silently mismatches. Expect this probe to report XX.
 */
#include <stdint.h>
#include "probe.h"

enum small  { S_A = 0, S_B = 200 };          /* range fits in 1 byte  */
enum medium { M_A = 0, M_B = 40000 };        /* needs 2 bytes         */
enum large  { L_A = 0, L_B = 0x12345678 };   /* needs 4 bytes         */

struct packetkind { char tag; enum small kind; };  /* layout depends on enum size */

static uint32_t h_enum_sizes(void)
{
    uint32_t f[4] = {
        (uint32_t)sizeof(enum small),        /* ARM: 1   RISC-V: 4 */
        (uint32_t)sizeof(enum medium),       /* ARM: 2   RISC-V: 4 */
        (uint32_t)sizeof(enum large),        /* ARM: 4   RISC-V: 4 */
        (uint32_t)sizeof(struct packetkind), /* differs via member enum size */
    };
    return probe_fnv1a(f, sizeof f);
}

REGISTER_PROBE("H06 enum_sizes  ", h_enum_sizes);
