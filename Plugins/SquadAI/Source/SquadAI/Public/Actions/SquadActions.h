#pragma once

#include "CoreMinimal.h"
#include "SquadAction.h"
#include "SquadCoverSubsystem.h"
#include "SquadActions.generated.h"

/** Nothing to do: hold position and stay alert. Always available as a fallback. */
UCLASS(meta = (DisplayName = "Idle"))
class SQUADAI_API USquadAction_Idle : public USquadAction
{
	GENERATED_BODY()

public:
	USquadAction_Idle();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
};

/**
 *  Lost contact: sweep the most likely spots. Each agent builds its own route from the predicted
 *  position, the uncertainty radius and the squad's heatmap of where the player likes to be.
 */
UCLASS(meta = (DisplayName = "Search"))
class SQUADAI_API USquadAction_Search : public USquadAction
{
	GENERATED_BODY()

public:
	USquadAction_Search();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) override;

private:
	TArray<FVector> Waypoints;
	int32 WaypointIndex = 0;
	float LookTimer = 0.f;
};

/** Shared logic for every action that runs to a cover point. */
UCLASS(Abstract)
class SQUADAI_API USquadAction_MoveToCover : public USquadAction
{
	GENERATED_BODY()

public:
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) override;

protected:
	virtual void OnArrived(const FSquadContext& Context) {}

	/** Skip the cover the agent currently holds (repositioning). */
	virtual bool ShouldExcludeCurrentCover(const FSquadContext& Context) const { return false; }

	ESquadCoverIntent Intent = ESquadCoverIntent::Engage;
	ESquadBark EnterBark = ESquadBark::Moving;
	bool bBarkOnEnter = false;
	bool bSprint = true;
	/** Keep the weapon on the target while moving if it is visible. */
	bool bAimWhileMoving = true;
	float RangeMin = 700.f;
	float RangeMax = 2000.f;
	float Timeout = 8.f;
	float SearchRadius = 2200.f;

private:
	FVector Destination = FVector::ZeroVector;
	float Elapsed = 0.f;
	bool bHasDestination = false;
};

/** Exposed: get to the best protected spot that still allows shooting back. */
UCLASS(meta = (DisplayName = "Take Cover"))
class SQUADAI_API USquadAction_TakeCover : public USquadAction_MoveToCover
{
	GENERATED_BODY()

public:
	USquadAction_TakeCover();
	virtual float Score(const FSquadContext& Context) const override;

protected:
	virtual bool ShouldExcludeCurrentCover(const FSquadContext& Context) const override { return Context.bInCover; }
};

/** The flanker's move: a cover point on the target's side, away from the squad's main axis. */
UCLASS(meta = (DisplayName = "Flank"))
class SQUADAI_API USquadAction_Flank : public USquadAction_MoveToCover
{
	GENERATED_BODY()

public:
	USquadAction_Flank();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) override;
	virtual bool CanBeInterrupted() const override { return false; }
};

/** Close the distance: the target is reloading, hiding or has gone quiet. */
UCLASS(meta = (DisplayName = "Advance"))
class SQUADAI_API USquadAction_Advance : public USquadAction_MoveToCover
{
	GENERATED_BODY()

public:
	USquadAction_Advance();
	virtual float Score(const FSquadContext& Context) const override;
};

/** Morale broke or health is low: fall back to cover further from the threat. */
UCLASS(meta = (DisplayName = "Retreat"))
class SQUADAI_API USquadAction_Retreat : public USquadAction_MoveToCover
{
	GENERATED_BODY()

public:
	USquadAction_Retreat();
	virtual float Score(const FSquadContext& Context) const override;
	virtual bool CanBeInterrupted() const override { return false; }

protected:
	virtual void OnArrived(const FSquadContext& Context) override;
};

/**
 *  In cover: lean out (or rise over waist-high cover), fire a few bursts, duck back.
 *  Requires a squad attack token, so only a couple of agents shoot at any time.
 */
UCLASS(meta = (DisplayName = "Attack From Cover"))
class SQUADAI_API USquadAction_AttackFromCover : public USquadAction
{
	GENERATED_BODY()

public:
	USquadAction_AttackFromCover();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) override;
	virtual bool CanBeInterrupted() const override;

protected:
	enum class EPhase : uint8 { PeekOut, Shoot, Return };

	/** Suppressive fire aims at the last known position and accepts a wide spread. */
	bool bSuppressive = false;
	FVector2D ShootDuration = FVector2D(1.4f, 2.6f);

private:
	EPhase Phase = EPhase::PeekOut;
	FVector PeekLocation = FVector::ZeroVector;
	FVector CoverLocation = FVector::ZeroVector;
	float PhaseTime = 0.f;
	float ShootTime = 0.f;
	float BurstTimer = 0.f;
	bool bBurstOn = false;
};

/** Pin the target down at its last known position so the flanker can move. */
UCLASS(meta = (DisplayName = "Suppress"))
class SQUADAI_API USquadAction_Suppress : public USquadAction_AttackFromCover
{
	GENERATED_BODY()

public:
	USquadAction_Suppress();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
};

/** Out of ammo: reload, preferably behind cover, and tell the squad. */
UCLASS(meta = (DisplayName = "Reload"))
class SQUADAI_API USquadAction_Reload : public USquadAction
{
	GENERATED_BODY()

public:
	USquadAction_Reload();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
	virtual bool CanBeInterrupted() const override { return false; }

private:
	float Elapsed = 0.f;
};

/** No cover available but the target is in sight: strafe and shoot. */
UCLASS(meta = (DisplayName = "Engage In Open"))
class SQUADAI_API USquadAction_EngageInOpen : public USquadAction
{
	GENERATED_BODY()

public:
	USquadAction_EngageInOpen();
	virtual float Score(const FSquadContext& Context) const override;
	virtual void Enter(const FSquadContext& Context) override;
	virtual ESquadActionStatus Tick(const FSquadContext& Context, float DeltaTime) override;
	virtual void Exit(const FSquadContext& Context, ESquadActionStatus Result) override;

private:
	float StrafeTimer = 0.f;
	float BurstTimer = 0.f;
	bool bBurstOn = false;
};
