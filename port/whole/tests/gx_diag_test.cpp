// Standalone CPU tests; no GL context or game data required.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define GL_EXTENSIONS 0x1F03
const unsigned char* glGetString(unsigned) { return nullptr; }
#include "gx_diag.h"

int main() {
  unsigned char storage[100] = {};
  for (unsigned i=0;i<80;++i) storage[i]=static_cast<unsigned char>(i);
  unsigned char unaligned[101] = {};
  std::memcpy(unaligned+1,storage,80);
  assert(gxdiag::Hash(storage,80)==gxdiag::Hash(unaligned+1,80));
  const auto h=gxdiag::Hash(storage,80);
  storage[37]^=1;
  assert(h!=gxdiag::Hash(storage,80));

  unsigned char vertices[48] = {};
  std::vector<std::uint32_t> ops;
  for(unsigned i=0;i<2;++i) {
    Re4GxDraw d{};d.stride=12;d.vertexOffset=i*24;d.vertexCount=2;d.indexCount=3;
    ops.push_back(RE4_GX_OP_DRAW | (sizeof(d)/4)<<8);
    const auto at=ops.size();ops.resize(at+sizeof(d)/4);std::memcpy(ops.data()+at,&d,sizeof(d));
  }
  Re4GxChunk chunk{};chunk.flags=RE4_GX_CHUNK_XFB;chunk.ops=ops.data();chunk.opWords=ops.size();
  chunk.vertices=vertices;chunk.vertexBytes=48;chunk.vertexCount=4;
  gxdiag::Vtx(chunk,true,0);
  assert(gxdiag::prevAddress.size()==2);
  ++chunk.sequence;
  gxdiag::Vtx(chunk,true,0);
  // Repeating the same buffers yields address+size+content hits, too.
  assert(gxdiag::prevAddress.size()==2);
  vertices[0]=17;++chunk.sequence;
  gxdiag::Vtx(chunk,true,0);
  assert(gxdiag::prevContent.size()==2);

  gxdiag::active=true;
  {
    gxdiag::Phase outer(gxdiag::Rest);
    {gxdiag::Phase child(gxdiag::Memcpy);std::memcpy(storage,vertices,48);}
  }
  assert(gxdiag::current==nullptr);
  assert(gxdiag::phaseCalls[gxdiag::Rest]==1);
  assert(gxdiag::phaseCalls[gxdiag::Memcpy]==1);
  std::puts("gx_diag_test: OK");
}
