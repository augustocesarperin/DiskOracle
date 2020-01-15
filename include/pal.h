#ifndef PAL_H
#define PAL_H

// Platform Abstraction Layer interface

int pal_list_drives();
int pal_get_smart(const char *device);

#endif // PAL_H
