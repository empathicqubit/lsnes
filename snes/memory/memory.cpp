#include <snes/snes.hpp>

#define MEMORY_CPP
namespace SNES {

Bus bus;

unsigned Bus::mirror(unsigned addr, unsigned size) {
  unsigned base = 0;
  if(size) {
    unsigned mask = 1 << 23;
    while(addr >= size) {
      while(!(addr & mask)) mask >>= 1;
      addr -= mask;
      if(size > mask) {
        size -= mask;
        base += mask;
      }
      mask >>= 1;
    }
    base += addr;
  }
  return base;
}

void Bus::map(
  MapMode mode,
  unsigned bank_lo, unsigned bank_hi,
  unsigned addr_lo, unsigned addr_hi,
  unsigned mclass,
  const function<uint8 (unsigned)> &rd,
  const function<void (unsigned, uint8)> &wr,
  unsigned base, unsigned length
) {
  assert(bank_lo <= bank_hi && bank_lo <= 0xff);
  assert(addr_lo <= addr_hi && addr_lo <= 0xffff);
  unsigned id = idcount++;
  assert(id < 255);
  reader[id] = rd;
  writer[id] = wr;

  if(length == 0) length = (bank_hi - bank_lo + 1) * (addr_hi - addr_lo + 1);

  unsigned offset = 0;
  for(unsigned bank = bank_lo; bank <= bank_hi; bank++) {
    for(unsigned addr = addr_lo; addr <= addr_hi; addr++) {
      unsigned destaddr = (bank << 16) | addr;
      if(mode == MapMode::Linear) destaddr = mirror(base + offset++, length);
      if(mode == MapMode::Shadow) destaddr = mirror(base + destaddr, length);
      lookup[(bank << 16) | addr] = id;
      target[(bank << 16) | addr] = destaddr;
      if(mclass) classmap[(bank << 16) | addr] = mclass;
    }
  }
}

void Bus::map_reset() {
  function<uint8 (unsigned)> reader = [](unsigned) { return cpu.regs.mdr; };
  function<void (unsigned, uint8)> writer = [](unsigned, uint8) {};

  idcount = 0;
  map(MapMode::Direct, 0x00, 0xff, 0x0000, 0xffff, 0xFF, reader, writer);
}

void Bus::map_xml() {
  for(auto &m : cartridge.mapping) {
    map(m.mode, m.banklo, m.bankhi, m.addrlo, m.addrhi, (unsigned)m.clazz, m.read, m.write, m.offset, m.size);
  }
}

unsigned Bus::enumerateMirrors(uint8 clazz, uint32 offset, unsigned start)
{
  unsigned i;
  for(i = start; i < 0x1000000; i++)
    if((classmap[i] == clazz && target[i] == offset) || (i == offset && clazz == 255))
      return i;
  return i;
}

void Bus::clearDebugFlags()
{
  u_debugflags = 0;
  memset(debugflags, 0, 0x1000000);
}

void Bus::debugFlags(uint8 setf, uint8 clrf)
{
  u_debugflags = (u_debugflags | setf) & ~clrf;
}

void Bus::debugFlags(uint8 setf, uint8 clrf, uint8 clazz, uint32 offset)
{
  if(clazz == 255) {
    setf <<= 3;
    clrf <<= 3;
    debugflags[offset] = (debugflags[offset] | setf) & ~clrf;
  } else
    for(unsigned i = 0; i < 0x1000000; i++)
      if(classmap[i] == clazz && target[i] == offset)
        debugflags[i] = (debugflags[i] | setf) & ~clrf;
}

Bus::Bus() {
  u_debugflags = 0;
  lookup = new uint8 [112 * 1024 * 1024];
  target = (uint32*)(lookup + 0x3000000);
  classmap = lookup + 0x1000000;
  debugflags = lookup + 0x2000000;
  memset(debugflags, 0, 0x1000000);
}

Bus::~Bus() {
  delete[] lookup;
}

}
