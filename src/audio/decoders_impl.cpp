// The single translation unit that instantiates the vendored decoders.
//
// Kept apart from our own code so their warnings never drown ours, and so the
// implementations exist exactly once in the binary.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
