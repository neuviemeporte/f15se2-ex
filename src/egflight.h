#ifndef F15_SE2_EGFLIGHT
#define F15_SE2_EGFLIGHT
/* public interface of egflight.c */

int16 isqrt(int16 value);
void computeTrackingCameraAngles(int32 targetX, int32 targetY, int16 targetAlt,
                                 int32 viewX, int32 viewY, int16 viewAlt,
                                 int16 *heading, int16 *pitch);
void UpdateThrottleState(void);
void drawFuelGauge(void);

#endif /* F15_SE2_EGFLIGHT */
