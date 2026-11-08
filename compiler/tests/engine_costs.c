/* Enumerate actual engine consumption for every action with abundant stock.
 * This catches missing reserve guards when the domain cost table is wrong. */
#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "../../engine/craftax_full.c"
#include <assert.h>

int main(void) {
 CraftaxState *s=calloc(1,sizeof(*s));assert(s);
 for(int action=0;action<=42;action++) {
  memset(s,0,sizeof(*s));s->player_position[0]=s->player_position[1]=24;
  s->player_direction=4;
  for(int r=22;r<=26;r++)for(int c=22;c<=26;c++)s->map[0][r][c]=CRAFTAX_BLOCK_GRASS;
  s->map[0][23][24]=CRAFTAX_BLOCK_CRAFTING_TABLE;
  s->map[0][24][23]=CRAFTAX_BLOCK_FURNACE;
  s->inventory.wood=s->inventory.stone=s->inventory.coal=s->inventory.iron=s->inventory.diamond=50;
  craftax_do_crafting_native(s,action);craftax_place_block_native(s,action);
  printf("%d %d %d %d %d %d\n",action,50-s->inventory.wood,50-s->inventory.stone,
    50-s->inventory.coal,50-s->inventory.iron,50-s->inventory.diamond);
 }
 free(s);return 0;
}
