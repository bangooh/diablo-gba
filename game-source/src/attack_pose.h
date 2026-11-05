#ifndef ATTACK_POSE_H
#define ATTACK_POSE_H
/* GBA adaptation of DevilutionX AnimationInfo's repeated-action distribution.
 * Finish the previous swing visually before showing the next wind-up. Only
 * display frame selection changes: the impact tick and attack duration do not.
 * Legacy repeat mode distributes the unshown tail starting at impact+1.
 * Version0.9.2 finishes full cycles and passes repeat=0 for weapon attacks.
 * No sprite interpolation or extra decoded frame is required. */
static inline unsigned attack_pose_frame(unsigned count,unsigned impact,unsigned duration,
 unsigned impact_tick,unsigned elapsed,unsigned repeat){
 if(!count)return 0;
 if(impact>=count)impact=count-1;
 if(!impact_tick)impact_tick=1;
 if(elapsed<=impact_tick){
  unsigned tail=repeat?count-impact-1:0;
  unsigned frame=elapsed*(tail+impact)/impact_tick;
  return frame<tail?impact+1+frame:frame-tail;
 }
 unsigned recovery=duration>impact_tick+1?duration-impact_tick-1:1;
 unsigned frame=impact+(elapsed-impact_tick)*(count-impact-1)/recovery;
 return frame<count?frame:count-1;
}
#endif
