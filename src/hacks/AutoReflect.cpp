/*
 * AutoReflect.cpp
 *
 *  Created on: Nov 18, 2016
 *      Author: nullifiedcat
 */

#include "AutoReflect.h"

#include "../common.h"
#include "../sdk.h"

namespace hacks { namespace tf { namespace autoreflect {

// Vars for user settings
CatVar enabled(CV_SWITCH, "reflect_enabled", "0", "Auto Reflect", "Master AutoReflect switch");
CatVar idle_only(CV_SWITCH, "reflect_only_idle", "0", "Only when not shooting", "Don't AutoReflect if you're holding M1");
CatVar legit(CV_SWITCH, "reflect_legit", "0", "Legit Reflect", "Only Auto-airblasts projectiles that you can see, doesnt move your crosshair");
CatVar dodgeball(CV_SWITCH, "reflect_dodgeball", "0", "Dodgeball Mode", "Allows auto-reflect to work in dodgeball servers");
	
CatVar stickies(CV_SWITCH, "reflect_stickybombs", "0", "Reflect stickies", "Reflect Stickybombs");
// TODO setup proj sorting
// TODO CatVar bigProj(CV_SWITCH, "reflect_big_projectile", "0", "Reflect big projectiles", "Reflect Rockets");
// TODO CatVar smallProj(CV_SWITCH, "reflect_small_projectile", "0", "Reflect small projectiles", "Reflect Huntsman arrows, Crusaders bolts");
// TODO CatVar miscProj(CV_SWITCH, "reflect_misc_projectile", "0", "Reflect other", "Reflect jarate, milk");

	
// Function called by game for movement
void CreateMove() {
	// Check if user settings allow Auto Reflect
	if (!enabled) return;

	// Check if player is using a flame thrower
	if (g_pLocalPlayer->weapon()->m_iClassID != CL_CLASS(CTFFlameThrower)) return;
	// If user settings allow, return if local player is in attack
	if (idle_only && (g_pUserCmd->buttons & IN_ATTACK)) return;

	// Create some book-keeping vars
	float closest_dist = 0.0f;
	Vector closest_vec;
	// Loop to find the closest entity
	for (int i = 0; i < HIGHEST_ENTITY; i++) {
		
		// Find an ent from the for loops current tick
		CachedEntity* ent = ENTITY(i);
		
		// Check if null or dormant
		if (CE_BAD(ent)) continue;

		// Check if ent should be reflected
		if (!ShouldReflect(ent)) continue;

		// Grab latency
		float latency = g_IEngine->GetNetChannelInfo()->GetLatency(FLOW_INCOMING) + g_IEngine->GetNetChannelInfo()->GetLatency(FLOW_OUTGOING);
		// Create a vector variable to store our velocity
		Vector velocity;
		// Grab Velocity of projectile
		velocity::EstimateAbsVelocity(RAW_ENT(ent), velocity);
		// Predict a vector for where the projectile will be
		Vector predictedProj = ent->m_vecOrigin + (velocity * latency);;
		
		// Dont vischeck if ent is stickybomb or if dodgeball mode is enabled 
		if (!IsEntStickyBomb(ent) && !dodgeball) {
			// Vis check the predicted vector
			if (!IsVectorVisible(g_pLocalPlayer->v_Origin, predictedProj)) continue;
		} /*else {
			// Stickys are weird, we use a different way to vis check them
			// Vis checking stickys are wonky, I quit, just ignore the check >_>
			//if (!VisCheckEntFromEnt(ent, LOCAL_E)) continue;
 		}*/

		// Calculate distance
		float dist = predictedProj.DistToSqr(g_pLocalPlayer->v_Origin);
			
		// If legit mode is on, we check to see if reflecting will work if we dont aim at the projectile
		if (legit) {
			if ( GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, predictedProj) > 85.0f )  continue;
		}
		
		// Compare our info to the others and determine if its the best, if we dont have a projectile already, then we save it here
		if (dist < closest_dist || closest_dist == 0.0f) {
			closest_dist = dist;
			closest_vec = predictedProj;
		}
	}

	// Determine whether the closest projectile is whithin our parameters, preferably 185 units should be our limit, sqr is around the number below
	if (closest_dist == 0 || closest_dist > 34650) return;
	
	// We dont want to aim if legit is true
	if (!legit) {
		// Aim at predicted projectile
		AimAt(g_pLocalPlayer->v_Eye, closest_vec, g_pUserCmd);
		// Use silent angles
		g_pLocalPlayer->bUseSilentAngles = true;
	}
	
	// Airblast
	g_pUserCmd->buttons |= IN_ATTACK2;
	
	// Function is finished, return
	return;
}

// Function to determine whether an ent is good to reflect
bool ShouldReflect(CachedEntity* ent) {
	// Check if the entity is a projectile
	if (ent->m_Type != ENTITY_PROJECTILE) return false;
	logging::Info("Is projectile");
	
	// We dont want to do these checks in dodgeball, it breakes if we do
	if (!dodgeball) {
		// Check if the projectile is your own teams
		if (!ent->m_bEnemy) return false;
		logging::Info("isnt team");

		// If projectile is already deflected, don't deflect it again.
		if (CE_INT(ent, (ent->m_bGrenadeProjectile ?
				/* NetVar for grenades */ netvar.Grenade_iDeflected :
				/* For rockets */ netvar.Rocket_iDeflected))) return false;
		logging::Info("pass already reflect");
	}
	
	// Check if the projectile is a sticky bomb and if the user settings allow it to be reflected
	if (IsEntStickyBomb(ent) && !stickies) return false;
	
	// Target passed the test, return true
	return true;
}
	
bool IsEntStickyBomb(CachedEntity* ent) {
	// Check if the projectile is a sticky bomb
	if (ent->m_iClassID == CL_CLASS(CTFGrenadePipebombProjectile)) {
		if (CE_INT(ent, netvar.iPipeType) == 1) {
			// Ent passed and should be reflected
			return true;
		}
	}
	// Ent didnt pass the test so return false
	return false;
}

}}}
