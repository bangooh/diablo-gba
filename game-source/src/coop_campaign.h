#pragma once
#include "coop.h"
enum { COOP_CAMPAIGN_ABORTED=0,COOP_CAMPAIGN_APPLIED=1,
       COOP_CAMPAIGN_COMMITTED_LOST=2 };
/* Implemented in coop_campaign.inc after the private menu archive types. */
static int coop_campaign_enter(const CharacterState *local);
/* destination0 is town;1..20 are archived regions. Negative x/y selects stairs. */
static int coop_campaign_travel(unsigned destination,int x,int y);
static int coop_campaign_host_save(void);
static int coop_campaign_personal_save(const CharacterState *hero);
/* Retry only a first write that this session began on verified erased media. */
static int coop_campaign_can_initialize(void);
static void coop_campaign_prepare_scene(void);
