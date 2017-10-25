#ifdef CPU_CPP

uint8 CPU::port_read(uint2 port) const { return status.port[port]; }
void CPU::port_write(uint2 port, uint8 data) { status.port[port] = data; }

void CPU::op_io() {
  status.clock_count = 6;
  dma_edge();
  add_clocks(6);
  alu_edge();
}

uint8 CPU::op_read(uint32 addr, bool exec) {
  status.clock_count = speed(addr);
  dma_edge();
  add_clocks(status.clock_count - 4);
  //MDR presents the state held by parasitic capacitance of the external bus.
  //This bus is not affected by reads from CPU-internal registers, only if
  //some external device responds. SDD1 does hook some of these addresses, but
  //passes read straight through, as expected (as the CPU probably won't
  //monitor if external device responds, even if it broadcasts a read).
  //
  //We use 4000-43FF as CPU register range, and not 4000-437F it likely is
  //for quickness of checking. This will only affect things if some device
  //tries to map the 4380-43FF range (that device will still work correctly,
  //but openbus in that range won't).
  //
  //This was discovered while investigating why one Super Metroid glitch
  //worked on emulator but crashed on real console.
  //
  //a word fetch from 2f4017 AND 0xfffc results in 2f3c and a word fetch from
  //2f4210 AND 0x7f7f results in 2f22. This also extends to long fetches
  //by arguments. E.g. long argument fetch from 94420F with 2F already on
  //the bus AND 0x7f7fff results in 2f222f.
  //
  //The reason for masking some bits in above explanation was to ignore some
  //known bits in those registers (bits 7 of 4210 and 4211, bits 0&1 of 4017).
  uint8_t tmp = bus.read(addr, exec);
  if(!config.cpu.bus_fixes || (addr & 0x40FC00) != 0x004000) regs.mdr = tmp;
  add_clocks(4);
  alu_edge();
  return tmp;
}

void CPU::op_write(uint32 addr, uint8 data) {
  alu_edge();
  status.clock_count = speed(addr);
  dma_edge();
  add_clocks(status.clock_count);
  bus.write(addr, regs.mdr = data);
}

unsigned CPU::speed(unsigned addr) const {
  if(addr & 0x408000) {
    if(addr & 0x800000) return status.rom_speed;
    return 8;
  }
  if((addr + 0x6000) & 0x4000) return 8;
  if((addr - 0x4000) & 0x7e00) return 6;
  return 12;
}

#endif
