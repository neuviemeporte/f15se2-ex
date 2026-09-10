#ifndef F15_MENU_POINTER_H
#define F15_MENU_POINTER_H

/* Return the original keyboard action, Enter on pointer confirmation, or zero
 * for a pointer release that only changes selection (or misses the menu). */
int menu_pilotPointerInput(int action, int *pending);
int menu_missionPointerInput(int action, int *selection, int *pending);

#endif
