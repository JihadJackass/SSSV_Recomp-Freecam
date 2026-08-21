// SSSV Free Camera - native companion library.
//
// The recompiled (MIPS) mod code cannot talk to SDL, so this tiny library is
// loaded by the recomp runtime from the mod's manifest (native_libraries) and
// exposes one function to the mod: SSSVFreeCam_PollMouse.
//
// It resolves SDL2 symbols out of the already-running SSSVRecompiled process
// (the game links SDL2 dynamically on all platforms), reads the global mouse
// state, and reports per-poll deltas plus the button mask and window focus.
//
// Build (from the native/ directory):
//   Linux:   gcc  -shared -fPIC -O2 -o sssv_freecam_native.so  sssv_freecam_native.c
//   macOS:   clang -dynamiclib -O2 -o sssv_freecam_native.dylib sssv_freecam_native.c
//   Windows (MSVC dev prompt):
//            cl /LD /O2 sssv_freecam_native.c /Fe:sssv_freecam_native.dll
//   Windows (mingw):
//            gcc -shared -O2 -o sssv_freecam_native.dll sssv_freecam_native.c
//
// The built library goes in the same folder as the .nrm mod file.

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #define EXPORT __declspec(dllexport)
#else
    #include <dlfcn.h>
    #define EXPORT __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------------------
// Minimal recomp runtime ABI (mirrors N64Recomp/include/recomp.h).
// The runtime checks this exported variable and requires the value 1.
// ---------------------------------------------------------------------------
EXPORT uint32_t recomp_api_version = 1;

typedef uint64_t gpr;

typedef struct {
    gpr r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
        r8,  r9,  r10, r11, r12, r13, r14, r15,
        r16, r17, r18, r19, r20, r21, r22, r23,
        r24, r25, r26, r27, r28, r29, r30, r31;
    // Floats/hi/lo/etc. follow in the real struct; we only read r4 (a0) and
    // never touch anything past the GPRs, so this prefix is sufficient and
    // layout-compatible.
} recomp_context;

// Word store into emulated RDRAM at a given N64 virtual address.
#define MEM_W(offset, reg) \
    (*(int32_t*)(rdram + ((((reg) + (offset))) - 0xFFFFFFFF80000000ull)))

// ---------------------------------------------------------------------------
// Lazy SDL2 symbol resolution from the host process.
// ---------------------------------------------------------------------------
typedef uint32_t (*PFN_SDL_GetGlobalMouseState)(int* x, int* y);
typedef void*    (*PFN_SDL_GetKeyboardFocus)(void);

static PFN_SDL_GetGlobalMouseState p_SDL_GetGlobalMouseState = NULL;
static PFN_SDL_GetKeyboardFocus    p_SDL_GetKeyboardFocus    = NULL;
static int g_resolved = 0;
static int g_resolve_failed = 0;

static void resolve_sdl(void) {
    if (g_resolved || g_resolve_failed) {
        return;
    }
#ifdef _WIN32
    HMODULE sdl = GetModuleHandleA("SDL2.dll");
    if (!sdl) {
        // Some builds ship SDL2 with a versioned or renamed DLL; try the
        // process itself as a fallback in case SDL was linked statically
        // with exported symbols.
        sdl = GetModuleHandleA(NULL);
    }
    if (sdl) {
        p_SDL_GetGlobalMouseState =
            (PFN_SDL_GetGlobalMouseState)GetProcAddress(sdl, "SDL_GetGlobalMouseState");
        p_SDL_GetKeyboardFocus =
            (PFN_SDL_GetKeyboardFocus)GetProcAddress(sdl, "SDL_GetKeyboardFocus");
    }
#else
    p_SDL_GetGlobalMouseState =
        (PFN_SDL_GetGlobalMouseState)dlsym(RTLD_DEFAULT, "SDL_GetGlobalMouseState");
    p_SDL_GetKeyboardFocus =
        (PFN_SDL_GetKeyboardFocus)dlsym(RTLD_DEFAULT, "SDL_GetKeyboardFocus");
#endif
    if (p_SDL_GetGlobalMouseState) {
        g_resolved = 1;
    } else {
        g_resolve_failed = 1;
    }
}

// ---------------------------------------------------------------------------
// The export called by the recompiled mod code.
// a0 (ctx->r4) = pointer (N64 address) to s32 out[4]:
//   out[0] = mouse dx since last poll (counts)
//   out[1] = mouse dy since last poll (counts)
//   out[2] = SDL mouse button mask (bit 1 = middle button)
//   out[3] = 1 if the game window has focus, else 0
// ---------------------------------------------------------------------------
EXPORT void SSSVFreeCam_PollMouse(uint8_t* rdram, recomp_context* ctx) {
    static int have_prev = 0;
    static int prev_x = 0, prev_y = 0;

    gpr out = ctx->r4;
    int x = 0, y = 0;
    uint32_t buttons = 0;
    int focused = 0;
    int dx = 0, dy = 0;

    resolve_sdl();

    if (g_resolved) {
        buttons = p_SDL_GetGlobalMouseState(&x, &y);
        if (have_prev) {
            dx = x - prev_x;
            dy = y - prev_y;
        }
        prev_x = x;
        prev_y = y;
        have_prev = 1;

        if (p_SDL_GetKeyboardFocus) {
            focused = (p_SDL_GetKeyboardFocus() != NULL) ? 1 : 0;
        } else {
            focused = 1; // can't tell; assume focused
        }
    }

    MEM_W(0x0, out) = dx;
    MEM_W(0x4, out) = dy;
    MEM_W(0x8, out) = (int32_t)buttons;
    MEM_W(0xC, out) = focused;
}
