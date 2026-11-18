#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadAITypes.h"
#include "SquadCoverSubsystem.generated.h"

UENUM(BlueprintType)
enum class ESquadCoverIntent : uint8
{
	/** Safe spot that still lets the agent shoot back. */
	Engage,
	/** Spot on the target's side, away from the squad's main axis. */
	Flank,
	/** Safe spot further away from the threat. */
	Retreat
};

struct SQUADAI_API FSquadCoverQuery
{
	AController* Querier = nullptr;
	FVector AgentLocation = FVector::ZeroVector;
	FVector ThreatLocation = FVector::ZeroVector;
	ESquadCoverIntent Intent = ESquadCoverIntent::Engage;
	float SearchRadius = 2200.f;
	float PreferredRangeMin = 600.f;
	float PreferredRangeMax = 2000.f;
	/** Direction from the threat towards the squad. Flank covers want to be far from this axis. */
	FVector SquadAxis = FVector::ZeroVector;
	/** Locations held by squad mates; covers near them are penalised so the squad spreads out. */
	TArray<FVector> AllyLocations;
	/** Cover to skip, e.g. the one the agent is leaving because it has no angle on the target. */
	int32 ExcludeIndex = INDEX_NONE;
};

struct SQUADAI_API FSquadCoverCandidate
{
	int32 Index = INDEX_NONE;
	float Score = 0.f;
	bool bProtected = false;
	bool bCanAttack = false;
};

/**
 *  Procedural cover for the whole level.
 *  Samples the navmesh on a grid, probes for walls in eight directions and records every position
 *  that hides a standing character, plus whether it can lean out left/right or shoot over the top.
 *  No hand-placed cover markup is needed.
 */
class ANavigationData;

UCLASS()
class SQUADAI_API USquadCoverSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** (Re)builds the cover set from the current navmesh. Called automatically once navigation is ready. */
	UFUNCTION(BlueprintCallable, Category = "Squad AI|Cover")
	void BuildCover();

	bool IsBuilt() const { return bBuilt; }

	/** Best cover for the query, or INDEX_NONE. Fills OutCandidates (when given) for debugging. */
	int32 FindBestCover(const FSquadCoverQuery& Query, TArray<FSquadCoverCandidate>* OutCandidates = nullptr) const;

	/** True when the cover still hides its owner from ThreatLocation. */
	bool IsCoverProtectedFrom(int32 Index, const FVector& ThreatLocation) const;

	/** Lean-out spot that has line of sight to the threat, or false if none. */
	bool GetPeekLocation(int32 Index, const FVector& ThreatLocation, FVector& OutLocation) const;

	bool ClaimCover(int32 Index, AController* Claimer);
	void ReleaseCover(int32 Index, AController* Claimer);
	void ReleaseAllCover(AController* Claimer);

	const FSquadCoverPoint* GetCover(int32 Index) const { return Covers.IsValidIndex(Index) ? &Covers[Index] : nullptr; }
	const TArray<FSquadCoverPoint>& GetAllCover() const { return Covers; }

	/** Height of a standing character's eyes and chest above the navmesh. */
	static constexpr float EyeHeight = 160.f;
	static constexpr float ChestHeight = 120.f;

private:

	void TryBuildCover();

	/** Navmesh (re)generated at runtime: the walls may have moved, rebuild the cover set. */
	UFUNCTION()
	void HandleNavigationGenerated(ANavigationData* NavData);
	bool HasLineOfSight(const FVector& From, const FVector& To) const;
	void AddCoverCandidate(const FVector& NavLocation, const FHitResult& WallHit);
	void ComputePeeks(FSquadCoverPoint& Cover) const;

	UPROPERTY()
	TArray<FSquadCoverPoint> Covers;

	TMap<FIntVector, int32> SpatialHash;

	FTimerHandle BuildRetryTimer;
	int32 BuildAttempts = 0;
	bool bBuilt = false;
};
