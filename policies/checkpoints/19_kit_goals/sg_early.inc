/* Strategic goal module "early" (see full_strategic.inc). Empty: the incumbent. */
static void sg_early_register(SGKind *table) { (void)table; }
static int sg_early_select(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 (void)a;(void)o;(void)cfg;(void)s;return 0;
}
