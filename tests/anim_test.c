// Avatar animations really change the drawn parts:
// talking (mouth), blinking, waving, dancing, walking, holding and sipping, sitting, swimming.
#include "../source/avatar.h"
#include "../source/assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../source/gen/character_skins.h"

static char trace[4096];
static int ops;
static u64 test_look = 0x1234567ull;
static u64 test_time;
static const PeopleSprite *tracked;
static bool tracked_drawn;
static float tracked_y;
u64 now_ms(void) { return test_time; }
static void rec(int page, int sx, int sy, int sw, int sh, float x, float y) {
  if(tracked&&tracked->page==page&&tracked->x==sx&&tracked->y==sy&&tracked->w==sw&&tracked->h==sh){tracked_drawn=true;tracked_y=y+tracked->ry;}
  char b[64];
  snprintf(b, sizeof b, "%d:%d,%d,%d,%d@%.0f,%.0f;", page, sx, sy, sw, sh, x, y);
  strncat(trace, b, sizeof trace - strlen(trace) - 1);
  ops++;
}
void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float s, bool f, u32 c) { (void)s; (void)f; (void)c; rec(page, sx, sy, sw, sh, x, y); }
void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool f, u32 c) { (void)kx; (void)ky; (void)f; (void)c; rec(page, sx, sy, sw, sh, x, y); }
void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool f, u32 c) { (void)kx; (void)ky; (void)f; (void)c; rec(page, sx, sy, sw, sh, x, y); }
void dbg_log(const char *fmt, ...) { (void)fmt; }

static const char *draw(Pose pose, int frame, int expr, int drink, bool sip) {
  static char out[8][4096];
  static int k;
  trace[0] = 0; ops = 0;
  avatar_draw(test_look, 2, pose, frame, expr, 2, drink, sip, 0, 0, 1, 255);
  k = (k + 1) % 8;
  snprintf(out[k], sizeof out[k], "%s", trace);
  return out[k];
}

static void differs(const char *what, const char *a, const char *b) {
  bool d = strcmp(a, b) != 0;
  printf("  %-34s %s\n", what, d ? "changes" : "SAME");
  assert(d);
}

int main(void) {
  const char *std = draw(POSE_STD, 0, 0, 0, false);
  assert(strlen(std) > 0);
  differs("talk (mouth)", std, draw(POSE_STD, 1, EXPR_TALK, 0, false));
  differs("blink", std, draw(POSE_STD, 0, EXPR_BLINK, 0, false));
  differs("wave", draw(POSE_WAVE, 0, 0, 0, false), draw(POSE_WAVE, 1, 0, 0, false));
  differs("wave vs standing", std, draw(POSE_WAVE, 0, 0, 0, false));
  differs("dance frame 0 → 3", draw(POSE_DANCE, 0, 0, 0, false), draw(POSE_DANCE, 3, 0, 0, false));
  differs("walk frame 0 → 2", draw(POSE_WALK, 0, 0, 0, false), draw(POSE_WALK, 2, 0, 0, false));
  differs("hold drink (coffee)", std, draw(POSE_STD, 0, 0, 8, false));
  differs("sip", draw(POSE_STD, 0, 0, 8, false), draw(POSE_STD, 0, 0, 8, true));
  differs("sit", std, draw(POSE_SIT, 0, 0, 0, false));
  differs("bounce hands over ball", draw(POSE_SIT, 0, 0, 0, false), draw(POSE_BOUNCE, 0, 0, 0, false));
  for (int pose = POSE_SIT; pose <= POSE_FLOOR; pose++) {
    static const int gestures[] = {1,4,5,7,8};
    for (int g = 0; g < 5; g++) {
      int upper = avatar_seated_expr(gestures[g], gestures[g] == 1);
      differs("seated gesture", draw(pose, 0, 0, 0, false), draw(pose, 0, upper, 0, false));
      for (int dir = 0; dir < 8; dir+=2) {
        char name[64]; snprintf(name, sizeof name, "h_sit_lg_001_%d_0", (int[]){0,1,2,3,2,1,0,7}[dir]);
        tracked = NULL;
        for (int i = 0; i < people_sprite_count; i++) if (!strcmp(people_sprites[i].name, name)) { tracked = &people_sprites[i]; break; }
        assert(tracked); tracked_drawn = false;
        avatar_draw(test_look, dir, pose, 0, upper, dir, 0, false, 0, 0, 1, 255);
        assert(tracked_drawn); tracked = NULL;
      }
    }
    differs("seated wave frames", draw(pose,0,avatar_seated_expr(0,true),0,false), draw(pose,1,avatar_seated_expr(0,true),0,false));
  }
  // The cache returns exactly the same thing the second time.
  const char *again = draw(POSE_DANCE, 3, 0, 0, false), *again2 = draw(POSE_DANCE, 3, 0, 0, false);
  assert(!strcmp(again, again2));
  trace[0] = 0;
  avatar_draw_swim(0x1234567ull, 2, POSE_SWIM, 0, 0, 2, 0, 0, 1);
  char s0[4096]; snprintf(s0, sizeof s0, "%s", trace);
  trace[0] = 0;
  avatar_draw_swim(0x1234567ull, 2, POSE_SWIM, 2, 0, 2, 0, 0, 1);
  differs("swim frame 0 → 2", s0, trace);
  test_look |= AV_FOUNDER;
  std = draw(POSE_STD, 0, 0, 0, false);
  differs("founder talk", std, draw(POSE_STD, 1, EXPR_TALK, 0, false));
  differs("founder walk", draw(POSE_WALK, 0, 0, 0, false), draw(POSE_WALK, 2, 0, 0, false));
  differs("founder dance", draw(POSE_DANCE, 0, 0, 0, false), draw(POSE_DANCE, 3, 0, 0, false));
  differs("founder wave", draw(POSE_WAVE, 0, 0, 0, false), draw(POSE_WAVE, 1, 0, 0, false));
  differs("founder sip", draw(POSE_STD, 0, 0, 8, false), draw(POSE_STD, 0, 0, 8, true));
  Look later = look_decode(0x1234567ull); later.hair_style = 64;
  test_look = look_encode(&later);
  std = draw(POSE_STD, 0, 0, 0, false);
  assert(ops > 0);
  differs("later wardrobe walking", draw(POSE_WALK, 0, 0, 0, false), draw(POSE_WALK, 2, 0, 0, false));
  int glasses=-1;
  for(int i=0;i<wardrobe_faces_count;i++)if(wardrobe_faces[i].layers&&!strcmp(wardrobe_faces[i].layers,"ea:2:0")){glasses=i;break;}
  assert(glasses>=0);later.face_style=glasses;test_look=look_encode(&later);
  float glasses_y[2];
  for(int frame=0;frame<2;frame++){
    tracked=NULL;
    const char *name=frame?"x_std_ea_2_3_0":"x_std_ea_2_2_0";
    for(int i=0;i<people_sprite_count;i++)if(!strcmp(people_sprites[i].name,name)){tracked=&people_sprites[i];break;}
    assert(tracked);tracked_drawn=false;draw(POSE_DANCE,frame?2:0,0,0,false);
    assert(tracked_drawn);glasses_y[frame]=tracked_y;
  }
  assert(glasses_y[1]==glasses_y[0]-1);tracked=NULL;
  int sleeves=0;
  for(int top=0;top<wardrobe_tops_count;top++) {
    const char *layer=wardrobe_tops[top].layers?strstr(wardrobe_tops[top].layers,"rc:"):NULL;
    if(!layer)continue;
    char model[16];int len=0;layer+=3;
    while(layer[len]&&layer[len]!=':'&&len<15){model[len]=layer[len];len++;}model[len]=0;
    later.shirt_style=top;test_look=look_encode(&later);
    for(int sip=0;sip<2;sip++)for(int dir=0;dir<8;dir++) {
      char name[64];snprintf(name,sizeof name,"x_%s_rc_%s_%d_0",sip?"drk":"crr",model,(int[]){0,1,2,3,2,1,0,7}[dir]);
      tracked=NULL;
      for(int i=0;i<people_sprite_count;i++)if(!strcmp(people_sprites[i].name,name)){tracked=&people_sprites[i];break;}
      if(!tracked)continue;
      for(int pose=POSE_STD;pose<=POSE_SIT;pose++) {
        tracked_drawn=false;avatar_draw(test_look,dir,pose,0,0,dir,8,sip,0,0,1,255);
        assert(tracked_drawn);sleeves++;
      }
    }
  }
  assert(sleeves>0);printf("Raised coat sleeves: %d carrying/sipping poses passed.\n",sleeves);tracked=NULL;
  for (int skin=1; skin<=7; skin++) {
    test_look = (u64)skin << 48;
    for (int pose=POSE_STD; pose<=POSE_DANCE; pose++) for (int dir=0; dir<8; dir++) {
      trace[0]=0; ops=0;
      avatar_draw(test_look,dir,pose,0,0,dir,0,false,0,0,1,255);
      assert(ops==1 && trace[0]);
    }
    test_time=0;const char *first=draw(POSE_WALK,0,0,0,false);
    test_time=125;differs("character skin walk",first,draw(POSE_WALK,0,0,0,false));
    for (int pose=POSE_STD; pose<=POSE_DANCE; pose++) for (int dir=0; dir<8; dir++) {
      int count=character_skin_frames[skin-1][pose][dir];
      double fps=character_skin_fps[skin-1][pose][dir];
      // An exact number of cycles at every exported frame rate, near the 3DS calendar clock.
      u64 period=(u64)count*4000;
      u64 cycles=period*(4000000000000ull/period);
      for (int frame=0; frame<count; frame++) {
        test_time=(u64)ceil(frame*1000/fps);
        trace[0]=0; ops=0;
        avatar_draw(test_look,dir,pose,0,0,dir,0,false,0,0,1,255);
        assert(ops==1 && trace[0]);
        char expected[4096]; snprintf(expected,sizeof expected,"%s",trace);
        test_time+=cycles;
        trace[0]=0; ops=0;
        avatar_draw(test_look,dir,pose,0,0,dir,0,false,0,0,1,255);
        assert(ops==1 && !strcmp(expected,trace));
      }
    }
  }
  printf("OK\n");
  return 0;
}
