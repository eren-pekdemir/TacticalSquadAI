#include "SquadAIController.h"

#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "SquadAIDebug.h"
#include "SquadBarks.h"
#include "SquadBrainComponent.h"
#include "SquadCombatant.h"
#include "SquadKnowledgeComponent.h"
#include "SquadSubsystem.h"

namespace SquadController
{
	constexpr float ShareInterval = 0.5f;
	constexpr float SuppressionDecay = 0.35f;
	constexpr float MoraleRecovery = 0.02f;
	constexpr float CalmAfterDamage = 4.f;
	constexpr float BarkLifetime = 3.5f;
	constexpr int32 MaxRecentBarks = 3;
	constexpr float AimChestOffset = 35.f;
}

ASquadAIController::ASquadAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	Knowledge = CreateDefaultSubobject<USquadKnowledgeComponent>(TEXT("Knowledge"));
	Brain = CreateDefaultSubobject<USquadBrainComponent>(TEXT("Brain"));

	UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 3500.f;
	SightConfig->LoseSightRadius = 4000.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 250.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
	SightConfig->SetMaxAge(1.f);

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 3000.f;
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;
	HearingConfig->SetMaxAge(2.f);

	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
	DamageConfig->SetMaxAge(3.f);

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->ConfigureSense(*DamageConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());

	SetGenericTeamId(FGenericTeamId(1));
}

// ---- Lifetime -----------------------------------------------------------------------------------

void ASquadAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	bDead = false;
	Morale = 1.f;
	Suppression = 0.f;

	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(InPawn))
	{
		SetGenericTeamId(TeamAgent->GetGenericTeamId());
	}

	if (const ISquadCombatant* Combatant = GetCombatant())
	{
		SquadId = Combatant->GetCombatantSquadId();
	}
	else
	{
		UE_LOG(LogSquadAI, Warning, TEXT("%s possessed %s, which does not implement ISquadCombatant. The squad brain will stay idle."), *GetName(), *GetNameSafe(InPawn));
	}

	GetPerceptionComponent()->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ASquadAIController::HandlePerceptionUpdated);
	GetPerceptionComponent()->RequestStimuliListenerUpdate();
	InPawn->OnTakeAnyDamage.AddUniqueDynamic(this, &ASquadAIController::HandlePawnDamaged);

	if (USquadSubsystem* Squad = GetSquadSubsystem())
	{
		Squad->RegisterAgent(this, SquadId);
	}
}

void ASquadAIController::OnUnPossess()
{
	if (APawn* PreviousPawn = GetPawn())
	{
		PreviousPawn->OnTakeAnyDamage.RemoveDynamic(this, &ASquadAIController::HandlePawnDamaged);
	}

	ReleaseCover();
	if (USquadSubsystem* Squad = GetSquadSubsystem())
	{
		Squad->UnregisterAgent(this);
	}

	Super::OnUnPossess();
}

void ASquadAIController::HandleDeath()
{
	bDead = true;
	Brain->Shutdown();
	StopFiring();
	StopMovement();
	ClearAim();
	ReleaseCover();
	GetPerceptionComponent()->Deactivate();

	if (USquadSubsystem* Squad = GetSquadSubsystem())
	{
		Squad->NotifyAgentDied(this);
	}
}

void ASquadAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDead || !GetPawn())
	{
		return;
	}

	const ISquadCombatant* Combatant = GetCombatant();
	if (Combatant && !Combatant->IsCombatantAlive())
	{
		HandleDeath();
		return;
	}

	UpdateVisibleTargets(DeltaTime);
	UpdateEmotions(DeltaTime);
	UpdateThreatWatch();
	UpdateMovementStyle();

	SquadAIDebug::DrawAgent(*this);

	// Forget barks once they have faded from the screen.
	const double Now = GetWorld()->GetTimeSeconds();
	RecentBarks.RemoveAll([Now](const FSquadBarkEvent& Event) { return Now - Event.Time > SquadController::BarkLifetime; });
}

// ---- Accessors ----------------------------------------------------------------------------------

ISquadCombatant* ASquadAIController::GetCombatant() const
{
	return Cast<ISquadCombatant>(GetPawn());
}

USquadSubsystem* ASquadAIController::GetSquadSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
}

USquadCoverSubsystem* ASquadAIController::GetCoverSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<USquadCoverSubsystem>() : nullptr;
}

bool ASquadAIController::IsAgentAlive() const
{
	const ISquadCombatant* Combatant = GetCombatant();
	return !bDead && Combatant && Combatant->IsCombatantAlive();
}

ESquadRole ASquadAIController::GetRole() const
{
	const USquadSubsystem* Squad = GetSquadSubsystem();
	return Squad ? Squad->GetRole(this) : ESquadRole::None;
}

// ---- Senses -------------------------------------------------------------------------------------

void ASquadAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (bDead || !Actor || GetTeamAttitudeTowards(*Actor) != ETeamAttitude::Hostile)
	{
		return;
	}

	const FAISenseID SenseId = Stimulus.Type;

	if (SenseId == UAISense::GetSenseID<UAISense_Sight>())
	{
		if (!Stimulus.WasSuccessfullySensed())
		{
			Knowledge->ReportLostSight(Actor);
			return;
		}

		const FSquadTargetKnowledge* Previous = Knowledge->Find(Actor);
		const bool bWasKnown = Previous && Previous->Confidence > 0.5f;
		const bool bSeenBefore = Previous && Previous->LastSeenTime >= 0.0;

		Knowledge->ReportSeen(Actor, Actor->GetActorLocation(), Actor->GetVelocity());

		if (!bWasKnown)
		{
			Bark(bSeenBefore ? ESquadBark::Reacquired : ESquadBark::Contact, 4.f);
		}
		return;
	}

	if (SenseId == UAISense::GetSenseID<UAISense_Hearing>())
	{
		Knowledge->ReportSensed(Actor, Stimulus.StimulusLocation, ESquadInfoSource::Hearing, 0.45f, 350.f);

		if (Stimulus.Tag == ReloadNoiseTag)
		{
			if (USquadSubsystem* Squad = GetSquadSubsystem())
			{
				Squad->MarkTargetVulnerable(this, 2.5f);
			}
			Bark(ESquadBark::TargetReloading, 5.f);
		}
		return;
	}

	if (SenseId == UAISense::GetSenseID<UAISense_Damage>())
	{
		Knowledge->ReportSensed(Actor, Stimulus.StimulusLocation, ESquadInfoSource::Damage, 0.75f, 150.f);
	}
}

void ASquadAIController::UpdateVisibleTargets(float DeltaTime)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bShare = Now >= NextShareTime;
	if (bShare)
	{
		NextShareTime = Now + SquadController::ShareInterval;
	}

	USquadSubsystem* Squad = GetSquadSubsystem();
	TArray<AActor*> DeadTargets;
	TArray<AActor*> VisibleTargets;

	for (const auto& Pair : Knowledge->GetAll())
	{
		AActor* Target = Pair.Key.Get();
		if (!Target)
		{
			continue;
		}

		if (const ISquadCombatant* TargetCombatant = Cast<ISquadCombatant>(Target); TargetCombatant && !TargetCombatant->IsCombatantAlive())
		{
			DeadTargets.Add(Target);
		}
		else if (Pair.Value.bVisible)
		{
			VisibleTargets.Add(Target);
		}
	}

	// While the target is in sight we track it for real; once it is gone, only the memory is left.
	for (AActor* Target : VisibleTargets)
	{
		Knowledge->ReportSeen(Target, Target->GetActorLocation(), Target->GetVelocity());
		if (bShare && Squad)
		{
			Squad->ShareKnowledge(this, *Knowledge->Find(Target));
		}
	}

	for (AActor* Dead : DeadTargets)
	{
		const FSquadTargetKnowledge* Record = Knowledge->Find(Dead);
		if (Record && Record->bVisible)
		{
			Bark(ESquadBark::TargetDown, 5.f);
		}
		Knowledge->Forget(Dead);
	}
}

void ASquadAIController::HandlePawnDamaged(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (bDead || Damage <= 0.f)
	{
		return;
	}

	LastDamageTime = GetWorld()->GetTimeSeconds();

	const ISquadCombatant* Combatant = GetCombatant();
	const float Bravery = Combatant ? Combatant->GetCombatantBravery() : 0.75f;
	AddMorale(-(Damage / 100.f) * 0.6f * (1.25f - Bravery));
	AddSuppression(0.3f);

	// Being shot always reveals roughly where the shooter is.
	if (APawn* Shooter = InstigatedBy ? InstigatedBy->GetPawn() : nullptr)
	{
		Knowledge->ReportSensed(Shooter, Shooter->GetActorLocation(), ESquadInfoSource::Damage, 0.8f, 150.f);
	}

	if (Combatant && Combatant->IsCombatantAlive())
	{
		Bark(ESquadBark::Hit, 3.f);
	}
}

// ---- Emotions -----------------------------------------------------------------------------------

void ASquadAIController::AddMorale(float Delta)
{
	Morale = FMath::Clamp(Morale + Delta, 0.f, 1.f);
}

void ASquadAIController::AddSuppression(float Amount)
{
	if (bDead)
	{
		return;
	}

	Suppression = FMath::Clamp(Suppression + Amount, 0.f, 1.f);

	// Being pinned down chips away at the nerves of the less brave.
	const ISquadCombatant* Combatant = GetCombatant();
	const float Bravery = Combatant ? Combatant->GetCombatantBravery() : 0.75f;
	AddMorale(-Amount * 0.06f * (1.2f - Bravery));
}

void ASquadAIController::UpdateEmotions(float DeltaTime)
{
	Suppression = FMath::Max(0.f, Suppression - SquadController::SuppressionDecay * DeltaTime);

	const ISquadCombatant* Combatant = GetCombatant();
	const float Bravery = Combatant ? Combatant->GetCombatantBravery() : 0.75f;
	const float Ceiling = 0.5f + 0.5f * Bravery;
	const bool bCalm = Suppression < 0.2f && GetWorld()->GetTimeSeconds() - LastDamageTime > SquadController::CalmAfterDamage;

	if (bCalm && Morale < Ceiling)
	{
		Morale = FMath::Min(Ceiling, Morale + SquadController::MoraleRecovery * DeltaTime);
	}
}

// ---- Cover --------------------------------------------------------------------------------------

bool ASquadAIController::AcquireCover(ESquadCoverIntent Intent, const FVector& ThreatLocation, float RangeMin, float RangeMax, float SearchRadius, bool bExcludeCurrent)
{
	USquadCoverSubsystem* CoverSystem = GetCoverSubsystem();
	if (!CoverSystem || !CoverSystem->IsBuilt() || !GetPawn())
	{
		return false;
	}

	FSquadCoverQuery Query;
	Query.Querier = this;
	Query.AgentLocation = GetPawn()->GetActorLocation();
	Query.ThreatLocation = ThreatLocation;
	Query.Intent = Intent;
	Query.SearchRadius = SearchRadius;
	Query.PreferredRangeMin = RangeMin;
	Query.PreferredRangeMax = RangeMax;
	Query.ExcludeIndex = bExcludeCurrent ? CoverIndex : INDEX_NONE;

	if (const USquadSubsystem* Squad = GetSquadSubsystem())
	{
		Query.SquadAxis = Squad->GetSquadAxis(this, ThreatLocation);
		for (const ASquadAIController* Mate : Squad->GetSquadMates(this))
		{
			if (const FSquadCoverPoint* MateCover = Mate->GetCurrentCover())
			{
				Query.AllyLocations.Add(MateCover->Location);
			}
			else if (const APawn* MatePawn = Mate->GetPawn())
			{
				Query.AllyLocations.Add(MatePawn->GetActorLocation());
			}
		}
	}

	const int32 Best = CoverSystem->FindBestCover(Query, &LastCoverCandidates);
	if (Best == INDEX_NONE || !CoverSystem->ClaimCover(Best, this))
	{
		UE_LOG(LogSquadAI, Verbose, TEXT("%s: no cover for intent %d (%d candidates scored)"), *GetNameSafe(GetPawn()), static_cast<int32>(Intent), LastCoverCandidates.Num());
		return false;
	}

	CoverIndex = Best;
	return true;
}

void ASquadAIController::ReleaseCover()
{
	if (USquadCoverSubsystem* CoverSystem = GetCoverSubsystem())
	{
		CoverSystem->ReleaseAllCover(this);
	}
	CoverIndex = INDEX_NONE;
}

const FSquadCoverPoint* ASquadAIController::GetCurrentCover() const
{
	const USquadCoverSubsystem* CoverSystem = GetCoverSubsystem();
	return CoverSystem ? CoverSystem->GetCover(CoverIndex) : nullptr;
}

bool ASquadAIController::IsInCoverFrom(const FVector& ThreatLocation) const
{
	const FSquadCoverPoint* Cover = GetCurrentCover();
	return Cover && HasReached(Cover->Location, 70.f) && GetCoverSubsystem()->IsCoverProtectedFrom(CoverIndex, ThreatLocation);
}

// ---- Body ---------------------------------------------------------------------------------------

void ASquadAIController::MoveToPoint(const FVector& Destination, bool bSprint, float AcceptanceRadius)
{
	bSprintRequested = bSprint;
	// bStopOnOverlap is off so the capsule radius is not added to the acceptance radius: agents must
	// actually stand on the cover point. bCanStrafe keeps our own focus, so agents can aim while moving.
	MoveToLocation(Destination, AcceptanceRadius, false, true, true, true);
}

bool ASquadAIController::HasReached(const FVector& Destination, float Tolerance) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	// Pawn location is the capsule centre; compare on the ground plane with a generous height window.
	const FVector Location = ControlledPawn->GetActorLocation();
	return FVector::Dist2D(Location, Destination) <= Tolerance && FMath::Abs(Location.Z - Destination.Z) < 200.f;
}

bool ASquadAIController::IsMoving() const
{
	return GetMoveStatus() == EPathFollowingStatus::Moving;
}

void ASquadAIController::StopMoving()
{
	bSprintRequested = false;
	StopMovement();
}

void ASquadAIController::AimAt(const FVector& Point)
{
	bAiming = true;
	SetFocalPoint(Point, EAIFocusPriority::Gameplay);
}

void ASquadAIController::ClearAim()
{
	bAiming = false;
	ClearFocus(EAIFocusPriority::Gameplay);
}

void ASquadAIController::UpdateThreatWatch()
{
	// Sprinting agents look where they run; everyone else keeps the threat in view. Explicit aims
	// (Gameplay priority) always win over this lower-priority watch focus.
	const FSquadTargetKnowledge* Threat = Knowledge->GetPrimaryTarget();
	const bool bSprinting = bSprintRequested && IsMoving();
	const bool bShouldWatch = !bAiming && !bSprinting && Threat && Threat->Confidence > 0.15f;

	if (bShouldWatch)
	{
		SetFocalPoint(Knowledge->PredictLocation(*Threat) + FVector(0.f, 0.f, SquadController::AimChestOffset), EAIFocusPriority::Default);
		bWatching = true;
	}
	else if (bWatching)
	{
		ClearFocus(EAIFocusPriority::Default);
		bWatching = false;
	}
}

void ASquadAIController::UpdateMovementStyle()
{
	if (ISquadCombatant* Combatant = GetCombatant())
	{
		const bool bFacingFocus = bAiming || bWatching;
		Combatant->SetCombatantStrafing(bFacingFocus);
		Combatant->SetCombatantSprinting(bSprintRequested && !bFacingFocus && IsMoving());
	}
}

bool ASquadAIController::IsFriendlyInLineOfFire(const FVector& AimPoint) const
{
	const APawn* Self = GetPawn();
	const ISquadCombatant* Combatant = GetCombatant();
	if (!Self || !Combatant)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadLineOfFire), false, Self);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Combatant->GetCombatantEyeLocation(), AimPoint, ECC_Visibility, Params))
	{
		return false;
	}

	const AActor* Blocker = Hit.GetActor();
	return Blocker && Blocker != Self && Cast<ISquadCombatant>(Blocker) && FGenericTeamId::GetTeamIdentifier(Blocker) == GetGenericTeamId();
}

void ASquadAIController::FireAt(const FSquadTargetKnowledge& Target, bool bSuppressive)
{
	ISquadCombatant* Combatant = GetCombatant();
	if (!Combatant)
	{
		return;
	}

	FVector AimPoint;
	const AActor* TargetActor = Target.Target.Get();
	if (!bSuppressive && Target.bVisible && TargetActor)
	{
		AimPoint = TargetActor->GetActorLocation() + FVector(0.f, 0.f, SquadController::AimChestOffset);
	}
	else
	{
		// Blind / suppressive fire: rake the area around where we think the target is.
		const FVector Scatter = FMath::VRand() * FMath::Min(Target.UncertaintyRadius * 0.3f, 150.f);
		AimPoint = Target.LastKnownLocation + FVector(Scatter.X, Scatter.Y, SquadController::AimChestOffset);
	}

	AimAt(AimPoint);

	// A professional never shoots through a team mate: hold fire until the lane is clear.
	if (IsFriendlyInLineOfFire(AimPoint))
	{
		Combatant->SetCombatantFiring(false, AimPoint, 0.f);
		return;
	}

	Combatant->SetCombatantFiring(true, AimPoint, ComputeSpread(Target, bSuppressive));
}

void ASquadAIController::StopFiring()
{
	if (ISquadCombatant* Combatant = GetCombatant())
	{
		Combatant->SetCombatantFiring(false, FVector::ZeroVector, 0.f);
	}
}

float ASquadAIController::ComputeSpread(const FSquadTargetKnowledge& Target, bool bSuppressive) const
{
	if (bSuppressive || !Target.bVisible)
	{
		return BaseSpread + SuppressiveFireSpread + SuppressedSpread * Suppression;
	}

	// The first shots after acquiring a target go wide on purpose: the player gets a warning and a
	// chance to react, and the AI reads as human rather than an aimbot.
	const float Settle = FMath::Clamp(Target.VisibleDuration / FMath::Max(AimSettleTime, 0.01f), 0.f, 1.f);
	const float TargetMotion = FMath::Clamp(Target.LastKnownVelocity.Size2D() / 600.f, 0.f, 1.f) * 2.5f;
	const float SelfMotion = GetPawn() && GetPawn()->GetVelocity().Size2D() > 150.f ? 1.5f : 0.f;

	return BaseSpread + UnsettledSpread * (1.f - Settle) + SuppressedSpread * Suppression + TargetMotion + SelfMotion;
}

// ---- Voice --------------------------------------------------------------------------------------

bool ASquadAIController::Bark(ESquadBark Type, float SquadCooldown)
{
	if (bDead && Type != ESquadBark::Hit)
	{
		return false;
	}

	if (USquadSubsystem* Squad = GetSquadSubsystem(); Squad && !Squad->TryBark(this, Type, SquadCooldown))
	{
		return false;
	}

	FSquadBarkEvent Event;
	Event.Bark = Type;
	Event.Line = SquadBarks::PickLine(Type);
	Event.Time = GetWorld()->GetTimeSeconds();

	RecentBarks.Add(Event);
	if (RecentBarks.Num() > SquadController::MaxRecentBarks)
	{
		RecentBarks.RemoveAt(0);
	}

	UE_LOG(LogSquadAI, Verbose, TEXT("%s: \"%s\""), *GetNameSafe(GetPawn()), *Event.Line.ToString());
	OnBark.Broadcast(this, Event);
	return true;
}
