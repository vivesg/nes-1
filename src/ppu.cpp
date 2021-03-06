#include "ppu.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <cmath>

const uint16_t ATTRIBUTE_TABLE_BASE_ADDR = 0x23c0;
const uint16_t NAME_TABLE_BASE_ADDR = 0x2000;
const uint16_t NAME_TABLE_SIZE = 0x400;
const uint16_t PATTERN_TABLE_SIZE = 0x1000;


void PPU::render_load_shift_registers() {
  bg_shift_pat &= 0xffff0000;
  bg_shift_pat |= tilepat;
  bg_shift_attr &= 0xffff0000;
  bg_shift_attr |= 0x5555 * tileattr;
}

void PPU::render_nt_lookup_0() {
  ioaddr = NAME_TABLE_BASE_ADDR + (vram.raw & 0xfff);
}

void PPU::render_nt_lookup_1() {
  pat_addr = PATTERN_TABLE_SIZE * reg.bg_addr + 16 * read(ioaddr) + vram.yfine;
}

void PPU::render_attr_lookup_0() {
  ioaddr = ATTRIBUTE_TABLE_BASE_ADDR + NAME_TABLE_SIZE * vram.base_nta + 8 * (vram.ycoarse / 4) + (vram.xcoarse / 4);
}

void PPU::render_attr_lookup_1() {
  tileattr = (read(ioaddr) >> ((vram.xcoarse & 2) + 2 * (vram.ycoarse & 2))) & 3;
}

void PPU::render_fetch_bg_low_0() {
  tilepat = read(pat_addr);
}

void PPU::render_fetch_bg_low_1() {
  // Interlace the two patterns (temp) // directly lifted from bisqwit
  unsigned p = tilepat | (read(pat_addr + 8) << 8);
  p = (p & 0xf00f) | ((p & 0x0f00)>>4) | ((p & 0x00f0)<<4);
  p = (p & 0xc3c3) | ((p & 0x3030)>>2) | ((p & 0x0c0c)<<2);
  p = (p & 0x9999) | ((p & 0x4444)>>1) | ((p & 0x2222)<<1);
  tilepat = p;
}

void PPU::render_fetch_bg_high_0() {
  // TODO: move from interlaced emulation
}

void PPU::render_fetch_bg_high_1() {
}

void PPU::render_incr_horizontal() {
    if (++vram.xcoarse == 0) {
      vram.base_nta_x = 1 - vram.base_nta_x;
    }
}

void PPU::render_copy_horizontal() {
  if (reg.show_bg) {
    vram.xcoarse = (unsigned)scroll.xcoarse;
    vram.base_nta_x = (unsigned)scroll.base_nta_x;
    sprrenpos = 0;
  }
}

void PPU::render_incr_vertical() {
  if (++vram.yfine == 0 && ++vram.ycoarse == 30) {
    vram.ycoarse = 0;
    vram.base_nta_y = 1 - vram.base_nta_y;
  }
}

// Pre-render scanline only
void PPU::render_copy_vertical() {
  if(reg.show_bg) {
    vram.raw = (unsigned)scroll.raw;
  }
}


void PPU::render_set_vblank() {

  double rate = bus->get_rate();
  double remainder = fmod(1.0, rate);
  static int s = 0;

  if (++frameskip_count >= 1.0 * rate) {
    frameskip_count = 0;
    if (rate > 1.0 && remainder > 0.001 && s++ >= 1.0 / remainder) {
      s = 0;
    }
  }

  bus->on_frame();

  reg.vblanking = true; 

  if (reg.NMI_enabled) {
    bus->pull_NMI();
  }

}

void PPU::render_clear_vblank() {
  scanline = -1;
  odd_frame ^= 1;
  reg.PPUSTATUS = 0;
}

void PPU::render_fetch_sprite_low_0() {}
void PPU::render_fetch_sprite_low_1() {}
void PPU::render_fetch_sprite_high_0() {}
void PPU::render_fetch_sprite_high_1() {}

void PPU::render_OAM_clear() {
  sprinpos = sproutpos = 0;
  if(reg.show_sp) {
    reg.OAMADDR = 0;
  }
}

void PPU::render_OAM_read_0() {

  if(sprrenpos < sproutpos) {

    auto& object = OAM3[sprrenpos];
    memcpy(&object, &OAM2[sprrenpos], sizeof(object));
    
    unsigned y = scanline - object.y;

    if (object.flip_vertically) {
      y ^= (reg.sprite_size + 1) * 8 - 1;
    }

    if (reg.sprite_size) {

      pat_addr = PATTERN_TABLE_SIZE * (object.index & 1) + 0x10 * (object.index & 0xfe);

    } else {

      pat_addr = PATTERN_TABLE_SIZE * reg.sp_addr + 0x10 * (object.index & 0xff);

    }

    pat_addr += (y & 7) + (y & 8) * 2;

  }

}

void PPU::render_OAM_read_1() {
  if (sprrenpos < sproutpos) {
    OAM3[sprrenpos++].pattern = tilepat;
  }
}

void PPU::render_OAM_write() {
  sprtmp = OAM[reg.OAMADDR];
}

template<int X, char X_MOD_8, bool TDM, bool X_ODD_64_TO_256, bool X_LT_256, bool X_LT_337>
void PPU::render() {

  if (X && X_LT_337) {
    bg_shift_pat <<= 2;
    bg_shift_attr <<= 2;
  }

  // Prepare fetch pattern from the NT
  if(X_MOD_8 == 1) {
    render_nt_lookup_0();
  }

  // Also, 2nd fetch in cycle is another NT byte at the end of the scanline
  // ...TODO...


  if (X_MOD_8 == 2) {
    render_nt_lookup_1();
  }

  if (X_MOD_8 == 3 && TDM) {
    render_attr_lookup_0();
  } 


  if (X_MOD_8 == 4 && TDM) {
    render_attr_lookup_1();
  }

  // got the address, get the bytes
  if(X_MOD_8 == 5) {
    render_fetch_bg_low_0();
  }

  if(X_MOD_8 == 6) {
    render_fetch_bg_low_1();
  }

  if (X_MOD_8 == 7) {
    render_fetch_bg_high_0();
  }

  if (X && X_MOD_8 == 0) {
    render_fetch_bg_high_1();
  }

  if (X && X_MOD_8 == 0 && TDM) {
    render_incr_horizontal();
  }


  // End of scanline: copy temp horizontal data to main vram
  if (X == 257) {
    render_copy_horizontal();
  }

  // Increment y
  if (X == 256) {
    render_incr_vertical();
  }

  // Pre-render scanline: copy temp vertical data to main vram
  // From 280 - 304
  if (X == 304 && scanline == -1) {
    render_copy_vertical();
  }



  if (X && X_MOD_8 == 0) {
    render_load_shift_registers();
  }

  // Sprite update
  //
  //
  if (X == 1) {
    render_OAM_clear();
  }

  if (X_MOD_8 == 3 && !TDM) {
    render_OAM_read_0();
  }

  if (X_MOD_8 == 7 && !TDM) {
    render_OAM_read_1();
  }


  if (X == 256) {
    // hack for mmc3 scanline counter
    read(0x1000);
  }

  // Prepare sprite data
  //
  if (X_ODD_64_TO_256) {
    switch (reg.OAMADDR++ & 3) {
      case 0:
        // Primary OAM can hold 64 sprites per frame
        if (sprinpos >= 64) {
          reg.OAMADDR = 0;
          break;
        }

        ++sprinpos;

        // Secondary OAM holds max 8 sprites per scanline
        OAM2[sproutpos].y = sprtmp;
        OAM2[sproutpos].sprindex = reg.OAM_index;

        // If the sprite is not on this scanline, skip ahead in memory
        if (!(sprtmp <= scanline && scanline < sprtmp + (reg.sprite_size? 16 : 8))) {
          reg.OAMADDR = sprinpos != 2 ? reg.OAMADDR + 3 : 8;
        }

        break;

      case 1:
        OAM2[sproutpos].index = sprtmp;
        break;

      case 2:
        OAM2[sproutpos].attr = sprtmp;
        break;

      case 3:
        if (sproutpos < 8) {
          OAM2[sproutpos++].x = sprtmp;
        } else {
          reg.spr_overflow = true;
        }
        
        if (sprinpos == 2) {
          reg.OAMADDR = 8;
        }
        
        break;
    }
  } else {

    render_OAM_write();

  }

  if(X_LT_256 && scanline >= 0) {
    render_pixel();
  }

}


//<int X, int X_MOD_8, int Tile_Decode_Mode, bool X_ODD_64_TO_256, bool X_LT_256, bool X_LT_337>
#define X(a) (&PPU::render<\
  (((a)==0)||((a)==1)||((a)==256)||((a)==257)||((a)==328)||((a)==336)||((a)==337)?(a):(280 <= a && a < 305)?304:-1),\
  ((a) & 7),\
  ((1 <= (a) && (a) <= 257) || (321 <= (a) && (a) <= 340)),\
  bool(((a) & 1) && ((a) >= 64) && ((a) < 256)),\
  bool((a) < 256),\
  bool((a) < 337)>)

#define Y(a) X(a),X(a+1),X(a+2),X(a+3),X(a+4),X(a+5),X(a+6),X(a+7),X(a+8),X(a+9)
const PPU::Renderf_array PPU::renderfuncs {
  Y(  0),Y( 10),Y( 20),Y( 30),Y( 40),Y( 50),Y( 60),Y( 70),Y( 80),Y( 90),
  Y(100),Y(110),Y(120),Y(130),Y(140),Y(150),Y(160),Y(170),Y(180),Y(190),
  Y(200),Y(210),Y(220),Y(230),Y(240),Y(250),Y(260),Y(270),Y(280),Y(290),
  Y(300),Y(310),Y(320),Y(330),X(340),X(341)
};
#undef X
#undef Y



void PPU::render_pixel() {

  bool edge = !(8 <= cycle && cycle < 248);
  bool showbg = ((!edge) || reg.show_bg8) && reg.show_bg;
  bool showsp = ((!edge) || reg.show_sp8) && reg.show_sp;

  unsigned fx = scroll.xfine;
  unsigned pixel { 0 }, attr { 0 };
  
  if (showbg) {

    pixel = (bg_shift_pat >> (30 - fx * 2)) & 3;
    attr = (bg_shift_attr >> (30 - fx * 2)) & (pixel ? 3 : 0);

  } else if ((vram.raw & 0x3f00) == 0x3f00 && !reg.rendering_enabled) {
    pixel = vram.raw;
  }

  if (showsp) {
    for (int sno = 0; sno < sprrenpos; ++sno) {
      auto& s = OAM3[sno];
      
      unsigned xdiff = cycle - s.x;
      
      if(xdiff >= 8)
        continue;
        
      if(!s.flip_horizontally) {
        xdiff = 7 - xdiff;
      }

      uint8_t spritepixel = (s.pattern >> (xdiff * 2)) & 3;

      if(!spritepixel) 
        continue;

      if(pixel && s.sprindex == 0)
        reg.spr0_hit = true;

      if (!pixel || !s.priority) {
        attr = s.palette + 4;
        pixel = spritepixel;
      }
      
      break;
    
    }
  }


  pixel = palette[(attr * 4 + pixel) & 0x1f] & (0x30 + (!reg.grayscale) * 0x0f);

  video->put_pixel(cycle, scanline, pixel & 0x3f);

}


void PPU::write(uint8_t value, uint16_t addr) {

  addr &= 0x3fff;

  if (addr < 0x2000) { // Pattern table (CHR RAM/ROM)

    rom->write_chr(value, addr);

  } else if (addr < 0x3f00) { // Name table

    rom->write_nt(value, addr - 0x2000);

  } else { // Palette

    palette[addr & (0xf | (((addr & 3) != 0) << 4))] = value;

  }

}

// http://wiki.nesdev.com/w/index.php/PPU_memory_map
uint8_t PPU::read(uint16_t addr, bool no_palette /* = false */) const {

  addr &= 0x3fff;

  if (addr < 0x2000) { // Pattern table

    return rom->read_chr(addr);

  } else if (addr < 0x3f00 + no_palette * 0xff) { // Name table

    return rom->read_nt(addr - 0x2000);

  } else { // Palette http://wiki.nesdev.com/w/index.php/PPU_palettes

    // Retrieve the palette index; address above 0x3f20 are mirrors of 0x3f00 to 0x3f1f.
    // Additionally, index 10, 14, 18 and 1C are mirrors of 0, 4, 8 and C respectively.
    return palette[addr & (0xf | (((addr & 3) != 0) << 4))];

  }

}



void PPU::tick() {

  if (cycle == 1) {
    if (scanline == 241) {
      render_set_vblank();
    } else if (scanline == 261) {
      render_clear_vblank();
    }
  }

  if (reg.rendering_enabled && scanline < 240) {
      (*tick_renderer)(*this);
      ++tick_renderer;
  }

  ++cycle;

  if (cycle == 341) {

    cycle = (reg.rendering_enabled && (scanline == -1) && odd_frame);// || ((scanline & 1) && reg.show_bg);
    tick_renderer = renderfuncs.begin() + cycle;
    ++scanline;

  }

}

void PPU::regw_control(uint8_t value) {
  reg.PPUCTRL = value; 
  scroll.base_nta = reg.base_nta;
}

void PPU::regw_mask(uint8_t value) {
  reg.PPUMASK = value;
}

void PPU::regw_OAM_address(uint8_t value) {
  reg.OAMADDR = value;
}

void PPU::regw_OAM_data(uint8_t value) {
  OAM[reg.OAMADDR++] = value;
}

void PPU::regw_scroll(uint8_t value) {
  if (loopy_w) {
    scroll.yfine = value & 7;
    scroll.ycoarse = value >> 3;
  } else {
    scroll.xscroll = value;
  }
  loopy_w ^= 1;
}

void PPU::regw_address(uint8_t value) {
  if (loopy_w) {
    scroll.vaddr_lo = value;
    vram.raw = (unsigned)scroll.raw;
  } else {
    scroll.vaddr_hi = value & 0x3f;
  }
  loopy_w ^= 1;
}

void PPU::regw_data(uint8_t value) {
  write(value, vram.raw);
  vram.raw = vram.raw + (bool(reg.vramincr) * 31 + 1);
}

uint8_t PPU::regr_status() {
  uint8_t result { static_cast<uint8_t>(reg.PPUSTATUS) };
  reg.vblanking = false;
  loopy_w = false;
  return result;
}

uint8_t PPU::regr_OAM_data() {
  return OAM[reg.OAMADDR] & (reg.OAM_data == 2 ? 0xE3 : 0xFF);
}

uint8_t PPU::regr_data() {
  uint8_t result = read_buffer;
  read_buffer = read(vram.raw, true);
  vram.raw = vram.raw + (!!reg.vramincr * 31 + 1);
  return result;
}

PPU::PPU(IBus *bus, IROM *rom, IVideoDevice *video)
  : bus(bus)
  , rom(rom)
  , video(video)
  , tick_renderer(renderfuncs.begin())
{
  for (auto& i : framebuffer) {
    i = 0x008000ff;
  }

  reg.PPUCTRL = 0x00;
  reg.PPUMASK = 0x00;
  reg.PPUSTATUS = 0x00;
  reg.OAMADDR = 0;
}


