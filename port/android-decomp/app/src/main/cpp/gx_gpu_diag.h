#pragma once
#include "gx_diag.h"

namespace gxgpu {
inline constexpr GLenum elapsed=0x88BF, disjoint=0x8FBB;
using Result64 = void (*)(GLuint, GLenum, GLuint64*);
inline Result64 result64=nullptr;
inline bool available=false, initialized=false;
struct Query {
  GLuint id=0;
  bool pending=false, valid=true;
  unsigned seq=0, frame=0, index=0;
  int width=0,height=0;
  std::uint64_t cpu=0;
  std::string label,kind;
};
inline std::array<Query,512> queries;
inline int current=-1;
inline unsigned frame=0, auxIndex=0, passIndex=0, missed=0;
inline bool auxMode=false;
inline std::uint64_t beginCPU=0;
inline void Init(void* (*resolve)(const char*)) {
  if(initialized || (!gxdiag::gpuOn && !gxdiag::finishAux)) return;
  initialized=true;
  const auto* ext=reinterpret_cast<const char*>(::glGetString(GL_EXTENSIONS));
  if(gxdiag::gpuOn && ext && std::strstr(ext,"GL_EXT_disjoint_timer_query")) {
    result64=reinterpret_cast<Result64>(resolve("glGetQueryObjectui64vEXT"));
    GLint bits=0;::glGetQueryiv(elapsed,0x8864 /* QUERY_COUNTER_BITS_EXT */,&bits);
    available=result64 && bits>0;
    std::fprintf(stderr,"gputime-init: elapsed_bits=%d result64=%d available=%d; alternate pass/aux frames, no nested elapsed queries\n",bits,int(result64!=nullptr),int(available));
    if(available) {
      GLuint ids[512];::glGenQueries(512,ids);
      for(unsigned i=0;i<512;++i)queries[i].id=ids[i];
      GLint ignored;::glGetIntegerv(disjoint,&ignored);
    }
  } else std::fprintf(stderr,"gputime-init: GL_EXT_disjoint_timer_query absent; finish_aux=%d (not automatic)\n",int(gxdiag::finishAux));
}
inline void Collect() {
  if(!available) return;
  GLint bad=0;::glGetIntegerv(disjoint,&bad);
  if(bad) for(auto& q:queries) if(q.pending)q.valid=false;
  for(auto& q:queries) {
    if(!q.pending)continue;
    GLuint ready=0;::glGetQueryObjectuiv(q.id,GL_QUERY_RESULT_AVAILABLE,&ready);
    if(!ready)continue;
    GLuint64 ns=0;result64(q.id,GL_QUERY_RESULT,&ns);
    std::fprintf(stderr,"gputime: seq=%u frame=%u %s=%u target=%dx%d kind=%s cpu_ms=%.6f gpu_ms=%.6f valid=%d\n",
      q.seq,q.frame,q.label.c_str(),q.index,q.width,q.height,q.kind.c_str(),q.cpu/1e6,double(ns)/1e6,int(q.valid));
    q.pending=false;
  }
}
inline void FrameBegin() {
  if(!gxdiag::gpuOn && !gxdiag::finishAux)return;
  ++frame;auxIndex=passIndex=0;auxMode=frame&1;
  Collect();
}
inline void Begin(const char* label,unsigned index,int width,int height,const char* kind) {
  if(!available || current>=0)return;
  for(unsigned i=0;i<queries.size();++i) {
    auto& q=queries[i];
    if(q.pending)continue;
    current=i;q.seq=gxdiag::sequence;q.frame=frame;q.index=index;q.width=width;q.height=height;
    q.kind=kind;q.label=label;q.valid=true;
    beginCPU=gxdiag::Now();::glBeginQuery(elapsed,q.id);return;
  }
  ++missed;
}
inline void Target(int width,int height,const char* kind) {
  if(current>=0) {auto& q=queries[current];q.width=width;q.height=height;q.kind=kind;}
}
inline void End() {
  if(current<0)return;
  auto& q=queries[current];::glEndQuery(elapsed);q.cpu=gxdiag::Now()-beginCPU;q.pending=true;current=-1;
}
inline void PassBegin(unsigned index,int width,int height,const char* kind) {
  if(gxdiag::gpuOn && !auxMode) Begin("pass",index,width,height,kind);
}
inline void PassEnd() {if(gxdiag::gpuOn && !auxMode)End();}
template<class F> inline void Aux(F fn,int width,int height,const char* kind) {
  const auto index=auxIndex++;
  if(available && auxMode) {
    Begin("aux",index,width,height,kind);
    fn();End();return;
  }
  if(gxdiag::finishAux && !available) {
    const auto t0=gxdiag::Now();::glFinish();const auto t1=gxdiag::Now();
    fn();const auto t2=gxdiag::Now();::glFinish();const auto t3=gxdiag::Now();
    std::fprintf(stderr,"finish-aux: seq=%u aux=%u target=%dx%d kind=%s pre_ms=%.6f draw_ms=%.6f post_ms=%.6f isolated_ms=%.6f\n",
      gxdiag::sequence,index,width,height,kind,(t1-t0)/1e6,(t2-t1)/1e6,(t3-t2)/1e6,(t3-t1)/1e6);
    return;
  }
  fn();
}
inline void Release() {
  if(!initialized || !available)return;
  End();GLuint ids[512];for(unsigned i=0;i<512;++i)ids[i]=queries[i].id;
  ::glDeleteQueries(512,ids);available=false;
}
} // namespace gxgpu
