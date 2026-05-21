/*
 * portable/probe.h - drop-in probe registry for the comparison harness.
 *
 * A probe is a named function returning a uint32_t checksum of its result.
 * REGISTER_PROBE() drops a descriptor into the `probes` linker section; the
 * shared runner (probe_runner.c) walks that section, so adding a new probe is
 * just dropping a .c file into portable/probes/ (or portable/hazards/) - no
 * central table to edit, and both target legs pick it up automatically.
 *
 * The linker scripts KEEP(*(probes)) so --gc-sections can't drop the section.
 */
#ifndef PORTABLE_PROBE_H
#define PORTABLE_PROBE_H

#include <stdint.h>
#include <stddef.h>

struct probe { const char *name; uint32_t (*fn)(void); };

#define REGISTER_PROBE(displayname, func)                                    \
    static const struct probe __probe_##func                                 \
        __attribute__((section("probes"), used)) = { (displayname), (func) }

/* FNV-1a over raw result bytes - the standard checksum primitive for probes. */
static inline uint32_t probe_fnv1a(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* Provided by the platform glue; runs every registered probe. */
void portable_run(void (*emit_char)(char));

#endif /* PORTABLE_PROBE_H */
