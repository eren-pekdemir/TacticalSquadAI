#include "Actions/SquadActions.h"

#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "SquadAIController.h"
#include "SquadCombatant.h"
#include "SquadCoverSubsystem.h"
#include "SquadSubsystem.h"

namespace SquadCombat
{
	constexpr float BurstOnTime = 0.55f;
	constexpr float BurstOffTime = 0.35f;
	constexpr float PeekTimeout = 1.5f;
	constexpr float ReturnTimeout = 1.5f;
	/** Suppression above this makes an agent duck back into cover mid-burst. */
	constexpr float DuckSuppression = 0.7f;

	/** Alternates trigger pulls; returns whether the trigger is down this frame. */
	bool UpdateBurst(float DeltaTime, float& Timer, bool& bOn)
	{
		Timer -= DeltaTime;
		if (Timer <= 0.f)
		{
			bOn = !bOn;
			Timer = (bOn ? BurstOnTime : BurstOffTime) * FMath::FRandRange(0.7f, 1.3f);
		}
		return bOn;
	}
}

// ---- Attack from cover --------------------------------------------------------------------------

USquadAction_AttackFromCover::USquadAction_AttackFromCover()
{
	DisplayName = TEXT("AttackFromCover");
	CooldownOnSuccess = 0.8f;
	CooldownOnFailure = 2.f;
}

float USquadAction_AttackFromCover::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget || !Context.bCoverHasShot || Context.Ammo <= 0.f || Context.bReloading || Context.Target.Confidence < 0.35f)
	{
		return 0.f;
	}

	if (Context.Squad && !Context.Squad->CanTakeAttackToken(Context.Controller))
	{
		return 0.f;
	}

	return 0.5f + 0.3f * Context.Target.Confidence - 0.35f * Context.Suppression;
}

void USquadAction_AttackFromCover::Enter(const FSquadContext& Context)
{
	Phase = EPhase::PeekOut;
	PhaseTime = 0.f;
	BurstTimer = 0.f;
	bBurstOn = false;
	ShootTime = FMath::FRandRange(ShootDuration.X, ShootDuration.Y);

	if (Context.Squad)
	{
		Context.Squad->TakeAttackToken(Context.Controller, ShootTime + 3.f);
	}

	const FSquadCoverPoint* Cover = Context.Controller->GetCurrentCover();
	CoverLocation = Cover ? Cover->Location : Context.Pawn->GetActorLocation();

	if (!Context.Cover || !Context.Cover->GetPeekLocation(Context.Controller->GetCoverIndex(), Context.Target.LastKnownLocation, PeekLocation))
	{
		// No angle from this cover: fire blind over/around it from where we stand.
		PeekLocation = CoverLocation;
	}

	// Never step into a squad mate who is already standing on that spot.
	constexpr float PersonalSpace = 80.f;
	for (TActorIterator<APawn> It(Context.Controller->GetWorld()); It; ++It)
	{
		const APawn* Other = *It;
		if (Other != Context.Pawn && Cast<ISquadCombatant>(Other) && FVector::Dist2D(Other->GetActorLocation(), PeekLocation) < PersonalSpace)
		{
			PeekLocation = CoverLocation;
			break;
		}
	}

	// Stepping sideways out of tall cover: lean towards the open side. Over low cover: just stand up.
	const FVector Step = (PeekLocation - CoverLocation).GetSafeNormal2D();
	const FVector ToThreat = (Context.Target.LastKnownLocation - CoverLocation).GetSafeNormal2D();
	const FVector ThreatRight = FVector::CrossProduct(FVector::UpVector, ToThreat);
	Lean = Step.IsNearlyZero() ? 0.f : FMath::Sign(FVector::DotProduct(Step, ThreatRight));

	Context.Controller->SetExposed(true, Lean);
	Context.Controller->MoveToPoint(PeekLocation, false, 15.f);
}

ESquadActionStatus USquadAction_AttackFromCover::Tick(const FSquadContext& Context, float DeltaTime)
{
	ASquadAIController* Controller = Context.Controller;
	PhaseTime += DeltaTime;

	if (Context.bHasTarget)
	{
		Controller->AimAt(bSuppressive ? Context.Target.LastKnownLocation : Context.PredictedTargetLocation);
	}

	switch (Phase)
	{
	case EPhase::PeekOut:
		if (Controller->HasReached(PeekLocation, 25.f) || PhaseTime > SquadCombat::PeekTimeout)
		{
			Phase = EPhase::Shoot;
			PhaseTime = 0.f;
		}
		break;

	case EPhase::Shoot:
	{
		const bool bLostTarget = !bSuppressive && (!Context.bHasTarget || (!Context.Target.bVisible && Context.TimeSinceSeen > 1.f));
		const bool bDuck = Context.Suppression > SquadCombat::DuckSuppression;
		const bool bEmpty = Context.Ammo <= 0.f;

		if (PhaseTime > ShootTime || bLostTarget || bDuck || bEmpty)
		{
			Controller->StopFiring();
			Controller->SetExposed(false);
			Controller->MoveToPoint(CoverLocation, false, 15.f);
			Phase = EPhase::Return;
			PhaseTime = 0.f;
			break;
		}

		if (SquadCombat::UpdateBurst(DeltaTime, BurstTimer, bBurstOn) && Context.bHasTarget)
		{
			Controller->FireAt(Context.Target, bSuppressive);
		}
		else
		{
			Controller->StopFiring();
		}
		break;
	}

	case EPhase::Return:
		if (Controller->HasReached(CoverLocation, 25.f) || PhaseTime > SquadCombat::ReturnTimeout)
		{
			return ESquadActionStatus::Succeeded;
		}
		break;
	}

	return ESquadActionStatus::Running;
}

void USquadAction_AttackFromCover::Exit(const FSquadContext& Context, ESquadActionStatus Result)
{
	Context.Controller->StopFiring();
	Context.Controller->SetExposed(false);
	if (Context.Squad)
	{
		Context.Squad->ReleaseAttackToken(Context.Controller);
	}
}

bool USquadAction_AttackFromCover::CanBeInterrupted() const
{
	// Committed once out of cover: finish the burst and duck back in.
	return Phase == EPhase::PeekOut && PhaseTime < 0.2f;
}

// ---- Suppress -----------------------------------------------------------------------------------

USquadAction_Suppress::USquadAction_Suppress()
{
	DisplayName = TEXT("Suppress");
	bSuppressive = true;
	ShootDuration = FVector2D(2.5f, 3.5f);
	CooldownOnSuccess = 1.5f;
}

float USquadAction_Suppress::Score(const FSquadContext& Context) const
{
	if (Context.Role != ESquadRole::Suppressor || !Context.bHasTarget || Context.Target.bVisible)
	{
		return 0.f;
	}

	if (!Context.bCoverHasShot || Context.Ammo < 0.2f || Context.Target.Confidence < 0.3f || Context.TimeSinceConfirmed > 10.f)
	{
		return 0.f;
	}

	if (Context.Squad && !Context.Squad->CanTakeAttackToken(Context.Controller))
	{
		return 0.f;
	}

	return 0.7f;
}

void USquadAction_Suppress::Enter(const FSquadContext& Context)
{
	Context.Controller->Bark(ESquadBark::Suppressing, 5.f);
	Super::Enter(Context);
}

// ---- Reload -------------------------------------------------------------------------------------

USquadAction_Reload::USquadAction_Reload()
{
	DisplayName = TEXT("Reload");
	CooldownOnSuccess = 0.5f;
}

float USquadAction_Reload::Score(const FSquadContext& Context) const
{
	if (Context.bReloading || Context.Ammo >= 0.3f)
	{
		return 0.f;
	}

	if (Context.Ammo <= 0.f)
	{
		return 0.95f;
	}

	// Top up when it is safe to do so.
	const bool bSafe = Context.bInCover || !Context.bHasTarget || !Context.Target.bVisible;
	return bSafe ? 0.75f : 0.3f;
}

void USquadAction_Reload::Enter(const FSquadContext& Context)
{
	Elapsed = 0.f;
	Context.Controller->StopFiring();
	Context.Combatant->StartCombatantReload();

	if (Context.SquadSize > 1 && Context.bHasTarget)
	{
		Context.Controller->Bark(ESquadBark::Reloading, 4.f);
	}
}

ESquadActionStatus USquadAction_Reload::Tick(const FSquadContext& Context, float DeltaTime)
{
	Elapsed += DeltaTime;
	if (Elapsed > 0.2f && !Context.Combatant->IsCombatantReloading())
	{
		return ESquadActionStatus::Succeeded;
	}
	return Elapsed > 6.f ? ESquadActionStatus::Failed : ESquadActionStatus::Running;
}

// ---- Engage in the open -------------------------------------------------------------------------

USquadAction_EngageInOpen::USquadAction_EngageInOpen()
{
	DisplayName = TEXT("EngageInOpen");
}

float USquadAction_EngageInOpen::Score(const FSquadContext& Context) const
{
	if (!Context.bHasTarget || !Context.Target.bVisible || Context.bInCover || Context.Ammo <= 0.f)
	{
		return 0.f;
	}

	// Up close there is no time to look for cover.
	return Context.DistanceToTarget < 700.f ? 0.7f : 0.45f;
}

void USquadAction_EngageInOpen::Enter(const FSquadContext& Context)
{
	StrafeTimer = 0.f;
	BurstTimer = 0.f;
	bBurstOn = false;
}

ESquadActionStatus USquadAction_EngageInOpen::Tick(const FSquadContext& Context, float DeltaTime)
{
	if (!Context.bHasTarget || !Context.Target.bVisible)
	{
		return ESquadActionStatus::Succeeded;
	}

	ASquadAIController* Controller = Context.Controller;
	Controller->AimAt(Context.Target.LastKnownLocation);

	// Never stand still in the open: side-step every second or two.
	StrafeTimer -= DeltaTime;
	if (StrafeTimer <= 0.f)
	{
		StrafeTimer = FMath::FRandRange(1.f, 2.f);
		if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld()))
		{
			const FVector ToTarget = (Context.Target.LastKnownLocation - Context.Pawn->GetActorLocation()).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, ToTarget) * (FMath::RandBool() ? 1.f : -1.f);
			FNavLocation Step;
			if (NavSys->ProjectPointToNavigation(Context.Pawn->GetActorLocation() + Side * FMath::FRandRange(200.f, 350.f), Step, FVector(100.f, 100.f, 200.f)))
			{
				Controller->MoveToPoint(Step.Location, false, 30.f);
			}
		}
	}

	const bool bMayShoot = !Context.Squad || Context.Squad->HasAttackToken(Controller) || Context.Squad->TakeAttackToken(Controller, 2.f);
	if (bMayShoot && SquadCombat::UpdateBurst(DeltaTime, BurstTimer, bBurstOn))
	{
		Controller->FireAt(Context.Target, false);
	}
	else
	{
		Controller->StopFiring();
	}

	return ESquadActionStatus::Running;
}

void USquadAction_EngageInOpen::Exit(const FSquadContext& Context, ESquadActionStatus Result)
{
	Context.Controller->StopFiring();
	if (Context.Squad)
	{
		Context.Squad->ReleaseAttackToken(Context.Controller);
	}
}
