#pragma once

// Opt-in CPU wall-clock measurements. No GPU queries, flushes or readbacks added.
// Include after GL/platform headers. All state belongs to the context's render thread.
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <utility>
#include <type_traits>
#include <map>
#include <string>
#include <vector>
#include <set>

#include "gx_diag.h"
#include "gx_gpu_diag.h"

namespace gxprof {
inline bool Env(const char* key) {
  const char* p = std::getenv(key);
  return p && *p && std::strcmp(p, "0");
}
inline bool enabled = Env("RE4_GX_GLPROF");
inline bool noDraw = Env("RE4_GX_NODRAW");
inline const bool passLog = Env("RE4_GX_PASSES") || gxdiag::gpuOn;
inline const char* skipPassList = std::getenv("RE4_GX_SKIP_PASS");
inline const bool passOn = passLog || (skipPassList && *skipPassList) || gxdiag::gpuOn || gxdiag::finishAux;
inline std::uint64_t Now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return std::uint64_t(t.tv_sec) * 1000000000ull + t.tv_nsec;
}
struct Count { std::uint64_t ns = 0, calls = 0, bytes = 0, skipped = 0; };
inline std::map<std::string, Count> runCalls, otherCalls;
inline bool inRun = false;
inline std::uint64_t runs = 0, runNs = 0, fboSwitches = 0, efbCopies = 0;
inline GLuint drawFbo = ~0u, readFbo = ~0u;
inline unsigned activeUnit = 0;
inline GLuint tex2d[32] = {};
inline std::map<GLuint, std::pair<int, int>> texSize;
inline std::map<GLuint, GLuint> fboColor;
struct Pass {
  GLuint fbo = 0;
  unsigned draws = 0, skipped = 0;
  std::uint64_t ns = 0;
  std::uint64_t start = 0, cpuNs = 0;
  std::string end = "end", kind = "geometry";
  int width = 0, height = 0;
};
inline std::vector<Pass> passes;
inline std::string passKind = "geometry";
inline unsigned long long passFrame = 0;
inline void PassBegin() {
  Pass p; p.fbo = drawFbo; p.kind = passKind;
  p.start = Now();
  const auto size = texSize[fboColor[drawFbo]];
  p.width = drawFbo == 0 ? 1280 : size.first;
  p.height = drawFbo == 0 ? 720 : size.second;
  passes.push_back(p);
  gxgpu::PassBegin(unsigned(passes.size()-1),p.width,p.height,p.kind.c_str());
}
inline void PassEnd(const char* reason) {
  if (!passOn || !inRun || passes.empty()) return;
  passes.back().end = reason;
  passes.back().cpuNs = Now() - passes.back().start;
  gxgpu::PassEnd();
  PassBegin();
}
inline bool SkipPass() {
  if (!inRun || !skipPassList || !*skipPassList || passes.empty()) return false;
  const std::string list = std::string(",") + skipPassList + ",";
  return list.find("," + std::to_string(passes.size()-1) + ",") != std::string::npos ||
      list.find("," + passKind + ",") != std::string::npos;
}
inline void PassFinish() {
  if (!passOn || !inRun) return;
  ++passFrame;
  if(!passes.empty()) passes.back().cpuNs = Now() - passes.back().start;
  gxgpu::PassEnd();
  if (passLog) {
    std::string text;
    char line[320];
    for (std::size_t i = 0; i < passes.size(); ++i) {
      const Pass& p = passes[i];
      std::snprintf(line, sizeof(line), "glpass: frame=%llu pass=%zu fbo=%u target=%dx%d kind=%s draws=%u skipped=%u draw_ms=%.3f cpu_ms=%.3f end=%s\n",
          passFrame, i, p.fbo, p.width, p.height, p.kind.c_str(), p.draws, p.skipped, p.ns / 1e6, p.cpuNs/1e6, p.end.c_str());
      text += line;
    }
    std::fwrite(text.data(), 1, text.size(), stderr);
  }
}
struct CopyScope {
  explicit CopyScope(const char* kind) {
    if (passOn && inRun) { PassEnd(kind); passKind = kind; passes.back().kind = kind; }
  }
  ~CopyScope() {
    if (passOn && inRun) { passKind = "geometry"; if (!passes.empty()) passes.back().kind = passKind; }
  }
};
struct RunScope {
  std::uint64_t start = 0;
  explicit RunScope(bool skip) {
    inRun = !skip;
    if (enabled && !skip) { ++runs; start = Now(); }
    if (passOn && !skip) { gxgpu::FrameBegin(); passes.clear(); passKind = "geometry"; PassBegin(); }
  }
  ~RunScope() { PassFinish(); if (start) runNs += Now() - start; inRun = false; }
};
struct Timer {
  Count* count = nullptr;
  std::uint64_t start = 0;
  explicit Timer(const char* name) {
    if (enabled) {
      count = &(inRun ? runCalls : otherCalls)[name];
      ++count->calls;
    }
    if (enabled || (gxdiag::phasesOn && gxdiag::active)) { start = Now(); if(gxdiag::phasesOn) ++gxdiag::clockCalls; }
  }
  ~Timer() {
    if (!start) return;
    const auto ns=Now()-start;
    if(count) count->ns+=ns;
    if(gxdiag::phasesOn && gxdiag::active) {gxdiag::glNs+=ns;++gxdiag::clockCalls;}
  }
};
template<class... A> inline void Meta(const char*, A...) {}
inline void Meta(const char* name, GLenum target, GLuint fbo) {
  if ((!enabled && !passOn) || std::strcmp(name, "glBindFramebuffer")) return;
  if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
    if (inRun && drawFbo != fbo) {
      ++fboSwitches;
      PassEnd("fbo_switch");
      if (passOn && !passes.empty()) passes.back().fbo = fbo;
    }
    drawFbo = fbo;
  }
  if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) readFbo = fbo;
}
inline void Bytes(const char* name, std::uint64_t bytes) {
  if (enabled) (inRun ? runCalls : otherCalls)[name].bytes += bytes;
}
inline void Meta(const char* name, GLenum, GLsizeiptr size, const void* data, GLenum) {
  if (!std::strcmp(name, "glBufferData") && data) Bytes(name, size);
}
inline void Meta(const char* name, GLenum, GLintptr, GLsizeiptr size, const void*) {
  if (!std::strcmp(name, "glBufferSubData")) Bytes(name, size);
}
inline void Meta(const char* name, GLenum, GLintptr, GLsizeiptr size, GLbitfield flags) {
  if (!std::strcmp(name, "glMapBufferRange") && (flags & GL_MAP_WRITE_BIT)) Bytes(name, size);
}
inline void Meta(const char* name, GLenum, GLint, GLint, GLsizei w, GLsizei h,
                 GLint, GLenum format, GLenum type, const void* data) {
  if (std::strcmp(name, "glTexImage2D") || !data) return;
  unsigned pixel = type == GL_UNSIGNED_BYTE ? (format == GL_RGBA ? 4 : format == GL_RGB ? 3 :
                     format == GL_RG ? 2 : 1) : type == GL_FLOAT ? 4 : 2;
  Bytes(name, std::uint64_t(w) * h * pixel);
}
template<class F, class... A> inline auto Invoke(const char* name, F fn, A... args)
    -> decltype(fn(args...)) {
  if constexpr (std::is_void<decltype(fn(args...))>::value) {
    const bool draw = inRun && !std::strncmp(name, "glDraw", 6);
    if (draw && passOn && !passes.empty()) {
      auto& p = passes.back();
      ++p.draws;
      const auto size = texSize[fboColor[drawFbo]];
      p.width = drawFbo == 0 ? 1280 : size.first;
      p.height = drawFbo == 0 ? 720 : size.second;
      p.kind = passKind;
      gxgpu::Target(p.width,p.height,p.kind.c_str());
    }
    if (draw && (noDraw || SkipPass())) {
      if (passOn && !passes.empty()) ++passes.back().skipped;
      if (enabled) ++runCalls[name].skipped;
      return;
    }
    if (draw && passOn && !passes.empty()) {
      Timer timer(name);
      const auto start = Now();
      auto call = [&] { fn(args...); };
      if (!std::strcmp(name,"glDrawArrays") && (gxdiag::gpuOn || gxdiag::finishAux)) {
        const auto& p=passes.back();
        gxgpu::Aux(call,p.width,p.height,p.kind.c_str());
      } else call();
      passes.back().ns += Now() - start;
      return;
    }
  }
  if (!enabled && !(gxdiag::phasesOn && gxdiag::active)) return fn(args...);
  Meta(name, args...);
  Timer timer(name);
  return fn(args...);
}
inline void BindFramebuffer(GLenum target, GLuint fbo) {
  Meta("glBindFramebuffer", target, fbo);
  Timer timer("glBindFramebuffer");
  ::glBindFramebuffer(target, fbo);
}
inline void BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
  if (data) Bytes("glBufferData", size);
  Timer timer("glBufferData");
  ::glBufferData(target, size, data, usage);
}
inline void BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
  Bytes("glBufferSubData", size);
  Timer timer("glBufferSubData");
  ::glBufferSubData(target, offset, size, data);
}
inline void* MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr size, GLbitfield flags) {
  if (flags & GL_MAP_WRITE_BIT) Bytes("glMapBufferRange", size);
  Timer timer("glMapBufferRange");
  return ::glMapBufferRange(target, offset, size, flags);
}
inline void TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                       GLint border, GLenum format, GLenum type, const void* data) {
  Meta("glTexImage2D", target, level, internal, w, h, border, format, type, data);
  if (passOn && level == 0 && target == GL_TEXTURE_2D && activeUnit < 32) texSize[tex2d[activeUnit]] = {w, h};
  Timer timer("glTexImage2D");
  ::glTexImage2D(target, level, internal, w, h, border, format, type, data);
}
inline void ActiveTexture(GLenum unit) {
  if (passOn) activeUnit = unit - GL_TEXTURE0;
  Timer timer("glActiveTexture"); ::glActiveTexture(unit);
}
inline void BindTexture(GLenum target, GLuint tex) {
  if (passOn && target == GL_TEXTURE_2D && activeUnit < 32) tex2d[activeUnit] = tex;
  Timer timer("glBindTexture"); ::glBindTexture(target, tex);
}
inline void FramebufferTexture2D(GLenum target, GLenum attachment, GLenum texTarget, GLuint tex, GLint level) {
  if (passOn && attachment == GL_COLOR_ATTACHMENT0) fboColor[target == GL_READ_FRAMEBUFFER ? readFbo : drawFbo] = tex;
  Timer timer("glFramebufferTexture2D"); ::glFramebufferTexture2D(target, attachment, texTarget, tex, level);
}
inline const char* Category(const std::string& s) {
  if (s.find("glDraw") == 0) return "draw";
  if (s == "glBindTexture" || s == "glActiveTexture" || s == "glBindSampler") return "texture_bind";
  if (s.find("glTexImage") == 0 || s.find("glTexSubImage") == 0 || s.find("glCompressedTex") == 0) return "texture_upload";
  if (s == "glBufferData" || s == "glBufferSubData" || s == "glMapBufferRange" || s == "glUnmapBuffer") return "buffer_upload_map";
  if (s.find("glUniform") == 0 || s == "glBindBufferRange" || s == "glBindBufferBase") return "uniform_ubo";
  if (s == "glUseProgram") return "use_program";
  if (s == "glCompileShader" || s == "glLinkProgram" || s == "glGetUniformLocation" ||
      s == "glGetAttribLocation" || s == "glProgramBinary") return "shader_link_lookup";
  if (s == "glBindFramebuffer" || s.find("glCopyTex") == 0 || s == "glReadPixels" || s == "glClear" ||
      s == "glInvalidateFramebuffer" || s == "glBlitFramebuffer") return "fbo_copy_clear_read";
  if (s == "glGetError" || s == "glFlush" || s == "glFinish") return "error_flush_finish";
  if (s.find("Sync") != std::string::npos) return "sync";
  if (s.find("glGet") == 0 || s == "glCheckFramebufferStatus") return "queries";
  if (s.find("Attrib") != std::string::npos || s == "glBindVertexArray" || s == "glBindBuffer") return "buffers_vao_attrib";
  if (s == "SDL_GL_SwapWindow") return "swap_SDL_boundary";
  return "state_other";
}
inline void Calibrate() {
  if(!gxdiag::phasesOn) return;
  // CPU-only calibration: these are no-op C++ functions, NOT GL commands.
  // Preserve the real counters so calibration never appears as graphics work.
  const bool old=enabled;
  auto saved=std::move(otherCalls);
  const auto oldClocks=gxdiag::clockCalls;
  constexpr unsigned n=40000;
  const char* names[]={"glBindBufferRange","glActiveTexture","glBindTexture","glUseProgram",
    "glEnable","glDisable","glDepthMask","glUniform3ui","glViewport","glScissor","glBindSampler","glBindBuffer"};
  volatile unsigned sink=0;
  auto noop=[&](unsigned x) {sink=x;};
  double samples[2]={};
  for(int mode=0;mode<2;++mode) {
    enabled=mode;
    const auto start=Now();
    for(unsigned i=0;i<n;++i) Invoke(names[i%12],noop,i);
    samples[mode]=double(Now()-start)/n;
  }
  const auto start=Now();
  for(unsigned i=0;i<n;++i) {const auto t=Now();sink=unsigned(t);}
  const auto clock=double(Now()-start)/n;
  enabled=old;otherCalls=std::move(saved);gxdiag::clockCalls=oldClocks;
  std::fprintf(stderr,"profcal: noop_calls=%u wrapper_off_ns=%.3f wrapper_on_ns=%.3f delta_ns=%.3f clock_ns=%.3f; CPU-only microbenchmark, not a GPU/frame benchmark\n",
    n,samples[0],samples[1],samples[1]-samples[0],clock);
  (void)sink;
}
inline void Report(unsigned frames, unsigned draws, unsigned batches) {
  if (!enabled) return;
  std::fprintf(stderr, "glprof: frames=%u runs=%llu draws=%u batches=%u run_ms=%.3f fbo_per_run=%.2f efb_per_run=%.2f nodraw=%d\n",
      frames, (unsigned long long)runs, draws, batches, runNs / 1e6 / (runs ? runs : 1),
      double(fboSwitches) / (runs ? runs : 1), double(efbCopies) / (runs ? runs : 1), int(noDraw));
  for (int scope = 0; scope != 2; ++scope) {
    auto& calls = scope ? otherCalls : runCalls;
    std::map<std::string, Count> cats;
    for (const auto& p : calls) {
      auto& c = cats[Category(p.first)];
      c.ns += p.second.ns; c.calls += p.second.calls; c.bytes += p.second.bytes;
      std::fprintf(stderr, "glcall: %s %s ns=%llu calls=%llu bytes=%llu skipped=%llu\n", scope ? "other" : "run", p.first.c_str(),
          (unsigned long long)p.second.ns, (unsigned long long)p.second.calls, (unsigned long long)p.second.bytes,
          (unsigned long long)p.second.skipped);
    }
    for (const auto& p : cats)
      std::fprintf(stderr, "glcat: %s %s ms=%.3f calls=%llu bytes=%llu\n", scope ? "other" : "run", p.first.c_str(),
          p.second.ns / 1e6, (unsigned long long)p.second.calls, (unsigned long long)p.second.bytes);
    calls.clear();
  }
  runs = runNs = fboSwitches = efbCopies = 0;
  std::fflush(stderr);
}
} // namespace gxprof

// Generated wrappers cover every GL entry point used by gx_gl.cpp, including queries.
#include "gx_gl_prof_macros.h"
