#ifndef F15_SE2_WORLDXFER
#define F15_SE2_WORLDXFER

/* World-data hand-off between the three former DOS EXEs (now one process).
 *
 * START generates the mission into its own typed globals; EGAME plays it in its
 * g_* globals; END debriefs from its own globals. The three keep separate typed
 * views of the same records (the plane table is even a deliberate +2-byte-shifted
 * reinterpretation — see worldxfer.c), so the hand-off is an explicit typed
 * conversion at each seam, replacing the former commData->worldBuf byte cursor. */

void worldImportToEgame(void); /* START/shared globals -> EGAME g_* (mission start) */
void worldExportToEnd(void);   /* EGAME g_* -> END globals (mission end)           */

#endif /* F15_SE2_WORLDXFER */
