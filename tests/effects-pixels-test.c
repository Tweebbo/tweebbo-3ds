/* Decode the real packed source through the client code; the verifier checks original PNGs. */
#include "../source/avatar_effects.c"
#include <assert.h>
#include <unistd.h>
#include <limits.h>

void dbg_log(const char *fmt,...){(void)fmt;}
void gfx_sprite(int page,int sx,int sy,int sw,int sh,float x,float y,float scale,bool flip,u32 color){}
void gfx_additive(bool on){}
static u32 crc32_bytes(const u8 *bytes,size_t count){
  u32 value=0xffffffff;
  for(size_t i=0;i<count;i++){value^=bytes[i];for(int b=0;b<8;b++)value=(value>>1)^((value&1)?0xedb88320:0);}
  return value^0xffffffff;
}
int main(int argc,char **argv){
  assert(argc==2);char directory[PATH_MAX];assert(realpath(argv[1],directory));
  char previous[PATH_MAX];assert(getcwd(previous,sizeof previous));
  char temporary[]="/tmp/tweebbo-effect-pixels-XXXXXX";assert(mkdtemp(temporary));assert(chdir(temporary)==0);
  assert(symlink(directory,"romfs:")==0);puts("[");
  for(unsigned i=0;i<(unsigned)(people_sprite_count+swim_sprite_count);i++){
    const PeopleSprite *sprite=pixel_sprite(i);assert(sprite);
    u8 *pixels=read_pixels(i);assert(pixels);
    printf("%s[%u,%u,%u,%u]",i?",\n":"",i,sprite->w,sprite->h,crc32_bytes(pixels,(size_t)sprite->w*sprite->h*4));
    free(pixels);
  }
  puts("\n]");avatar_effect_pixels_close();assert(unlink("romfs:")==0);assert(chdir(previous)==0);assert(rmdir(temporary)==0);
  return 0;
}
