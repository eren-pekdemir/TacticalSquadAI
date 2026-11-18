#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SquadAITypes.h"
#include "SquadAction.generated.h"

class ASquadAIController;
class ISquadCombatant;
class USquadCoverSubsystem;
class USquadSubsystem;

/** Snapshot of everything an action needs to score itself. Built once per evaluation. */
struct SQUADAI_API FSquadContext
{
	ASquadAIController* Controller = nullptr;
	APawn* Pawn = nullptr;
	ISquadCombatant* Combatant = nullptr;
	USquadSubsystem* Squad = nullptr;
	USquadCoverSubsystem* Cover = nullptr;

	bool bHasTarget = false;
	FSquadTargetKnowledge Target;
	/** Navmesh-projected guess of where the target is now. */
	FVector PredictedTargetLocation = FVector::ZeroVector;
	float DistanceToTarget = 0.f;
	/** Seconds since the agent itself saw the target. Large if never. */
	float TimeSinceSeen = 1000.f;
	/** Seconds since anyone in the squad confirmed the target's position. */
	float TimeSinceConfirmed = 1000.f;

	ESquadRole Role = ESquadRole::None;
	float Health = 1.f;
	float Ammo = 1.f;
	float Morale = 1.f;
	float Suppression = 0.f;
	bool bInCover = false;
	/** In cover, and a lean-out spot from this cover has line of sight to the target's last known position. */
	bool bCoverHasShot = false;
	bool bReloading = false;
	bool bTargetVulnerable = false;
	int32 SquadSize = 1;
	double Now = 0.0;
};

/**
 *  One thing an agent can do. Each action scores how much it wants to run in the current context
 *  (0..1, utility AI); the brain runs the highest scoring one. Scores are kept for the debug view,
 *  so every decision the AI makes can be explained on screen.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class SQUADAI_API USquadAction : public UObject
{
	GENERATED_BODY()

public:

	virtual float Score(const FSquadContext& Context) const { return 0.f; }
	virtual void Enter(const FSquadContext& Context) {}
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) { return ESquadActionStatus::Succeeded; }
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) {}

	/** Uninterruptible actions finish before the brain switches (reloading, mid-peek). */
	virtual bool CanBeInterrupted() const { return true; }

	bool IsOnCooldown(double Now) const { return Now < CooldownUntil; }
	void StartCooldown(double Now, ESquadActionStatus Result);

	FName GetDisplayName() const { return DisplayName; }

	/** Multiplier on the raw score, so designers can tune personalities without code. */
	UPROPERTY(EditAnywhere, Category = "Utility", meta = (ClampMin = "0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, Category = "Utility", meta = (ClampMin = "0", Units = "s"))
	float CooldownOnSuccess = 0.f;

	UPROPERTY(EditAnywhere, Category = "Utility", meta = (ClampMin = "0", Units = "s"))
	float CooldownOnFailure = 2.f;

protected:

	UPROPERTY(EditAnywhere, Category = "Utility")
	FName DisplayName = TEXT("Action");

	double CooldownUntil = 0.0;
};
