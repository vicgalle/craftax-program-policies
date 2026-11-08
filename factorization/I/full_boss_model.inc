/* Necromancer submodel: public wave and exploration progress on the boss
 * floor. The choices that read it (healing reserve, potion identification,
 * the search-repair option, wave-five sniping) are the program's
 * (p/combat.tr: bm_healing_needed, bm_prepare, bm_search, bm_snipe).
 * No simulator state or evaluation seed is available to this module. */

static void bm_observe(FPAgent *a,const FPObservation *o,const FPScratch *s) {
 if(o->level!=8)return;
 if(!a->bm_init || a->bm_wave!=a->boss_hits) {
  a->bm_init=1;a->bm_wave=a->boss_hits;a->bm_wave_t=a->bm_progress_t=a->t;
  a->bm_active=0;
 }
 int novel=0;
 for(int i=0;i<s->visible_count;i++) {
  int p=s->visible_cells[i];unsigned char bit=(unsigned char)(1u<<(p&7));
  if(!(a->bm_seen[p>>3]&bit)){a->bm_seen[p>>3]|=bit;novel++;}
 }
 if(novel){a->bm_progress_t=a->t;a->bm_active=0;}
}
