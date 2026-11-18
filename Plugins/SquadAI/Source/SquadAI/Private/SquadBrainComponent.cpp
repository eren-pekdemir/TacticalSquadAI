#include "SquadBrainComponent.h"

#include "Actions/SquadActions.h"
#include "SquadAIController.h"
#include "SquadCombatant.h"
#include "SquadCoverSubsystem.h"
#include "SquadKnowledgeComponent.h"
#include "SquadSubsystem.h"

void USquadAction::StartCooldown(double Now, ESquadActionStatus Result)
{
	const float Cooldown = Result == ESquadActionStatus::Failed ? CooldownOnFailure : CooldownOnSuccess;
	CooldownUntil = Now + Cooldown;
}

USquadBrainComponent::USquadBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// The default repertoire. Designers can remove, add or re-weight entries per controller.
	Actions.Add(CreateDefaultSubobject<USquadAction_Idle>(TEXT("Idle")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Search>(TEXT("Search")));
	Actions.Add(CreateDefaultSubobject<USquadAction_TakeCover>(TEXT("TakeCover")));
	Actions.Add(CreateDefaultSubobject<USquadAction_AttackFromCover>(TEXT("AttackFromCover")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Suppress>(TEXT("Suppress")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Flank>(TEXT("Flank")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Advance>(TEXT("Advance")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Reload>(TEXT("Reload")));
	Actions.Add(CreateDefaultSubobject<USquadAction_Retreat>(TEXT("Retreat")));
	Actions.Add(CreateDefaultSubobject<USquadAction_EngageInOpen>(TEXT("EngageInOpen")));
}

FName USquadBrainComponent::GetActiveActionName() const
{
	return ActiveAction ? ActiveAction->GetDisplayName() : NAME_None;
}

FSquadContext USquadBrainComponent::BuildContext() const
{
	FSquadContext Context;
	Context.Controller = Cast<ASquadAIController>(GetOwner());
	if (!Context.Controller)
	{
		return Context;
	}

	Context.Pawn = Context.Controller->GetPawn();
	Context.Combatant = Context.Controller->GetCombatant();
	Context.Squad = Context.Controller->GetSquadSubsystem();
	Context.Cover = Context.Controller->GetCoverSubsystem();
	Context.Now = GetWorld()->GetTimeSeconds();

	if (!Context.Pawn || !Context.Combatant)
	{
		return Context;
	}

	Context.Health = Context.Combatant->GetCombatantHealthFraction();
	Context.Ammo = Context.Combatant->GetCombatantAmmoFraction();
	Context.bReloading = Context.Combatant->IsCombatantReloading();
	Context.Morale = Context.Controller->GetMorale();
	Context.Suppression = Context.Controller->GetSuppression();
	Context.Role = Context.Controller->GetRole();

	if (Context.Squad)
	{
		Context.SquadSize = Context.Squad->GetSquadMates(Context.Controller, true).Num();
		Context.bTargetVulnerable = Context.Squad->IsTargetVulnerable(Context.Controller);
	}

	const USquadKnowledgeComponent* Knowledge = Context.Controller->GetKnowledge();
	if (const FSquadTargetKnowledge* Primary = Knowledge->GetPrimaryTarget())
	{
		Context.bHasTarget = true;
		Context.Target = *Primary;
		Context.PredictedTargetLocation = Knowledge->PredictLocation(*Primary);
		Context.DistanceToTarget = FVector::Dist(Context.Pawn->GetActorLocation(), Primary->LastKnownLocation);
		Context.TimeSinceSeen = Primary->LastSeenTime >= 0.0 ? static_cast<float>(Context.Now - Primary->LastSeenTime) : 1000.f;
		Context.TimeSinceConfirmed = static_cast<float>(Context.Now - Primary->LastConfirmedTime);
		Context.bInCover = Context.Controller->IsInCoverFrom(Primary->LastKnownLocation);
		if (Context.bInCover && Context.Cover)
		{
			FVector PeekLocation;
			Context.bCoverHasShot = Context.Cover->GetPeekLocation(Context.Controller->GetCoverIndex(), Primary->LastKnownLocation, PeekLocation);
		}
	}

	return Context;
}

void USquadBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bShutdown)
	{
		return;
	}

	const FSquadContext Context = BuildContext();
	if (!Context.Pawn || !Context.Combatant || !Context.Combatant->IsCombatantAlive())
	{
		return;
	}

	EvaluationTimer -= DeltaTime;
	const bool bCanInterrupt = !ActiveAction || ActiveAction->CanBeInterrupted();
	if (EvaluationTimer <= 0.f && bCanInterrupt)
	{
		// Stagger agents so the squad does not think in lockstep.
		EvaluationTimer = EvaluationInterval * FMath::FRandRange(0.85f, 1.15f);
		Evaluate(Context);
	}

	if (ActiveAction)
	{
		const ESquadActionStatus Status = ActiveAction->Tick(Context, DeltaTime);
		if (Status != ESquadActionStatus::Running)
		{
			FinishActive(Context, Status);
			EvaluationTimer = 0.f; // Decide again immediately.
		}
	}

	LastContext = Context;
}

void USquadBrainComponent::Evaluate(const FSquadContext& Context)
{
	USquadAction* Best = nullptr;
	float BestScore = 0.f;

	LastScores.Reset();
	for (USquadAction* Action : Actions)
	{
		if (!Action)
		{
			continue;
		}

		float Score = 0.f;
		if (!Action->IsOnCooldown(Context.Now))
		{
			Score = FMath::Clamp(Action->Score(Context), 0.f, 1.f) * Action->Weight;
			if (Action == ActiveAction && Score > 0.f)
			{
				Score += MomentumBonus;
			}
		}

		FSquadActionScore& Entry = LastScores.AddDefaulted_GetRef();
		Entry.Action = Action->GetDisplayName();
		Entry.Score = Score;

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Action;
		}
	}

	for (FSquadActionScore& Entry : LastScores)
	{
		Entry.bActive = Best && Entry.Action == Best->GetDisplayName();
	}

	if (Best && Best != ActiveAction)
	{
		SwitchTo(Best, Context);
	}
}

void USquadBrainComponent::SwitchTo(USquadAction* NewAction, const FSquadContext& Context)
{
	if (ActiveAction)
	{
		// Interrupted actions count as successful: they were not wrong, just outranked.
		ActiveAction->Exit(Context, ESquadActionStatus::Succeeded);
	}

	UE_LOG(LogSquadAI, Verbose, TEXT("%s: %s -> %s [%s, morale %.2f, supp %.2f]"),
		*GetNameSafe(Context.Pawn), *GetActiveActionName().ToString(), *NewAction->GetDisplayName().ToString(),
		*UEnum::GetValueAsString(Context.Role), Context.Morale, Context.Suppression);

	ActiveAction = NewAction;
	ActiveAction->Enter(Context);
}

void USquadBrainComponent::FinishActive(const FSquadContext& Context, ESquadActionStatus Result)
{
	if (!ActiveAction)
	{
		return;
	}

	UE_LOG(LogSquadAI, Verbose, TEXT("%s: %s finished (%s)"), *GetNameSafe(Context.Pawn), *ActiveAction->GetDisplayName().ToString(),
		Result == ESquadActionStatus::Succeeded ? TEXT("succeeded") : TEXT("failed"));

	ActiveAction->Exit(Context, Result);
	ActiveAction->StartCooldown(Context.Now, Result);
	ActiveAction = nullptr;
}

void USquadBrainComponent::Shutdown()
{
	if (ActiveAction)
	{
		ActiveAction->Exit(LastContext, ESquadActionStatus::Failed);
		ActiveAction = nullptr;
	}
	bShutdown = true;
	SetComponentTickEnabled(false);
}
