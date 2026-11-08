/* Incumbent policy for HYBRID branch builds (the framework's local comparison).
 *
 * Compiled with -I<incumbent policy dir>, so the #include below picks up the
 * incumbent's full_policy.c/.h/full_goals.inc. Every exported fp_* symbol is
 * renamed to inc_fp_*; internal functions are static, so the candidate and
 * the incumbent link into one binary. branch.c calls inc_hybrid_act after the
 * --handoff condition holds. The incumbent reads the shared FPAgent bytes as
 * its own (shorter or equal) layout: candidates may only append fields. */
#define fp_default_config inc_fp_default_config
#define fp_reset inc_fp_reset
#define fp_reset_cold inc_fp_reset_cold
#define fp_decode_compact inc_fp_decode_compact
#define fp_decode_float inc_fp_decode_float
#define fp_act inc_fp_act
#define fp_observe inc_fp_observe
#define fp_decide inc_fp_decide
#define fp_goal_call_excursion inc_fp_goal_call_excursion
#define fp_goal_return inc_fp_goal_return
#define fp_goal_finish inc_fp_goal_finish
#define fp_skill_name inc_fp_skill_name
#define fp_validate_command inc_fp_validate_command
#define fp_parse_command inc_fp_parse_command
#define fp_set_command inc_fp_set_command
#define fp_clear_command inc_fp_clear_command
#define fp_goal_satisfied inc_fp_goal_satisfied
#define fp_observe_action inc_fp_observe_action
#define fp_replay_action inc_fp_replay_action
#include "full_policy.c"
#include <stddef.h>

int inc_hybrid_act(void *agent,const uint8_t *record,float previous_reward,void *scratch);
size_t inc_hybrid_agent_size(void);
size_t inc_hybrid_scratch_size(void);

int inc_hybrid_act(void *agent,const uint8_t *record,float previous_reward,void *scratch) {
 FPObservation obs;inc_fp_decode_compact(&obs,record);
 FPConfig cfg=inc_fp_default_config();
 return inc_fp_act((FPAgent *)agent,&obs,previous_reward,&cfg,(FPScratch *)scratch);
}
size_t inc_hybrid_agent_size(void) { return sizeof(FPAgent); }
size_t inc_hybrid_scratch_size(void) { return sizeof(FPScratch); }

/* Shared observation ABI for genuine policy-owned invocation handoffs. */
size_t inc_hybrid_runtime_offset(void) {
#ifdef FP_RUNTIME_ABI
 return offsetof(FPAgent,runtime);
#else
 return 0;
#endif
}
size_t inc_hybrid_runtime_size(void) {
#ifdef FP_RUNTIME_ABI
 return sizeof(FPGoalRuntime);
#else
 return 0;
#endif
}
void inc_hybrid_observe(void *agent,const void *observation,float reward,void *scratch) {
#ifdef FP_RUNTIME_ABI
 inc_fp_observe((FPAgent *)agent,(const FPObservation *)observation,reward,(FPScratch *)scratch);
#else
 (void)agent;(void)observation;(void)reward;(void)scratch;
#endif
}
int inc_hybrid_decide(void *agent,const void *observation,void *scratch) {
#ifdef FP_RUNTIME_ABI
 FPConfig cfg=inc_fp_default_config();
 return inc_fp_decide((FPAgent *)agent,(const FPObservation *)observation,&cfg,(FPScratch *)scratch);
#else
 (void)agent;(void)observation;(void)scratch;return -1;
#endif
}
