// dr_mp3.h / dr_flac.h are header-only with their implementation guarded by
// these macros - exactly one translation unit must define them before
// including, which is what this file exists to do.

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
