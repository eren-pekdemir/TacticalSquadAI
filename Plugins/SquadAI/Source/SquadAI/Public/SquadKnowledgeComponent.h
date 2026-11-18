#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SquadAITypes.h"
#include "SquadKnowledgeComponent.generated.h"

/**
 *  An agent's memory of hostile actors.
 *  Perception writes observations in; decisions read beliefs out. Nothing in the brain reads the
 *  real position of a target, so the player can break line of sight, relocate and be searched for
 *  in the wrong place.
 */
UCLASS(ClassGroup = (SquadAI), meta = (BlueprintSpawnableComponent))
class SQUADAI_API USquadKnowledgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	USquadKnowledgeComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** First-hand sighting this frame. */
	void ReportSeen(AActor* Target, const FVector& Location, const FVector& Velocity);

	/** Line of sight was lost; the record keeps its last known values and starts to decay. */
	void ReportLostSight(AActor* Target);

	/** A sound or a hit points roughly at the target. */
	void ReportSensed(AActor* Target, const FVector& Location, ESquadInfoSource Source, float InConfidence, float InUncertainty);

	/** Merge information from a squad mate. Never overrides fresher first-hand knowledge. */
	void ReceiveSharedKnowledge(const FSquadTargetKnowledge& Shared);

	void Forget(AActor* Target);

	/** The target the agent should care about most right now, or nullptr. */
	const FSquadTargetKnowledge* GetPrimaryTarget() const;

	const FSquadTargetKnowledge* Find(const AActor* Target) const;

	const TMap<TWeakObjectPtr<AActor>, FSquadTargetKnowledge>& GetAll() const { return Records; }

	/** Where the target probably is now, extrapolated from the last observation and projected on the navmesh. */
	FVector PredictLocation(const FSquadTargetKnowledge& Record, float MaxExtrapolationSeconds = 1.5f) const;

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	bool HasAnyTarget() const { return GetPrimaryTarget() != nullptr; }

	/** Seconds for confidence to drop to ~37% once a target is out of sight. */
	UPROPERTY(EditAnywhere, Category = "Squad AI", meta = (ClampMin = "0.5", Units = "s"))
	float ConfidenceHalfLife = 7.f;

	/** How fast the "where could he be" radius grows while the target is unseen. */
	UPROPERTY(EditAnywhere, Category = "Squad AI", meta = (ClampMin = "0", Units = "cm/s"))
	float UncertaintyGrowth = 180.f;

	/** Records below this confidence are dropped. */
	UPROPERTY(EditAnywhere, Category = "Squad AI", meta = (ClampMin = "0", ClampMax = "1"))
	float ForgetThreshold = 0.04f;

private:

	FSquadTargetKnowledge& FindOrAdd(AActor* Target);

	TMap<TWeakObjectPtr<AActor>, FSquadTargetKnowledge> Records;
};
