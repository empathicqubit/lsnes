#ifdef CPU_CPP

//called every 256 clocks; see CPU::add_clocks()
void CPU::step_auto_joypad_poll() {
  if(vcounter() >= (ppu.overscan() == false ? 225 : 240)) {
    auto cycle = status.auto_joypad_counter & 63;
    //cache enable state at first iteration
    if(cycle == 0) status.auto_joypad_latch = status.auto_joypad_poll;
    status.auto_joypad_active = cycle <= 15;
    if(status.auto_joypad_active && status.auto_joypad_latch) {
      if(cycle == 0) {
        if(status.auto_joypad_counter & 128)
          std::cerr << "step_auto_joypad_poll(): bus fixes set (counter=" << status.auto_joypad_counter << ")???" << std::endl;
        if(dma_trace_fn) dma_trace_fn("-- Start automatic polling --");
        interface->notifyLatched();
        input.port1->latch(1);
        input.port2->latch(1);
        input.port1->latch(0);
        input.port2->latch(0);
      }

      uint2 port0 = input.port1->data();
      uint2 port1 = input.port2->data();

      status.joy1 = (status.joy1 << 1) | (bool)(port0 & 1);
      status.joy2 = (status.joy2 << 1) | (bool)(port1 & 1);
      status.joy3 = (status.joy3 << 1) | (bool)(port0 & 2);
      status.joy4 = (status.joy4 << 1) | (bool)(port1 & 2);
      if(cycle == 15) {
        char buf[512];
        sprintf(buf, "-- End automatic polling [%04x %04x %04x %04x] --",
          status.joy1, status.joy2, status.joy3, status.joy4);
        if(dma_trace_fn) dma_trace_fn(buf);
      }
    }

    //Only bits 0-5 are supposed to increment.
    if(cycle < 60)
      status.auto_joypad_counter++;
  }
}

//called every 128 clocks; see CPU::add_clocks()
void CPU::step_auto_joypad_poll_NEW2(bool polarity) {
  //Poll starts on multiple of 128 mod 256 clocks (polarity=false) on first
  //vblank scanline. If autopoller is off, mark as done for the frame.
  if(vcounter() >= (ppu.overscan() == false ? 225 : 240) && !polarity &&
    (status.auto_joypad_counter & 63) == 0) {
    if(!(status.auto_joypad_counter & 128))
      std::cerr << "step_auto_joypad_poll_NEW2(): bus fixes clear???" << std::endl;
    //Preserve high bits of autopoll counter.
    auto x = status.auto_joypad_counter & ~63;
    status.auto_joypad_counter = x | (status.auto_joypad_poll ? 1 : 36);
    status.auto_joypad_latch = status.auto_joypad_poll;
  }
  //Abuse bit 6 of counter for "manual" poll flag. Bit 7 is supposed to be
  //always set.
  auto cycle = status.auto_joypad_counter & 63;
  auto old_latchstate = (status.auto_joypad_counter & 320) != 0;
  //If not enabled... This is not latched, as autopoll can be aborted.
  if(!status.auto_joypad_poll && cycle > 0 && cycle < 36) {
    if(dma_trace_fn) dma_trace_fn("-- Automatic polling ABORTED --");
    status.auto_joypad_counter += (36 - cycle);
    status.auto_joypad_active = false;
    status.auto_joypad_latch = false;
    //Release autopoll latch.
    status.auto_joypad_counter &= ~256;	//Autopoll clears latch.
    auto new_latchstate = (status.auto_joypad_counter & 320) != 0;
    if(old_latchstate && !new_latchstate) {
      input.port1->latch(0);
      input.port2->latch(0);
    }
    return;
  }
  //On cycle #1, latch is asserted (unless latch is already high, in this
  //case the autopoller is supposed to force latch high too).
  if(cycle == 1) {
    if(dma_trace_fn) dma_trace_fn("-- Start automatic polling --");
    //Assert autopoll latch.
    status.auto_joypad_counter |= 256;
    auto new_latchstate = (status.auto_joypad_counter & 320) != 0;
    if(!old_latchstate && new_latchstate) {
      interface->notifyLatched();
      input.port1->latch(1);
      input.port2->latch(1);
    }
  }
  //On cycle #2, busy is asserted and controllers are cleared.
  if(cycle == 2) {
    status.joy1 = 0;
    status.joy2 = 0;
    status.joy3 = 0;
    status.joy4 = 0;
    status.auto_joypad_active = true;
  }
  //Then, on cycle #3, latch is deasserted, unless "manual" latch forces
  //real latch high.
  if(cycle == 3) {
    //Release autopoll latch.
    status.auto_joypad_counter &= ~256;
    auto new_latchstate = (status.auto_joypad_counter & 320) != 0;
    if(old_latchstate && !new_latchstate) {
      input.port1->latch(0);
      input.port2->latch(0);
    }
  }
  //Then on cycles #4, #6, #8, ..., #34, a bit is shifted. Also, clock would
  //go low, but we can not emulate that.
  if(cycle >= 4 && cycle <= 34 && cycle % 2 == 0) {
    uint2 port0 = input.port1->data();
    uint2 port1 = input.port2->data();
    status.joy1 = (status.joy1 << 1) | (bool)(port0 & 1);
    status.joy2 = (status.joy2 << 1) | (bool)(port1 & 1);
    status.joy3 = (status.joy3 << 1) | (bool)(port0 & 2);
    status.joy4 = (status.joy4 << 1) | (bool)(port1 & 2);
  }
  //Then on cycles #5, #7, #9, ..., #35, clock drops high, But we can not
  //emulate that.
  //Then on cycle #35, busy flag is deasserted and poll is complete.
  if(cycle == 35) {
    status.auto_joypad_active = false;
    char buf[512];
    sprintf(buf, "-- End automatic polling [%04x %04x %04x %04x] --",
      status.joy1, status.joy2, status.joy3, status.joy4);
    if(dma_trace_fn) dma_trace_fn(buf);
  }
  //The entiere train is 35 cycles.
  if(cycle > 0 && cycle < 36) {
      status.auto_joypad_counter++;
  }
}


//called every 128 clocks; see CPU::add_clocks()
void CPU::step_auto_joypad_poll_NEW(bool polarity, bool new2) {
  if(new2) return step_auto_joypad_poll_NEW2(polarity);
  auto cycle = status.auto_joypad_counter & 63;
  if(cycle > 0 && cycle <= 34) {
    if(!status.auto_joypad_latch) {
      //FIXME: Is this right, busy flag goes on even if not enabled???
      if(cycle == 1)
        status.auto_joypad_active = true;
      if(cycle == 34)
        status.auto_joypad_active = false;
    } else {
      if(cycle == 1) {
        if(status.auto_joypad_counter & 128)
          std::cerr << "step_auto_joypad_poll_NEW(): bus fixes set???" << std::endl;
        if(dma_trace_fn) dma_trace_fn("-- Start automatic polling --");
        status.auto_joypad_active = true;
        interface->notifyLatched();
        input.port1->latch(1);
        input.port2->latch(1);
      }
      if(cycle == 3) {
        input.port1->latch(0);
        input.port2->latch(0);
      }
      if((cycle & 1) != 0 && cycle != 1) {
        uint2 port0 = input.port1->data();
        uint2 port1 = input.port2->data();

        status.joy1 = (status.joy1 << 1) | (bool)(port0 & 1);
        status.joy2 = (status.joy2 << 1) | (bool)(port1 & 1);
        status.joy3 = (status.joy3 << 1) | (bool)(port0 & 2);
        status.joy4 = (status.joy4 << 1) | (bool)(port1 & 2);
      }
      if(cycle == 34) {
        status.auto_joypad_active = false;
        char buf[512];
        sprintf(buf, "-- End automatic polling [%04x %04x %04x %04x] --",
          status.joy1, status.joy2, status.joy3, status.joy4);
        if(dma_trace_fn) dma_trace_fn(buf);
      }
    }
    status.auto_joypad_counter++;
  }
  if(vcounter() >= (ppu.overscan() == false ? 225 : 240) && cycle == 0 && !polarity) {
    //Preserve high bits of autopoller counter.
    auto x = status.auto_joypad_counter & ~63;
    status.auto_joypad_latch = status.auto_joypad_poll;
    status.auto_joypad_counter = x | 1;
  }
}


#endif
