#include "../../switch/source/avatar_effects.c"
#include "../../switch/source/igloo.c"
#include <assert.h>
#include <unistd.h>
#include <limits.h>
World W;
static unsigned art_draws,transformed_draws,particle_draws;
static SceneArt fake_art={.w=40,.h=60,.rx=20,.ry=50};
void dbg_log(const char *fmt,...){(void)fmt;}
void gfx_sprite(int page,int sx,int sy,int sw,int sh,float x,float y,float scale,bool flip,u32 color){(void)page;(void)sx;(void)sy;(void)sw;(void)sh;(void)x;(void)y;(void)scale;(void)flip;(void)color;particle_draws++;}
void gfx_additive(bool on){(void)on;}
const SceneArt *gfx_art(const char *name){assert(!strncmp(name,"penguin-",8));return &fake_art;}
void gfx_art_draw(const SceneArt *art,float x,float y,float sx,float sy,u32 color){(void)art;(void)x;(void)y;(void)sx;(void)sy;(void)color;art_draws++;}
bool gfx_avatar_effect(u64 key,u16 effect,const AvatarFxDraw *parts,unsigned count,bool mirrored,float x,float y,float scale,u8 alpha){
 assert((key>>62)==1&&count==2&&!mirrored&&scale>0&&alpha==255);(void)x;(void)y;
 AvatarFxBitmap image;assert(avatar_effect_compose(parts,count,mirrored,effect,&image));assert(image.width>0&&image.height>0);free(image.pixels);transformed_draws++;return true;
}
static unsigned crc32_bytes(const u8 *bytes,size_t count){unsigned value=~0u;for(size_t i=0;i<count;i++){value^=bytes[i];for(int b=0;b<8;b++)value=(value>>1)^((value&1)?0xedb88320:0);}return ~value;}
int main(int argc,char **argv){
 assert(argc==2);char directory[PATH_MAX];assert(realpath(argv[1],directory));char previous[PATH_MAX];assert(getcwd(previous,sizeof previous));char temporary[]="/tmp/tweebbo-penguin-effects-XXXXXX";assert(mkdtemp(temporary));assert(chdir(temporary)==0);assert(symlink(directory,"romfs:")==0);
 unsigned tested=0;puts("{\"layers\":[");
 for(unsigned frame=0;frame<PENGUIN_PIXEL_COUNT/2;frame++)for(unsigned layer=0;layer<2;layer++){
  AvatarFxDraw part;assert(avatar_effect_penguin_part(frame,layer,0xffffff,&part));const PeopleSprite *sprite=pixel_sprite(part.sprite);u8 *pixels=read_pixels(part.sprite);assert(sprite&&pixels);printf("%s[%u,%u,%u,%u,%u]",tested++?",\n":"",frame,layer,sprite->w,sprite->h,crc32_bytes(pixels,(size_t)sprite->w*sprite->h*4));free(pixels);
 }
 puts("],\"checks\":[");
 const u32 colors[]={0x2355cd,0xd04780,0xd5532b,0x2d9c69,0x8e48b6,0x2f9bb4,0xb89229,0x5f5b9b};
 unsigned comparisons=0;
 for(unsigned variant=0;variant<8;variant++)for(unsigned frame=0;frame<313;frame+=31){
  AvatarFxDraw parts[2];assert(avatar_effect_penguin_part(frame,false,0xffffff,parts));assert(avatar_effect_penguin_part(frame,true,colors[variant],parts+1));
  AvatarFxBitmap base;assert(avatar_effect_compose(parts,2,false,0,&base));
  for(unsigned effect=11;effect<=13;effect++){
   AvatarFxBitmap transformed;assert(avatar_effect_compose(parts,2,false,effect,&transformed));assert(base.width==transformed.width&&base.height==transformed.height&&base.x==transformed.x&&base.y==transformed.y);const AvatarFxAnimation *spec=avatar_effect(effect);assert(spec&&spec->transform);
   for(size_t n=0;n<(size_t)base.width*base.height;n++){
    const u8 *source=base.pixels+n*4,*actual=transformed.pixels+n*4;double level=(source[0]*.2126+source[1]*.7152+source[2]*.0722)/255.;const unsigned shifts[]={16,8,0,24};
    for(unsigned c=0;c<4;c++){double bg=(spec->background>>shifts[c])&255,fg=(spec->foreground>>shifts[c])&255;long expected=lrint(c==3?source[3]*(bg+(fg-bg)*level)/255.:bg+(fg-bg)*level);assert(abs((int)actual[c]-(int)expected)<=1);}
   }
   free(transformed.pixels);comparisons++;
  }free(base.pixels);
 }
 strcpy(W.model,"igloo_waddle");Ent penguin={.guest=4001000200u};assert(igloo_draw(&penguin,0,POSE_STD,1000,100,100,1));assert(art_draws==2&&!transformed_draws);
 penguin.effect=12;assert(igloo_draw(&penguin,2,POSE_WALK,1000,100,100,1));assert(transformed_draws==1&&art_draws==2);
 penguin.effect=1;assert(igloo_draw(&penguin,4,POSE_STD,1000,100,100,1));assert(particle_draws>0&&art_draws==4);
 assert(!avatar_effect_penguin_part(313,false,0, &(AvatarFxDraw){0}));penguin.guest=88;assert(!igloo_draw(&penguin,0,POSE_STD,1000,100,100,1));
 printf("\"original layer CRCs\",\"%u whole-figure transform comparisons\",\"real igloo draw dispatch\",\"particle layers\",\"invalid frame and ordinary avatar guards\"]}\n",comparisons);
 avatar_effect_pixels_close();assert(unlink("romfs:")==0);assert(chdir(previous)==0);assert(rmdir(temporary)==0);return 0;
}
