/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The USB audio counters test app: see app.h */
static plat_usb_audio_stats_t st;                  /* what the app read last */
static int32_t reads;

void app_init(void)                                /* (not zero: a read must fill every field) */
{
    uint32_t i;
    for (i = 0; i < sizeof st; i++)
        ((uint8_t *)&st)[i] = 0xA5u;
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void)
{
    plat_usb_audio_stats(&st);
    reads++;
}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }
#ifdef PLAT_HOST                                   /* (the simulator's checks: not in the device build) */
int app_expect(const char *what, int *got)
{
    static const struct {
        const char *name;
        const uint32_t *v;
    } k[] = {{"app_in_packets", &st.in_packets},     {"app_in_underruns", &st.in_underruns},
             {"app_in_overruns", &st.in_overruns},   {"app_in_missed", &st.in_missed},
             {"app_in_stalls", &st.in_stalls},       {"app_out_packets", &st.out_packets},
             {"app_out_frames", &st.out_frames},     {"app_out_underruns", &st.out_underruns},
             {"app_out_overruns", &st.out_overruns}, {"app_out_feedback", &st.out_feedback}};
    uint32_t i;
    if (!strcmp(what, "app_reads"))
        *got = (int)reads;
    else if (!strcmp(what, "app_in_alt"))
        *got = st.in_alt;
    else if (!strcmp(what, "app_out_alt"))
        *got = st.out_alt;
    else {
        for (i = 0; i < sizeof k / sizeof k[0]; i++)
            if (!strcmp(what, k[i].name)) {
                *got = (int)*k[i].v;
                return 1;
            }
        return 0;
    }
    return 1;
}
#endif
