#include "Actions/SquadActions.h"

#include "NavigationSystem.h"
#include "SquadAIController.h"
#include "SquadCombatant.h"
#include "SquadCoverSubsystem.h"
#include "SquadSubsystem.h"

// ---- Idle ---------------------------------------------------------------------------------------

USquadAction_Idle::USquadAction_Idle()
{
	DisplayName = TEXT("Idle");
}

float USquadAction_Idle::Score(const FSquadContext& Context) const
{
	return 0.05f;
}

void USquadAction_Idle::Enter(const FSquadContext& Context)
{
	Context.Controller->StopMoving();
	Context.Controller->StopFiring();
	Context.Controller->ClearAim();
}

ESquadActionStatus USquadAction_Idle::Tick(const FSquadContext& Context, float DeltaTime)
{
	// Alert but idle: keep watching where the threat was.
	if (Context.bHasTarget)
	{
		Context.Controller->AimAt(Context.Target.LastKnownLocation);
	}
	return ESquadActionStatus::Running;
}

// ---- Search -------------------------------------------------------------------------------------

USquadAction_Search::USquadAction_Search()
{
	DisplayName = TEXT("Search");
	CooldownOnSuccess = 1.f;
	CooldownOnFailure = 1.f;
}

float USquadAction_Search::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget || Context.Target.bVisible || Context.TimeSinceConfirmed < 3.5f)
	{
		return 0.f;
	}

	// The less we know, the more we want to go and look.
	return 0.3f + 0.3f * (1.f - Context.Target.Confidence);
}

void USquadAction_Search::Enter(const FSquadContext& Context)
{
	Waypoints.Reset();
	WaypointIndex = 0;
	LookTimer = 0.f;

	const FVector Predicted = Context.PredictedTargetLocation;
	Waypoints.Add(Predicted);

	// Places the player has liked before, then a random spot inside the "could be anywhere here" circle.
	const float Radius = FMath::Max(400.f, Context.Target.UncertaintyRadius);
	if (Context.Squad)
	{
		TArray<FVector> Hotspots;
		Context.Squad->GetHotspotsNear(Predicted, Radius + 600.f, 2, Hotspots);
		Waypoints.Append(Hotspots);
	}

	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context.Controller->GetWorld()))
	{
		FNavLocation Random;
		if (NavSys->GetRandomReachablePointInRadius(Predicted, Radius, Random))
		{
			Waypoints.Add(Random.Location);
		}
	}

	// Different agents walk the list in different orders so they do not bunch up.
	for (int32 Index = Waypoints.Num() - 1; Index > 1; --Index)
	{
		Waypoints.Swap(Index, FMath::RandRange(1, Index));
	}

	Context.Controller->Bark(Context.Target.LastSeenTime >= 0.0 ? ESquadBark::LostTarget : ESquadBark::Searching, 6.f);
	Context.Controller->MoveToPoint(Waypoints[0], false, 80.f);
}

ESquadActionStatus USquadAction_Search::Tick(const FSquadContext& Context, float DeltaTime)
{
	if (!Waypoints.IsValidIndex(WaypointIndex))
	{
		return ESquadActionStatus::Succeeded;
	}

	const FVector& Waypoint = Waypoints[WaypointIndex];
	ASquadAIController* Controller = Context.Controller;

	if (!Controller->HasReached(Waypoint, 120.f) && Controller->IsMoving())
	{
		// Weapon up, looking where we are heading.
		Controller->AimAt(Waypoint + FVector(0.f, 0.f, 120.f));
		return ESquadActionStatus::Running;
	}

	// At the waypoint (or the path ended): sweep the area before moving on.
	if (LookTimer <= 0.f)
	{
		SweepYaw = Context.Pawn->GetActorRotation().Yaw;
	}
	LookTimer += DeltaTime;
	const FVector Origin = Context.Pawn->GetActorLocation();
	// Check one side, then the other, like a person clearing an area.
	const FVector Sweep = FRotator(0.f, SweepYaw + FMath::Sin(LookTimer * 2.f) * 70.f, 0.f).Vector();
	Controller->AimAt(Origin + Sweep * 500.f + FVector(0.f, 0.f, 60.f));

	if (LookTimer > 1.6f)
	{
		LookTimer = 0.f;
		++WaypointIndex;
		if (Waypoints.IsValidIndex(WaypointIndex))
		{
			Controller->MoveToPoint(Waypoints[WaypointIndex], false, 80.f);
		}
	}

	return ESquadActionStatus::Running;
}

void USquadAction_Search::Exit(const FSquadContext& Context, ESquadActionStatus Result)
{
	Context.Controller->StopMoving();
}

// ---- Move to cover (shared) ---------------------------------------------------------------------

void USquadAction_MoveToCover::Enter(const FSquadContext& Context)
{
	Elapsed = 0.f;
	bHasDestination = false;

	const FVector Threat = Context.Target.LastKnownLocation;
	if (!Context.Controller->AcquireCover(Intent, Threat, RangeMin, RangeMax, SearchRadius, ShouldExcludeCurrentCover(Context)))
	{
		return;
	}

	Destination = Context.Controller->GetCurrentCover()->Location;
	bHasDestination = true;

	if (bBarkOnEnter)
	{
		Context.Controller->Bark(EnterBark, 4.f);
	}
}

ESquadActionStatus USquadAction_MoveToCover::Tick(const FSquadContext& Context, float DeltaTime)
{
	if (!bHasDestination)
	{
		return ESquadActionStatus::Failed;
	}

	Elapsed += DeltaTime;
	ASquadAIController* Controller = Context.Controller;

	if (Controller->HasReached(Destination))
	{
		Controller->StopMoving();
		OnArrived(Context);
		return ESquadActionStatus::Succeeded;
	}

	if (Elapsed > Timeout)
	{
		UE_LOG(LogSquadAI, Verbose, TEXT("%s: %s timed out %.0f cm from cover (move status %d)"), *GetNameSafe(Context.Pawn), *DisplayName.ToString(),
			FVector::Dist2D(Context.Pawn->GetActorLocation(), Destination), static_cast<int32>(Controller->GetMoveStatus()));
		return ESquadActionStatus::Failed;
	}

	// Run with the weapon lowered, or move tactically with the weapon up: on a visible target, or
	// backing away from a threat that is close, instead of turning our back on it.
	constexpr float CloseThreat = 1200.f;
	const bool bAim = bAimWhileMoving && Context.bHasTarget && (Context.Target.bVisible || Context.DistanceToTarget < CloseThreat);
	if (bAim)
	{
		Controller->AimAt(Context.Target.LastKnownLocation);
	}
	else
	{
		Controller->ClearAim();
	}

	if (!Controller->IsMoving())
	{
		Controller->MoveToPoint(Destination, bSprint && !bAim);
	}

	return ESquadActionStatus::Running;
}

void USquadAction_MoveToCover::Exit(const FSquadContext& Context, ESquadActionStatus Result)
{
	if (Result == ESquadActionStatus::Failed)
	{
		Context.Controller->StopMoving();
	}
}

// ---- Take cover ---------------------------------------------------------------------------------

USquadAction_TakeCover::USquadAction_TakeCover()
{
	DisplayName = TEXT("TakeCover");
	CooldownOnSuccess = 1.f;
	CooldownOnFailure = 3.f;
	Timeout = 10.f;
	Intent = ESquadCoverIntent::Engage;
}

float USquadAction_TakeCover::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget || Context.Target.Confidence < 0.25f || !Context.Cover || !Context.Cover->IsBuilt())
	{
		return 0.f;
	}

	// Safe but useless: this cover has no angle on the target. Find one that does.
	if (Context.bInCover)
	{
		return Context.bCoverHasShot || Context.Target.Confidence < 0.4f ? 0.f : 0.45f;
	}

	return 0.55f + 0.25f * Context.Suppression + 0.15f * (1.f - Context.Health) + (Context.Target.bVisible ? 0.1f : 0.f);
}

// ---- Flank --------------------------------------------------------------------------------------

USquadAction_Flank::USquadAction_Flank()
{
	DisplayName = TEXT("Flank");
	Intent = ESquadCoverIntent::Flank;
	EnterBark = ESquadBark::Flanking;
	bBarkOnEnter = true;
	bAimWhileMoving = false;
	RangeMin = 500.f;
	RangeMax = 1500.f;
	SearchRadius = 3000.f;
	Timeout = 12.f;
	CooldownOnSuccess = 6.f;
	CooldownOnFailure = 6.f;
}

float USquadAction_Flank::Score(const FSquadContext& Context) const
{
	if (Context.Role != ESquadRole::Flanker || !Context.bHasTarget || Context.Target.Confidence < 0.3f || Context.Morale < 0.5f)
	{
		return 0.f;
	}
	return 0.85f;
}

void USquadAction_Flank::Exit(const FSquadContext& Context, ESquadActionStatus Result)
{
	Super::Exit(Context, Result);
	if (Context.Squad)
	{
		Context.Squad->NotifyFlankFinished(Context.Controller);
	}
}

// ---- Advance ------------------------------------------------------------------------------------

USquadAction_Advance::USquadAction_Advance()
{
	DisplayName = TEXT("Advance");
	Intent = ESquadCoverIntent::Engage;
	EnterBark = ESquadBark::Moving;
	bBarkOnEnter = true;
	RangeMin = 400.f;
	RangeMax = 900.f;
	CooldownOnSuccess = 6.f;
	CooldownOnFailure = 4.f;
}

float USquadAction_Advance::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget || Context.Role == ESquadRole::Suppressor || Context.Morale < 0.55f || Context.DistanceToTarget < 900.f)
	{
		return 0.f;
	}

	// The target is reloading: everybody push.
	if (Context.bTargetVulnerable)
	{
		return 0.8f;
	}

	// The target went quiet behind cover: creep closer instead of waiting forever.
	if (Context.bInCover && Context.TimeSinceConfirmed > 3.f && Context.TimeSinceConfirmed < 10.f && Context.Target.Confidence > 0.3f)
	{
		return 0.42f * Context.Morale;
	}

	return 0.f;
}

// ---- Retreat ------------------------------------------------------------------------------------

USquadAction_Retreat::USquadAction_Retreat()
{
	DisplayName = TEXT("Retreat");
	Intent = ESquadCoverIntent::Retreat;
	EnterBark = ESquadBark::FallBack;
	bBarkOnEnter = true;
	bAimWhileMoving = false;
	RangeMin = 1200.f;
	RangeMax = 2600.f;
	Timeout = 10.f;
	CooldownOnSuccess = 10.f;
	CooldownOnFailure = 4.f;
}

float USquadAction_Retreat::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget)
	{
		return 0.f;
	}

	constexpr float BreakingPoint = 0.35f;
	if (Context.Morale < BreakingPoint)
	{
		return 0.6f + 0.4f * (1.f - Context.Morale / BreakingPoint);
	}

	if (Context.Health < 0.3f && Context.Suppression > 0.5f)
	{
		return 0.7f;
	}

	return 0.f;
}

void USquadAction_Retreat::OnArrived(const FSquadContext& Context)
{
	// Regrouping at a safe distance steadies the nerves a little.
	Context.Controller->AddMorale(0.2f);
}
