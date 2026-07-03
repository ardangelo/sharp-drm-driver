#ifndef SHARP_MEMORY_H_
#define SHARP_MEMORY_H_

void sharp_memory_set_invert(int setting);
void* sharp_memory_add_overlay(int x, int y, int width, int height, unsigned char const* pixels);
void sharp_memory_remove_overlay(void* entry);
void* sharp_memory_show_overlay(void* storage);
void sharp_memory_hide_overlay(void* display);
void sharp_memory_clear_overlays(void);

#endif
