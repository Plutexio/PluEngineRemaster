// The one translation unit holding the stb implementations, built as the shared library `stb`.
// Nothing else may define STB_IMAGE_IMPLEMENTATION / STB_IMAGE_WRITE_IMPLEMENTATION.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
