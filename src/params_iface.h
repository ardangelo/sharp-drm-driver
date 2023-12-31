#ifndef PARAMS_IFACE_H_
#define PARAMS_IFACE_H_

extern int g_param_mono_cutoff;
extern int g_param_mono_invert;
extern int g_param_overlays;

int params_probe(void);
void params_remove(void);

void params_set_mono_invert(int setting);

#endif
