#ifndef F15_SE2_BLACKBOX_SNAPSHOT_H
#define F15_SE2_BLACKBOX_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Write a stable, machine-readable view of authoritative gameplay objects.
 * This is deliberately write-only: restoring these fields without rebuilding
 * derived caches and pointers would create invalid runtime state. */
int blackbox_snapshotWriteJson(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_SNAPSHOT_H */
