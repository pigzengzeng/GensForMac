#ifndef _MAIN_H_
#define _MAIN_H_

/* max controller input slots exposed by the core */
#define MAX_INPUTS 8

/* frontend globals referenced by the core / osd layer */
extern int debug_on;
extern int log_error;

/* per-frame input poll callback invoked by the core (osd_input_update) */
extern int sdl_input_update(void);

#endif /* _MAIN_H_ */
