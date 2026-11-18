#include "SquadKnowledgeComponent.h"

#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY(LogSquadAI);

USquadKnowledgeComponent::USquadKnowledgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
}

void USquadKnowledgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Decay = FMath::Exp(-DeltaTime / FMath::Max(ConfidenceHalfLife, 0.1f));

	for (auto It = Records.CreateIterator(); It; ++It)
	{
		FSquadTargetKnowledge& Record = It.Value();

		if (!Record.Target.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		if (Record.bVisible)
		{
			Record.VisibleDuration += DeltaTime;
			Record.UncertaintyRadius = 0.f;
			continue;
		}

		Record.VisibleDuration = 0.f;
		Record.Confidence *= Decay;
		Record.UncertaintyRadius += UncertaintyGrowth * DeltaTime;

		if (Record.Confidence < ForgetThreshold)
		{
			It.RemoveCurrent();
		}
	}
}

FSquadTargetKnowledge& USquadKnowledgeComponent::FindOrAdd(AActor* Target)
{
	FSquadTargetKnowledge& Record = Records.FindOrAdd(Target);
	Record.Target = Target;
	return Record;
}

void USquadKnowledgeComponent::ReportSeen(AActor* Target, const FVector& Location, const FVector& Velocity)
{
	if (!Target)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	FSquadTargetKnowledge& Record = FindOrAdd(Target);
	Record.LastKnownLocation = Location;
	Record.LastKnownVelocity = Velocity;
	Record.LastConfirmedTime = Now;
	Record.LastSeenTime = Now;
	Record.Confidence = 1.f;
	Record.UncertaintyRadius = 0.f;
	Record.bVisible = true;
	Record.Source = ESquadInfoSource::Sight;
}

void USquadKnowledgeComponent::ReportLostSight(AActor* Target)
{
	if (FSquadTargetKnowledge* Record = Records.Find(Target))
	{
		Record->bVisible = false;
	}
}

void USquadKnowledgeComponent::ReportSensed(AActor* Target, const FVector& Location, ESquadInfoSource Source, float InConfidence, float InUncertainty)
{
	if (!Target)
	{
		return;
	}

	FSquadTargetKnowledge& Record = FindOrAdd(Target);

	// A vague cue never overrides what the agent is looking at right now.
	if (Record.bVisible)
	{
		return;
	}

	if (InConfidence >= Record.Confidence || Record.UncertaintyRadius > InUncertainty)
	{
		Record.LastKnownLocation = Location;
		Record.LastKnownVelocity = FVector::ZeroVector;
		Record.UncertaintyRadius = InUncertainty;
		Record.Source = Source;
	}

	Record.Confidence = FMath::Max(Record.Confidence, InConfidence);
	Record.LastConfirmedTime = GetWorld()->GetTimeSeconds();
}

void USquadKnowledgeComponent::ReceiveSharedKnowledge(const FSquadTargetKnowledge& Shared)
{
	AActor* Target = Shared.Target.Get();
	if (!Target)
	{
		return;
	}

	FSquadTargetKnowledge& Record = FindOrAdd(Target);
	if (Record.bVisible || Record.LastConfirmedTime >= Shared.LastConfirmedTime)
	{
		return;
	}

	// Second-hand information is slightly less trusted than the teller's own.
	Record.LastKnownLocation = Shared.LastKnownLocation;
	Record.LastKnownVelocity = Shared.LastKnownVelocity;
	Record.LastConfirmedTime = Shared.LastConfirmedTime;
	Record.Confidence = FMath::Max(Record.Confidence, Shared.Confidence * 0.85f);
	Record.UncertaintyRadius = Shared.UncertaintyRadius + 100.f;
	Record.Source = ESquadInfoSource::Squad;
}

void USquadKnowledgeComponent::Forget(AActor* Target)
{
	Records.Remove(Target);
}

const FSquadTargetKnowledge* USquadKnowledgeComponent::GetPrimaryTarget() const
{
	const AActor* Owner = GetOwner();
	const APawn* OwnerPawn = Owner ? (Cast<AController>(Owner) ? Cast<AController>(Owner)->GetPawn() : Cast<APawn>(Owner)) : nullptr;
	const FVector Origin = OwnerPawn ? OwnerPawn->GetActorLocation() : FVector::ZeroVector;

	const FSquadTargetKnowledge* Best = nullptr;
	float BestThreat = 0.f;

	for (const auto& Pair : Records)
	{
		const FSquadTargetKnowledge& Record = Pair.Value;
		if (!Record.IsValid())
		{
			continue;
		}

		// Threat = how sure we are * how close it is, with a strong bias for what we can see.
		const float Distance = FVector::Dist(Origin, Record.LastKnownLocation);
		const float Proximity = 1.f / (1.f + Distance / 1500.f);
		const float Threat = Record.Confidence * Proximity * (Record.bVisible ? 2.f : 1.f);

		if (Threat > BestThreat)
		{
			BestThreat = Threat;
			Best = &Record;
		}
	}

	return Best;
}

const FSquadTargetKnowledge* USquadKnowledgeComponent::Find(const AActor* Target) const
{
	return Records.Find(Target);
}

FVector USquadKnowledgeComponent::PredictLocation(const FSquadTargetKnowledge& Record, float MaxExtrapolationSeconds) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Min(static_cast<float>(Now - Record.LastConfirmedTime), MaxExtrapolationSeconds);
	const FVector Guess = Record.LastKnownLocation + Record.LastKnownVelocity * Elapsed;

	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Guess, Projected, FVector(200.f, 200.f, 300.f)))
		{
			return Projected.Location;
		}
	}

	return Record.LastKnownLocation;
}
