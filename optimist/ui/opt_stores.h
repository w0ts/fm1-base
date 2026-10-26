/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The stores Optimist's UI calls (storage/project.c, storage/sections/sections.c, storage/upreset.c, storage/nbank.c,
 * drums/drum_kits.c, storage/snapshots, fx/fx_rec.c): stand-ins until the trial's storage step (Optimist's formats on
 * fm1-base's plat_data windows, docs/FM1-BASE-PORT-MAP.md "Flash"). Everything reads empty; what would write says so
 * on the screen (OPT_NO_STORE) and changes nothing. After the UI's files: they answer with its ui_message. */
#pragma once

#define OPT_NO_STORE "NO STORE YET"             /* (the header's message: what a save or a load says in the trial) */

/* projects (project.c) */
static int project_used(uint32_t slot) { (void)slot; return 0; }
static void project_save(uint32_t slot) { (void)slot; ui_message(OPT_NO_STORE); }
static void project_load(uint32_t slot) { (void)slot; ui_message(OPT_NO_STORE); }
static uint32_t proj_orph_uid(uint32_t k) { (void)k; return 0xFFu; }
static int proj_tmp_busy(void) { return 0; }
static void settings_save(void) {}              /* (the settings live in RAM until the next start) */
static void song_backup(void) {}
static void song_restore(void) {}
static void sl24_auto_import(void) {}           /* (SLOOP 2.4's projects: none to import) */
static void arrangement_save(void) { ui_message(OPT_NO_STORE); }
static uint32_t arrangement_ready(void) { return 0; }
static void arrangement_apply(uint32_t scene) { (void)scene; }
static int fxr_decode(const uint8_t *a, uint32_t n, int all) { (void)a, (void)n, (void)all; return 0; }

/* the sections: the scenes stored (sections.c) */
static void section_store(uint32_t s) { (void)s; ui_message(OPT_NO_STORE); }
static void section_load(uint32_t s) { (void)s; }
static int section_cue(uint32_t s) { (void)s; return 0; }
static uint32_t section_bars(uint32_t s) { (void)s; return 1; }
static void sec_mem(uint32_t *pct, uint32_t *more) { *pct = 0, *more = 0; }
static void sec_name(uint32_t s, char *b) { (void)s; b[0] = 0; }
static int sec_name_set(uint32_t s, const char *nm) { (void)s, (void)nm; ui_message(OPT_NO_STORE); return 1; }
static int sec_scene_clear(uint32_t s) { (void)s; return 0; }

/* the user presets (upreset.c): none */
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return -1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t slot) { return slot; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k)
{
    b[0] = 'U', b[1] = (char)('0' + (k + 1u) / 10u % 10u), b[2] = (char)('0' + (k + 1u) % 10u), b[3] = 0;
}
static void up_ui(uint32_t op, uint32_t k) { (void)op, (void)k; ui_message(OPT_NO_STORE); }
static uint32_t up_engine_slot(uint32_t k) { (void)k; return NENGINES; }
static int up_store(uint32_t k, const char *name) { (void)k, (void)name; ui_message(OPT_NO_STORE); return -1; }

/* the FM6 / CZ collections in PRESETS (nbank.c): none */
static uint32_t nb_kind(const track_t *t) { (void)t; return 0; }
static uint32_t nb_list(uint32_t *rank, uint32_t n, uint32_t *at) { (void)n, (void)at; *rank = 0; return 0; }
static uint32_t nb_list_of(uint32_t kind, uint32_t *rank, uint32_t n, uint32_t *at) { (void)kind, (void)n, (void)at; *rank = 0; return 0; }
static uint32_t nb_kind_at(uint32_t i) { (void)i; return 0; }
static uint32_t nb_engine_slot(uint32_t kind) { (void)kind; return NENGINES; }
static void nb_name(uint32_t kind, uint32_t k, char *nm) { (void)kind, (void)k; nm[0] = 0; }
static void nb_load(uint32_t k) { (void)k; }

/* the user drum kits (drum_kits.c): the bank empty */
static uint32_t ukit_count(void) { return 0; }
static int ukit_used(uint32_t u) { (void)u; return 0; }
static int ukit_load(uint32_t u) { (void)u; return -1; }
static uint32_t ukit_nth(uint32_t n) { return n; }
static uint32_t ukit_rank(uint32_t u) { return u; }
static void ukit_ui(uint32_t op, uint32_t u) { (void)op, (void)u; ui_message(OPT_NO_STORE); }

/* the snapshots (storage/snapshots): every slot empty */
static uint32_t sn_ui_n(void) { return FELUCCA_SNAPSHOTS + 1u; }
static void sn_ui_label(char *b, uint32_t i)
{
    b[0] = i < FELUCCA_SNAPSHOTS ? (char)('1' + i) : 'B';
    b[1] = 0;
}
static uint32_t sn_ui_row(uint32_t i, char *name, uint32_t *bytes) { (void)i; name[0] = 0; *bytes = 0; return 0; }
static void sn_ui(uint32_t op, uint32_t i) { (void)op, (void)i; ui_message(OPT_NO_STORE); }
