#ifndef NES_HPP
#define NES_HPP

#include <base/base.hpp>

namespace NES {
  namespace Info {
    static const char Name[] = "bnes";
    static const unsigned SerializerVersion = 1;
  }
}

/*
  bnes - NES emulator
  authors: byuu, Ryphecha
  license: GPLv3
  project started: 2011-09-05
*/

#include <libco/libco.h>

namespace NES {
  struct Processor {
    cothread_t thread;
    unsigned frequency;
    int64 clock;

    inline void create(void (*entrypoint)(), unsigned frequency) {
      if(thread) co_delete(thread);
      thread = co_create(65536 * sizeof(void*), entrypoint);
      this->frequency = frequency;
      clock = 0;
    }

    inline void serialize(serializer &s) {
      s.integer(frequency);
      s.integer(clock);
    }

    inline Processor() : thread(nullptr) {
    }

    inline ~Processor() {
      if(thread) co_delete(thread);
    }
  };

  #include <nes/system/system.hpp>
  #include <nes/scheduler/scheduler.hpp>
  #include <nes/input/input.hpp>
  #include <nes/memory/memory.hpp>
  #include <nes/cartridge/cartridge.hpp>
  #include <nes/cpu/cpu.hpp>
  #include <nes/apu/apu.hpp>
  #include <nes/ppu/ppu.hpp>
  #include <nes/cheat/cheat.hpp>
  #include <nes/video/video.hpp>
  #include <nes/interface/interface.hpp>
}

#endif
