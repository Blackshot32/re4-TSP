// Linux/TSP frontend of the whole-program port: SDL2 window + GLES3, the core
// in its own thread, the app's GX renderer for the picture.
//
//   re4_host --play [--data DIR] [--modules DIR] [--cache DIR] [--scale N]
//
//   --scale 1: 0.5x EFB (320x224, the fastest), 2: the GameCube's 640x528
//   environment: RE4_FRONT_ASPECT=43|stretch|xfb, RE4_FRONT_POTATO=0..3,
//                RE4_FRONT_SCALE=1|2, RE4_FRONT_MERGE=1, RE4_FRONT_FXAA=1,
//                RE4_FRONT_VSYNC=0, RE4_FRONT_LOG=FILE
//
// Exit: START+SELECT held two seconds, or closing the window (Esc on a PC).
#include "re4_core.h"
#include "re4_gx_frame.h"
#include "gx_gl.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <sys/stat.h>

#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "gx_gl_prof.h"

extern "C" int re4_front_main(int argc, char** argv);

namespace {

bool gQuit;
FILE* gLog;
int gPotato;
bool gMerge, gFxaa;

double nowSec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int64_t nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void logf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (gLog) {
        vfprintf(gLog, fmt, ap);
        fputc('\n', gLog);
        fflush(gLog);
    }
    va_end(ap);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

bool envOn(const char* name)
{
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

int envInt(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v && *v ? atoi(v) : fallback;
}

// The game's sound, 32 kHz stereo, straight from the core.
void audioCallback(void*, Uint8* stream, int len)
{
    re4_core_audio(reinterpret_cast<int16_t*>(stream), len / 4);
}

struct Input {
    SDL_GameController* pad;
    bool startHeld, selectHeld;
    double quitAt;
};

void updatePad(Input& in, Re4CorePad& p)
{
    memset(&p, 0, sizeof p);
    if (in.pad) {
        SDL_GameController* c = in.pad;
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
        if (btn(SDL_CONTROLLER_BUTTON_A)) p.buttons |= 0x0100;
        if (btn(SDL_CONTROLLER_BUTTON_B)) p.buttons |= 0x0200;
        if (btn(SDL_CONTROLLER_BUTTON_X)) p.buttons |= 0x0400;
        if (btn(SDL_CONTROLLER_BUTTON_Y)) p.buttons |= 0x0800;
        if (btn(SDL_CONTROLLER_BUTTON_START)) p.buttons |= 0x1000;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) p.buttons |= 0x0010;  // Z (map)
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) p.buttons |= 0x0008;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) p.buttons |= 0x0004;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) p.buttons |= 0x0001;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p.buttons |= 0x0002;
        const int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        const int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        const int rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
        const int ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
        const int lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        const int rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        p.stickX = static_cast<int8_t>(lx * 100 / 32767);
        p.stickY = static_cast<int8_t>(-ly * 100 / 32767);
        p.substickX = static_cast<int8_t>(rx * 90 / 32767);
        p.substickY = static_cast<int8_t>(-ry * 90 / 32767);
        p.triggerL = static_cast<uint8_t>(lt * 255 / 32767);
        p.triggerR = static_cast<uint8_t>(rt * 255 / 32767);
        // the trigger's click presses the digital L/R (CoreInput: 0.85 / 0.70)
        if (p.triggerL > 217) p.buttons |= 0x0040;
        if (p.triggerR > 217) p.buttons |= 0x0020;
        const bool start = btn(SDL_CONTROLLER_BUTTON_START);
        const bool select = btn(SDL_CONTROLLER_BUTTON_BACK);
        if (start && select) {
            if (!in.quitAt) in.quitAt = nowSec();
            else if (nowSec() - in.quitAt > 2.0) gQuit = true;
        } else {
            in.quitAt = 0;
        }
    }
    // keyboard, for the PC check
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_UP]) p.buttons |= 0x0008;
    if (k[SDL_SCANCODE_DOWN]) p.buttons |= 0x0004;
    if (k[SDL_SCANCODE_LEFT]) p.buttons |= 0x0001;
    if (k[SDL_SCANCODE_RIGHT]) p.buttons |= 0x0002;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_Z]) p.buttons |= 0x0100;
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_X]) p.buttons |= 0x0200;
    if (k[SDL_SCANCODE_C]) p.buttons |= 0x0400;
    if (k[SDL_SCANCODE_V]) p.buttons |= 0x0800;
    if (k[SDL_SCANCODE_SPACE]) p.buttons |= 0x1000;
    if (k[SDL_SCANCODE_E]) p.buttons |= 0x0010;
    if (k[SDL_SCANCODE_A]) { p.stickX = -100; }
    if (k[SDL_SCANCODE_D]) { p.stickX = 100; }
    if (k[SDL_SCANCODE_W]) { p.stickY = 100; }
    if (k[SDL_SCANCODE_S]) { p.stickY = -100; }
    if (k[SDL_SCANCODE_ESCAPE]) gQuit = true;
}

const char* argValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

void presentFrame(re4gx::GxRenderer& gx, int W, int H, int aspect)
{
    unsigned w = 0, h = 0;
    if (!gx.Xfb(&w, &h) || !w || !h)
        return;
    int dw, dh;
    if (aspect == 1) {  // stretch
        dw = W; dh = H;
    } else if (aspect == 2) {  // the XFB's own aspect
        const double ar = (double) w / h;
        if ((double) W / H > ar) { dh = H; dw = (int) (H * ar + 0.5); }
        else { dw = W; dh = (int) (W / ar + 0.5); }
    } else {  // 4:3 box
        dw = H * 4 / 3; dh = H;
        if (dw > W) { dw = W; dh = W * 3 / 4; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    gx.Present((W - dw) / 2, (H - dh) / 2, dw, dh);
}

}  // namespace

extern "C" int re4_front_main(int argc, char** argv)
{
    const char* data = argValue(argc, argv, "--data");
    const char* modules = argValue(argc, argv, "--modules");
    const char* cache = argValue(argc, argv, "--cache");
    if (!data) data = getenv("RE4_DATA_DIR");
    if (!data) data = "/mnt/SDCARD/re4-data";
    if (!modules) modules = getenv("RE4_MODULE_DIR");
    if (!modules) modules = "./modules";
    if (!cache) cache = getenv("RE4_PROGRAM_CACHE");
    if (!cache) cache = "./shaders";

    int halves = 1;
    if (const char* s = argValue(argc, argv, "--scale")) halves = atoi(s);
    else halves = envInt("RE4_FRONT_SCALE", 1);
    if (halves < 1) halves = 1;
    if (halves > 4) halves = 4;
    int aspect = 0;
    if (const char* a = getenv("RE4_FRONT_ASPECT")) {
        if (!strcmp(a, "stretch")) aspect = 1;
        else if (!strcmp(a, "xfb")) aspect = 2;
    }
    gPotato = envInt("RE4_FRONT_POTATO", 3);
    gMerge = envOn("RE4_FRONT_MERGE");
    gFxaa = envOn("RE4_FRONT_FXAA");
    if (const char* path = getenv("RE4_FRONT_LOG")) gLog = fopen(path, "w");

    logf("front: data=%s modules=%s cache=%s halves=%d potato=%d", data, modules, cache, halves, gPotato);

    // MEM1 and the disc first, before any GL work (re4_core.h).
    if (!re4_core_reserve(data, modules)) {
        logf("front: the core refused to start: %s", re4_core_message());
        return 1;
    }
    re4_core_set_fast_cores(1);
    re4_core_set_potato(gPotato);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        logf("front: SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_Window* win = SDL_CreateWindow("Resident Evil 4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!win)
        win = SDL_CreateWindow("Resident Evil 4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                               SDL_WINDOW_OPENGL);
    if (!win) {
        logf("front: SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        logf("front: SDL_GL_CreateContext: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);
    if (envInt("RE4_FRONT_VSYNC", 1))
        SDL_GL_SetSwapInterval(1);
    int W = 1280, H = 720;
    SDL_GL_GetDrawableSize(win, &W, &H);
    logf("front: GL %s | %s | %dx%d", glGetString(GL_VERSION), glGetString(GL_RENDERER), W, H);
    gxdiag::Extensions();
    gxprof::Calibrate();

    re4gx::GxRenderer gx;
    re4gx::GxOptions opt;
    opt.samples = 1;
    opt.anisotropy = 1.0f;
    // experiments, off by default: the persistent vertex ring (RE4_FRONT_VRING) and
    // glInvalidateFramebuffer on the copy targets (RE4_FRONT_INVALIDATE)
    opt.vertexRing = envOn("RE4_FRONT_VRING");
    opt.invalidate = envOn("RE4_FRONT_INVALIDATE");
    opt.presentSharp = envOn("RE4_FRONT_SHARP");
    gx.SetOptions(opt);
    std::string err;
    if (!gx.InitHalves(halves, &err)) {
        logf("front: renderer: %s", err.c_str());
        return 1;
    }
    mkdir(cache, 0777);
    if (!gx.SetProgramCache(cache))
        logf("front: no program cache (the driver keeps no binaries)");
    gx.SetMerge(gMerge);
    gx.SetRecording(false);
    gx.SetFxaa(gFxaa);
    gx.SetTvFilter(false);
    gx.SetDofStrong(false);
    gx.SetFlares(false);
    gx.SetShownRows(0, 0);
    logf("front: renderer %dx%d (halves %d), merge %d, fxaa %d", (int) (640 * halves / 2), (int) (528 * halves / 2),
         halves, (int) gMerge, (int) gFxaa);

    SDL_AudioSpec want;
    memset(&want, 0, sizeof want);
    want.freq = 32000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audioCallback;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);
    else
        logf("front: no sound: %s", SDL_GetError());

    Input in{};
    in.pad = SDL_GameControllerOpen(0);
    if (in.pad)
        logf("front: pad: %s", SDL_GameControllerName(in.pad));
    else
        logf("front: no game controller (%s); keyboard only", SDL_GetError());

    if (!re4_core_start()) {
        logf("front: the core did not start: %s", re4_core_message());
        return 1;
    }
    logf("front: core started");

    double last = nowSec(), lastLog = last, nextVsync = last;
    unsigned frames = 0, chunks = 0, skipped = 0, runs = 0;
    double runMs = 0, presentMs = 0, swapMs = 0, worstRun = 0;
    re4gx::GxStats lastStats{};
    int room = -1;
    unsigned snapshotEvery = 0;
    // Opt-in identical-chunk A/B. Hold the acquired storage and pause the producer;
    // no gameplay changes occur without this explicit profiling switch.
    const bool profileReplay = envOn("RE4_FRONT_PROFILE_REPLAY");
    const bool profileAB = envOn("RE4_FRONT_PROFILE_AB");
    const char* profileSkipAB = getenv("RE4_FRONT_PROFILE_SKIP_AB");
    const char* originalSkipPass = gxprof::skipPassList;
    const double profileStart = nowSec();
    const Re4GxChunk* replayChunk = nullptr;
    double replayStart = 0;
    int replayPhase = -1;

    while (!gQuit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                gQuit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !in.pad)
                in.pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        Re4CorePad p;
        updatePad(in, p);
        re4_core_pad(&p);

        if (re4_core_state() != RE4_CORE_RUNNING && re4_core_state() != RE4_CORE_ENDED) {
            logf("front: the core failed: %s", re4_core_message());
            break;
        }

        // the room for the program prewarm, once in a while (the snapshot is large)
        if ((snapshotEvery++ & 31) == 0) {
            if (const Re4CoreSnapshot* s = re4_core_acquire()) {
                room = s->room;
                re4_core_release(s);
            }
        }
        if (room >= 0)
            gx.SetRoom((unsigned) room);

        const Re4GxChunk* list[4];
        int n = 0;
        if (replayChunk) {
            list[n++] = replayChunk;
            const int phase = int((nowSec() - replayStart) / 10.0);
            if ((profileAB || profileSkipAB) && phase != replayPhase) {
                replayPhase = phase;
                if (profileAB) gxprof::noDraw = (phase & 1) != 0;
                if (profileSkipAB) gxprof::skipPassList = (phase & 1) ? profileSkipAB : originalSkipPass;
                logf("profile-replay: phase=%d nodraw=%d skip=%s sequence=%u", phase, int(gxprof::noDraw),
                     gxprof::skipPassList ? gxprof::skipPassList : "none", replayChunk->sequence);
            }
        } else {
            while (n < 4 && (list[n] = re4_core_gx_acquire()) != nullptr)
                n++;
            if (profileReplay && room == 0x100 && nowSec() - profileStart >= 45.0 && n > 0 &&
                (list[n-1]->flags & RE4_GX_CHUNK_XFB) && !(list[n-1]->flags & RE4_GX_CHUNK_RESET)) {
                replayChunk = list[n-1];
                replayStart = nowSec();
                re4_core_pause(1, 100);
                logf("profile-replay: captured sequence=%u draws=%u vertices=%u blob=%u flags=%x; core paused",
                     replayChunk->sequence, replayChunk->draws, replayChunk->vertexBytes,
                     replayChunk->blobBytes, replayChunk->flags);
            }
        }
        if (n == 0) {
            if (re4_core_state() == RE4_CORE_ENDED)
                break;
            gx.Prewarm(10.0);
            SDL_Delay(1);
        } else {
            const double t0 = nowSec();
            for (int i = 0; i < n; i++) {
                if (i + 1 < n && gx.CanSkip(*list[i], *list[i + 1])) {
                    gx.Skip(*list[i]);
                    skipped++;
                } else {
                    gx.Run(*list[i]);
                    runs++;
                }
                if (list[i] != replayChunk) re4_core_gx_release(list[i]);
            }
            const double t1 = nowSec();
            const double ms = (t1 - t0) * 1000.0;
            runMs += ms;
            if (ms > worstRun) worstRun = ms;
            chunks += n;
            presentFrame(gx, W, H, aspect);
            const double t2 = nowSec();
            presentMs += (t2 - t1) * 1000.0;
            gxprof::Invoke("SDL_GL_SwapWindow", SDL_GL_SwapWindow, win);
            const double t3 = nowSec();
            swapMs += (t3 - t2) * 1000.0;
            frames++;
        }

        // the display's vsync (the game's VI locks to a display near 60 Hz)
        re4_core_vsync(nowNs());

        // at most 60 presents a second, even when the swap does not block
        nextVsync += 1.0 / 60.0;
        const double d = nextVsync - nowSec();
        if (d > 0.001) SDL_Delay((Uint32) (d * 1000.0));
        else if (d < -0.5) nextVsync = nowSec();

        const double t = nowSec();
        if (t - lastLog >= 5.0) {
            const double secs = t - lastLog;
            const re4gx::GxStats& st = gx.Stats();
            const unsigned f = frames ? frames : 1;
            logf("front: %.1f fps, chunks %u, runs %u, skipped %u, room r%x | run %.1f present %.1f swap %.1f ms | "
                 "draws +%u same +%u uniform +%u batches +%u taken +%u | uni +%u progs +%u tex +%u state +%u attr +%u",
                 frames / secs, chunks, runs, skipped, room < 0 ? 0 : room,
                 runMs / f, presentMs / f, swapMs / f,
                 st.draws - lastStats.draws, st.mergeSame - lastStats.mergeSame,
                 st.mergeUniform - lastStats.mergeUniform, st.mergeBatches - lastStats.mergeBatches,
                 st.mergeTaken - lastStats.mergeTaken,
                 st.uniformUploads - lastStats.uniformUploads, st.programSwitches - lastStats.programSwitches,
                 st.textureBinds - lastStats.textureBinds, st.stateChanges - lastStats.stateChanges,
                 st.attributeSetups - lastStats.attributeSetups);
            gxprof::Report(frames, st.draws - lastStats.draws, st.mergeBatches - lastStats.mergeBatches);
            gxdiag::Report(gxprof::enabled);
            lastStats = st;
            frames = chunks = skipped = runs = 0;
            runMs = presentMs = swapMs = worstRun = 0;
            lastLog = t;
            last = t;
        }
    }

    logf("front: exit");
    if (replayChunk) re4_core_gx_release(replayChunk);
    re4_core_gx_detach();
    gx.Release();
    if (audio)
        SDL_CloseAudioDevice(audio);
    if (in.pad)
        SDL_GameControllerClose(in.pad);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (gLog)
        fclose(gLog);
    return 0;
}
