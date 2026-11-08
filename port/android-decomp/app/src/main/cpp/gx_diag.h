#pragma once

// Opt-in diagnostics only; no retained geometry, changed uploads or render state.
#include "re4_gx_frame.h"
#include <time.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace gxdiag {
inline bool On(const char* key) {
  const char* p = std::getenv(key);
  return p && *p && std::strcmp(p, "0");
}
inline const bool vtxOn = On("RE4_GX_VTXSTATS");
inline const bool totalsOn = On("RE4_GX_PHASES");
inline const bool phasesOn = totalsOn && std::strcmp(std::getenv("RE4_GX_PHASES"),"total");
inline const bool gpuOn = On("RE4_GX_GPUTIME");
inline const bool finishAux = On("RE4_GX_FINISH_AUX");
inline void Extensions() {
  if(!vtxOn && !totalsOn && !gpuOn && !finishAux) return;
  const char* ext=reinterpret_cast<const char*>(::glGetString(GL_EXTENSIONS));
  if(!ext) ext="";
  std::fprintf(stderr,"glext-all: %s\n",ext);
  for(const char* name:{"GL_EXT_disjoint_timer_query","GL_EXT_buffer_storage","GL_EXT_multi_draw_arrays",
                       "GL_EXT_discard_framebuffer","GL_EXT_texture_storage"})
    std::fprintf(stderr,"glext: %s=%d\n",name,int(std::strstr(ext,name)!=nullptr));
  std::fprintf(stderr,"glext-note: texture_storage and invalidate_framebuffer are also GLES3 core features\n");
}
inline bool active = false;
inline unsigned sequence = 0;
inline std::uint64_t Now() {
  timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return std::uint64_t(t.tv_sec) * 1000000000ull + t.tv_nsec;
}
enum PhaseId { Rest, Ops, State, StateSignature, ShaderKey, ShaderCache, Merge,
               Indices, Memcpy, UniformCPU, DrawCPU, TextureCPU, Diagnostic, PhaseCount };
inline const char* phaseNames[] = {"run_rest", "ops_dispatch", "apply_state", "state_signature",
  "shader_key", "shader_cache", "merge", "indices_prepare", "memcpy", "uniform_cpu",
  "draw_cpu", "texture_cpu", "diagnostic"};
inline std::uint64_t glNs = 0, phaseCalls[PhaseCount] = {}, phaseNs[PhaseCount] = {};
inline std::uint64_t runNs = 0, runCount = 0, glWindowNs = 0, clockCalls = 0;
struct Phase;
inline Phase* current = nullptr;
struct Phase {
  bool enabled = false;
  int id;
  std::uint64_t start = 0, glStart = 0, children = 0;
  Phase* parent = nullptr;
  explicit Phase(int value) : id(value) {
    enabled = phasesOn && active;
    if (enabled) { parent = current; current = this; glStart = glNs; start = Now(); ++clockCalls; }
  }
  ~Phase() {
    if (!enabled) return;
    const auto gross = Now() - start; ++clockCalls;
    const auto gl = glNs - glStart;
    const auto cpu = gross >= gl ? gross - gl : 0;
    phaseNs[id] += cpu >= children ? cpu - children : 0;
    ++phaseCalls[id];
    if (parent) parent->children += cpu;
    current = parent;
  }
};
struct RunScope {
  std::uint64_t start=0, glStart=0;
  explicit RunScope(bool skip, unsigned seq) {
    active=!skip;sequence=seq;
    if(totalsOn && active) {start=Now();glStart=glNs;}
  }
  ~RunScope() {
    if(start) {
      const auto ns=Now()-start;
      runNs+=ns;glWindowNs+=glNs-glStart;++runCount;
      std::fprintf(stderr,"runtrace: seq=%u ms=%.6f gl_ms=%.6f detailed=%d\n",sequence,ns/1e6,(glNs-glStart)/1e6,int(phasesOn));
    }
    active=false;
  }
};
inline void* Copy(void* dst, const void* src, std::size_t size) {
  Phase phase(Memcpy);
  return std::memcpy(dst, src, size);
}
// xxHash64-style word-at-a-time hash; memcpy permits unaligned input on ARM.
inline std::uint64_t Rot(std::uint64_t x, unsigned n) { return (x << n) | (x >> (64 - n)); }
inline std::uint64_t Word(const unsigned char* p) { std::uint64_t x; std::memcpy(&x, p, 8); return x; }
inline std::uint64_t Hash(const unsigned char* p, std::size_t n) {
  constexpr std::uint64_t a=11400714785074694791ull, b=14029467366897019727ull,
    c=1609587929392839161ull, d=9650029242287828579ull, e=2870177450012600261ull;
  const unsigned char* end=p+n;
  auto round=[](std::uint64_t v, std::uint64_t x) { return Rot(v + x*b,31)*a; };
  std::uint64_t h;
  if (n>=32) {
    std::uint64_t v1=a+b,v2=b,v3=0,v4=0-a;
    do { v1=round(v1,Word(p));v2=round(v2,Word(p+8));v3=round(v3,Word(p+16));v4=round(v4,Word(p+24));p+=32; } while(end-p>=32);
    h=Rot(v1,1)+Rot(v2,7)+Rot(v3,12)+Rot(v4,18);
    for(auto v:{v1,v2,v3,v4}) {h^=round(0,v);h=h*a+d;}
  } else h=e;
  h+=n;
  while(end-p>=8) {h^=round(0,Word(p));h=Rot(h,27)*a+d;p+=8;}
  if(end-p>=4) {std::uint32_t x;std::memcpy(&x,p,4);h^=std::uint64_t(x)*a;h=Rot(h,23)*b+c;p+=4;}
  while(p<end) {h^=std::uint64_t(*p++)*e;h=Rot(h,11)*a;}
  h^=h>>33;h*=b;h^=h>>29;h*=c;return h^(h>>32);
}
using BlockKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
inline std::set<BlockKey> prevAddress, nextAddress, prevLogical, nextLogical;
inline std::set<std::pair<std::uint64_t,std::uint64_t>> prevContent, nextContent;
struct VtxTotals {
  std::uint64_t vertices=0, bytes=0, indices=0, extraIndices=0, sameAddress=0, sameLogical=0, sameContent=0, blocks=0;
  unsigned chunks=0;
};
inline VtxTotals frame;
inline unsigned vtxFrame=0;
inline void Vtx(const Re4GxChunk& chunk, bool uploaded, std::size_t extra) {
  if(!vtxOn || !uploaded) return;
  Phase diagnostic(Diagnostic);
  const auto start=Now();
  std::uint64_t addressBytes=0,logicalBytes=0,contentBytes=0,owner=0,ownerFn=0;
  unsigned ownerDraw=0, blocks=0;
  std::set<std::pair<unsigned,unsigned>> ranges;
  struct Format {std::uint64_t vertices=0,bytes=0;unsigned draws=0;};
  std::map<std::pair<unsigned,unsigned>,Format> formats;
  for(unsigned at=0;at<chunk.opWords;) {
    unsigned head=chunk.ops[at],n=head>>8,op=head&255;
    if(std::uint64_t(at)+1+n>chunk.opWords) break;
    const auto* p=chunk.ops+at+1;
    if(op==RE4_GX_OP_OWNER && n>=4) {owner=p[0];ownerFn=p[1];ownerDraw=0;}
    if(op==RE4_GX_OP_DRAW && n*4>=sizeof(Re4GxDraw)) {
      Re4GxDraw draw;std::memcpy(&draw,p,sizeof(draw));
      const std::uint64_t size=std::uint64_t(draw.vertexCount)*draw.stride;
      const auto ordinal=ownerDraw++;
      if(size && draw.vertexOffset+size<=chunk.vertexBytes && ranges.emplace(draw.vertexOffset,unsigned(size)).second) {
        auto& format=formats[{draw.layout,draw.stride}];format.vertices+=draw.vertexCount;format.bytes+=size;++format.draws;
        const auto hash=Hash(chunk.vertices+draw.vertexOffset,size);
        BlockKey addr{std::uintptr_t(chunk.vertices+draw.vertexOffset),size,hash};
        // The GX stream has no original source-array address. This second key is
        // an explicitly labelled logical identity, NOT a fabricated GC address.
        const std::uint64_t logical=owner ^ (ownerFn<<32) ^ (std::uint64_t(ordinal)*0x9e3779b97f4a7c15ull) ^ draw.layout;
        BlockKey object{logical,size,hash};
        const auto content=std::make_pair(size,hash);
        if(prevAddress.count(addr)) addressBytes+=size;
        if(prevLogical.count(object)) logicalBytes+=size;
        if(prevContent.count(content)) contentBytes+=size;
        nextAddress.insert(addr);nextLogical.insert(object);nextContent.insert(content);++blocks;
      }
    }
    at+=1+n;
  }
  frame.vertices+=chunk.vertexCount;frame.bytes+=chunk.vertexBytes;frame.indices+=std::uint64_t(chunk.indexCount)*2;
  frame.extraIndices+=extra;frame.sameAddress+=addressBytes;frame.sameLogical+=logicalBytes;frame.sameContent+=contentBytes;
  frame.blocks+=blocks;++frame.chunks;
  const auto ns=Now()-start;
  std::fprintf(stderr,"vtxchunk: frame=%u seq=%u flags=%x vertices=%u vertex_bytes=%u bpv=%.3f index_bytes=%llu merge_index_bytes=%zu blocks=%u identical_address_bytes=%llu identical_logical_bytes=%llu identical_content_bytes=%llu diagnostic_ms=%.3f\n",
    vtxFrame,chunk.sequence,chunk.flags,chunk.vertexCount,chunk.vertexBytes,chunk.vertexCount?double(chunk.vertexBytes)/chunk.vertexCount:0,
    (unsigned long long)chunk.indexCount*2,extra,blocks,(unsigned long long)addressBytes,(unsigned long long)logicalBytes,(unsigned long long)contentBytes,ns/1e6);
  for(const auto& f:formats) {
    unsigned layout=f.first.first,norm=(layout&RE4_GX_V_NORMAL)?12:0,nbt=norm&&(layout&RE4_GX_V_BINORMAL_TANGENT)?24:0,
      colors=((layout&RE4_GX_V_COLOR0)?1:0)+((layout&RE4_GX_V_COLOR1)?1:0),uv=0;
    for(unsigned i=0;i<8;++i) if(layout&(RE4_GX_V_TEX0<<i))++uv;
    const unsigned matrix=(layout&RE4_GX_V_MATRIX)?12:0;
    std::fprintf(stderr,"vtxformat: seq=%u layout=%x stride=%u vertices=%llu bytes=%llu blocks=%u pos=f32x3:12 normal=f32x3:%u nbt=f32x6:%u colors=unorm8x4:%u uv=f32x2:%u matrix=packed_u32x3:%u padding=%d\n",
      chunk.sequence,layout,f.first.second,(unsigned long long)f.second.vertices,(unsigned long long)f.second.bytes,f.second.draws,
      norm,nbt,colors*4,uv*8,matrix,int(f.first.second)-int(12+norm+nbt+colors*4+uv*8+matrix));
  }
  if(chunk.flags&RE4_GX_CHUNK_XFB) {
    std::fprintf(stderr,"vtxframe: frame=%u seq=%u chunks=%u vertices=%llu vertex_bytes=%llu bpv=%.3f index_bytes=%llu merge_index_bytes=%llu identical_address_pct=%.3f identical_logical_pct=%.3f identical_content_pct=%.3f\n",
      vtxFrame++,chunk.sequence,frame.chunks,(unsigned long long)frame.vertices,(unsigned long long)frame.bytes,
      frame.vertices?double(frame.bytes)/frame.vertices:0,(unsigned long long)frame.indices,(unsigned long long)frame.extraIndices,
      frame.bytes?100.*frame.sameAddress/frame.bytes:0,frame.bytes?100.*frame.sameLogical/frame.bytes:0,frame.bytes?100.*frame.sameContent/frame.bytes:0);
    prevAddress.swap(nextAddress);nextAddress.clear();prevLogical.swap(nextLogical);nextLogical.clear();
    prevContent.swap(nextContent);nextContent.clear();frame={};
  }
}

inline void Report(bool glprof) {
  if(!totalsOn) return;
  std::fprintf(stderr,"phases: runs=%llu run_ms=%.3f gl_ms=%.3f glprof=%d clock_calls=%llu\n",
    (unsigned long long)runCount,runCount?runNs/1e6/runCount:0,runCount?glWindowNs/1e6/runCount:0,int(glprof),(unsigned long long)clockCalls);
  for(int i=0;i<PhaseCount;++i) {
    std::fprintf(stderr,"phase: %s ms=%.3f calls=%llu\n",phaseNames[i],phaseNs[i]/1e6,(unsigned long long)phaseCalls[i]);
    phaseNs[i]=phaseCalls[i]=0;
  }
  runNs=runCount=glWindowNs=clockCalls=0;
}
} // namespace gxdiag
