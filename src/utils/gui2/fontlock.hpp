// One lock for all text rendering.
//
// A TTF_Font is not a read-only object: SDL_ttf keeps a mutable glyph cache
// inside it, so rendering the same font from two threads at once corrupts that
// cache. The engine dispatches each task sequence's phases to a shared worker
// pool, so the menu's captions and the match's can be rendered on different
// threads at the same time - and were.
//
// The symptoms were a TTF_RenderUTF8_Blended that returned null (a SEGV in
// Gui2Caption::Redraw, caught by AddressSanitizer while the formation panel was
// being filled in) and an invalid free inside TTF_CloseFont at exit, which is
// where a corrupted glyph cache is finally handed back to the allocator.
//
// Every TTF_* call that touches a shared font goes through this.

#ifndef _HPP_UTILS_GUI2_FONTLOCK
#define _HPP_UTILS_GUI2_FONTLOCK

#include <mutex>

namespace blunted {

std::mutex& FontMutex();

}  // namespace blunted

#endif
