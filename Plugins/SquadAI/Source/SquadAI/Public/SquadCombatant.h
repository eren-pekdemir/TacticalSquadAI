#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SquadCombatant.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USquadCombatant : public UInterface
{
	GENERATED_BODY()
};

/**
 *  The only contract between the Squad AI plugin and a game.
 *  A pawn that implements this can be driven by ASquadAIController; the plugin never
 *  touches weapons, health or animation directly.
 */
class SQUADAI_API ISquadCombatant
{
	GENERATED_BODY()

public:

	virtual bool IsCombatantAlive() const = 0;

	/** 0..1 */
	virtual float GetCombatantHealthFraction() const = 0;

	/** Where the combatant looks and shoots from. */
	virtual FVector GetCombatantEyeLocation() const = 0;

	/** Hold or release the trigger. AimPoint is where the bullet should go before spread is applied. */
	virtual void SetCombatantFiring(bool bFire, const FVector& AimPoint, float SpreadDegrees) = 0;

	/** 0..1 rounds left in the magazine. */
	virtual float GetCombatantAmmoFraction() const = 0;

	virtual bool IsCombatantReloading() const = 0;

	virtual void StartCombatantReload() = 0;

	/** Run (fast, weapon lowered) or walk tactically (slow, weapon raised). */
	virtual void SetCombatantSprinting(bool bSprint) = 0;

	/** Face the control rotation (strafing while aiming) or face the movement direction. */
	virtual void SetCombatantStrafing(bool bStrafe) = 0;

	/** Effective engagement range of the current weapon, in cm. */
	virtual float GetCombatantEngagementRange() const { return 2500.f; }

	/** Agents with the same squad id coordinate with each other. */
	virtual int32 GetCombatantSquadId() const { return 0; }

	/** 0..1 personality trait. Brave agents hold out longer before falling back. */
	virtual float GetCombatantBravery() const { return 0.75f; }

	/** Kneel (behind low cover, when pinned down) or stand. Optional for the game to support. */
	virtual void SetCombatantCrouched(bool bCrouch) {}

	/** Lean around a corner to shoot: -1 left, +1 right, 0 upright. Optional for the game to support. */
	virtual void SetCombatantLean(float Lean) {}
};
