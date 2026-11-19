#include "SquadCoverSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "TimerManager.h"

namespace SquadCover
{
	constexpr float GridStep = 100.f;
	constexpr float ProbeLength = 110.f;
	constexpr float WallOffset = 45.f;
	constexpr float DedupRadius = 70.f;
	constexpr float PeekOffset = 85.f;
	constexpr float MinThreatDistance = 350.f;
	constexpr float AllySpacing = 350.f;
	constexpr int32 MaxBuildAttempts = 60;

	FIntVector HashKey(const FVector& Location)
	{
		return FIntVector(
			FMath::FloorToInt(Location.X / DedupRadius),
			FMath::FloorToInt(Location.Y / DedupRadius),
			FMath::FloorToInt(Location.Z / 200.f));
	}

	FCollisionObjectQueryParams StaticGeometry()
	{
		FCollisionObjectQueryParams Params;
		Params.AddObjectTypesToQuery(ECC_WorldStatic);
		Params.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Params;
	}

	/** Below 0 when outside [Min, Max], 1 inside. */
	float RangeBand(float Distance, float Min, float Max)
	{
		if (Distance < Min)
		{
			return FMath::Clamp(Distance / Min, 0.f, 1.f);
		}
		if (Distance > Max)
		{
			return FMath::Clamp(1.f - (Distance - Max) / Max, 0.f, 1.f);
		}
		return 1.f;
	}
}

void USquadCoverSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The navmesh may be baked (ready now) or generated at runtime (ready later): poll until probing
	// finds cover, and rebuild whenever a runtime generation pass completes.
	InWorld.GetTimerManager().SetTimer(BuildRetryTimer, this, &USquadCoverSubsystem::TryBuildCover, 0.5f, true, 0.1f);
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
	{
		NavSys->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &USquadCoverSubsystem::HandleNavigationGenerated);
	}
}

void USquadCoverSubsystem::TryBuildCover()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	++BuildAttempts;
	const bool bNavReady = NavSys && NavSys->GetDefaultNavDataInstance() && !NavSys->IsNavigationBuildInProgress();

	if (!bNavReady && BuildAttempts < SquadCover::MaxBuildAttempts)
	{
		return;
	}

	BuildCover();

	// An empty result usually means the navmesh exists but has no tiles yet: keep trying.
	if (!Covers.IsEmpty() || BuildAttempts >= SquadCover::MaxBuildAttempts)
	{
		World->GetTimerManager().ClearTimer(BuildRetryTimer);
	}
}

void USquadCoverSubsystem::HandleNavigationGenerated(ANavigationData* NavData)
{
	BuildCover();
}

void USquadCoverSubsystem::BuildCover()
{
	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		UE_LOG(LogSquadAI, Warning, TEXT("Cover: no navigation system, cover not built."));
		return;
	}

	Covers.Reset();
	SpatialHash.Reset();

	const double StartTime = FPlatformTime::Seconds();
	const FCollisionObjectQueryParams ObjectParams = SquadCover::StaticGeometry();
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SquadCoverBuild), false);

	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);

		for (float X = Bounds.Min.X; X <= Bounds.Max.X; X += SquadCover::GridStep)
		{
			for (float Y = Bounds.Min.Y; Y <= Bounds.Max.Y; Y += SquadCover::GridStep)
			{
				FNavLocation NavLocation;
				const FVector Sample(X, Y, Bounds.GetCenter().Z);
				if (!NavSys->ProjectPointToNavigation(Sample, NavLocation, FVector(SquadCover::GridStep * 0.5f, SquadCover::GridStep * 0.5f, Bounds.GetExtent().Z)))
				{
					continue;
				}

				const FVector Chest = NavLocation.Location + FVector(0.f, 0.f, ChestHeight);
				for (int32 Dir = 0; Dir < 8; ++Dir)
				{
					const FVector Direction = FRotator(0.f, Dir * 45.f, 0.f).Vector();
					FHitResult Hit;
					if (World->LineTraceSingleByObjectType(Hit, Chest, Chest + Direction * SquadCover::ProbeLength, ObjectParams, TraceParams))
					{
						AddCoverCandidate(NavLocation.Location, Hit);
					}
				}
			}
		}
	}

	for (FSquadCoverPoint& Cover : Covers)
	{
		ComputePeeks(Cover);
	}

	bBuilt = true;
	UE_LOG(LogSquadAI, Log, TEXT("Cover: built %d cover points in %.1f ms."), Covers.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void USquadCoverSubsystem::AddCoverCandidate(const FVector& NavLocation, const FHitResult& WallHit)
{
	FVector Normal = WallHit.ImpactNormal;
	if (FMath::Abs(Normal.Z) > 0.3f)
	{
		return; // Slopes and ledges are not walls.
	}
	Normal.Z = 0.f;
	Normal.Normalize();

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FVector Desired = WallHit.ImpactPoint + Normal * SquadCover::WallOffset;
	Desired.Z = NavLocation.Z;

	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(Desired, Projected, FVector(30.f, 30.f, 100.f)))
	{
		return;
	}
	const FVector Location = Projected.Location;

	// Reject points that are too close to an existing one.
	const FIntVector Key = SquadCover::HashKey(Location);
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			if (const int32* Existing = SpatialHash.Find(Key + FIntVector(DX, DY, 0)))
			{
				if (FVector::DistSquared(Covers[*Existing].Location, Location) < FMath::Square(SquadCover::DedupRadius))
				{
					return;
				}
			}
		}
	}

	const FCollisionObjectQueryParams ObjectParams = SquadCover::StaticGeometry();
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SquadCoverBuild), false);
	FHitResult Hit;

	// The wall must actually be in front of the final position at chest height.
	const FVector Chest = Location + FVector(0.f, 0.f, ChestHeight);
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, Chest, Chest - Normal * (SquadCover::WallOffset + 60.f), ObjectParams, TraceParams))
	{
		return;
	}

	FSquadCoverPoint Cover;
	Cover.Location = Location;
	Cover.Normal = Normal;

	const FVector Head = Location + FVector(0.f, 0.f, EyeHeight + 15.f);
	Cover.bFullHeight = GetWorld()->LineTraceSingleByObjectType(Hit, Head, Head - Normal * (SquadCover::WallOffset + 60.f), ObjectParams, TraceParams);

	const int32 Index = Covers.Add(Cover);
	SpatialHash.Add(Key, Index);
}

void USquadCoverSubsystem::ComputePeeks(FSquadCoverPoint& Cover) const
{
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const FCollisionObjectQueryParams ObjectParams = SquadCover::StaticGeometry();
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SquadCoverPeek), false);

	// Facing the wall (-Normal), "right" is Up x Forward.
	const FVector Right = FVector::CrossProduct(FVector::UpVector, -Cover.Normal);
	const FVector Chest = Cover.Location + FVector(0.f, 0.f, ChestHeight);

	for (const float Side : { -1.f, 1.f })
	{
		const FVector Offset = Cover.Location + Right * Side * SquadCover::PeekOffset;
		const FVector OffsetChest = Offset + FVector(0.f, 0.f, ChestHeight);

		FHitResult Hit;
		const bool bPathBlocked = GetWorld()->LineTraceSingleByObjectType(Hit, Chest, OffsetChest, ObjectParams, TraceParams);
		const bool bViewBlocked = GetWorld()->LineTraceSingleByObjectType(Hit, OffsetChest, OffsetChest - Cover.Normal * 200.f, ObjectParams, TraceParams);
		if (bPathBlocked || bViewBlocked)
		{
			continue;
		}

		FNavLocation Projected;
		if (!NavSys->ProjectPointToNavigation(Offset, Projected, FVector(30.f, 30.f, 100.f)))
		{
			continue;
		}

		if (Side > 0.f)
		{
			Cover.bCanPeekRight = true;
			Cover.PeekRightLocation = Projected.Location;
		}
		else
		{
			Cover.bCanPeekLeft = true;
			Cover.PeekLeftLocation = Projected.Location;
		}
	}
}

bool USquadCoverSubsystem::HasLineOfSight(const FVector& From, const FVector& To) const
{
	FHitResult Hit;
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SquadCoverLOS), false);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, From, To, SquadCover::StaticGeometry(), TraceParams);
}

bool USquadCoverSubsystem::IsCoverProtectedFrom(int32 Index, const FVector& ThreatLocation) const
{
	const FSquadCoverPoint* Cover = GetCover(Index);
	if (!Cover)
	{
		return false;
	}

	// The wall has to be between us and the threat...
	const FVector ToThreat = (ThreatLocation - Cover->Location).GetSafeNormal2D();
	if (FVector::DotProduct(ToThreat, -Cover->Normal) < 0.15f)
	{
		return false;
	}

	// ...and actually block the threat's view of our chest (and head, for full-height cover).
	const FVector ThreatEye = ThreatLocation + FVector(0.f, 0.f, 70.f);
	if (HasLineOfSight(ThreatEye, Cover->Location + FVector(0.f, 0.f, ChestHeight)))
	{
		return false;
	}

	return !Cover->bFullHeight || !HasLineOfSight(ThreatEye, Cover->Location + FVector(0.f, 0.f, EyeHeight));
}

bool USquadCoverSubsystem::GetPeekLocation(int32 Index, const FVector& ThreatLocation, FVector& OutLocation) const
{
	const FSquadCoverPoint* Cover = GetCover(Index);
	if (!Cover)
	{
		return false;
	}

	const FVector ThreatChest = ThreatLocation + FVector(0.f, 0.f, 30.f);
	const FVector Eye(0.f, 0.f, EyeHeight);

	float BestDistance = TNumericLimits<float>::Max();
	bool bFound = false;

	auto Consider = [&](bool bAvailable, const FVector& Candidate)
	{
		if (!bAvailable || !HasLineOfSight(Candidate + Eye, ThreatChest))
		{
			return;
		}
		const float Distance = FVector::DistSquared(Candidate, ThreatLocation);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			OutLocation = Candidate;
			bFound = true;
		}
	};

	Consider(Cover->bCanPeekLeft, Cover->PeekLeftLocation);
	Consider(Cover->bCanPeekRight, Cover->PeekRightLocation);
	// Waist-high cover: stand up and shoot over it.
	Consider(!Cover->bFullHeight, Cover->Location);

	return bFound;
}

int32 USquadCoverSubsystem::FindBestCover(const FSquadCoverQuery& Query, TArray<FSquadCoverCandidate>* OutCandidates) const
{
	if (OutCandidates)
	{
		OutCandidates->Reset();
	}

	int32 BestIndex = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	const FVector SquadAxis = Query.SquadAxis.GetSafeNormal2D();

	for (int32 Index = 0; Index < Covers.Num(); ++Index)
	{
		const FSquadCoverPoint& Cover = Covers[Index];

		if (Index == Query.ExcludeIndex || Query.ExcludeIndices.Contains(Index))
		{
			continue;
		}

		if (Cover.ClaimedBy.IsValid() && Cover.ClaimedBy.Get() != Query.Querier)
		{
			continue;
		}

		const float TravelDistance = FVector::Dist(Query.AgentLocation, Cover.Location);
		if (TravelDistance > Query.SearchRadius)
		{
			continue;
		}

		const float ThreatDistance = FVector::Dist2D(Cover.Location, Query.ThreatLocation);
		if (ThreatDistance < SquadCover::MinThreatDistance)
		{
			continue;
		}

		FSquadCoverCandidate Candidate;
		Candidate.Index = Index;
		Candidate.bProtected = IsCoverProtectedFrom(Index, Query.ThreatLocation);

		if (!Candidate.bProtected && Query.Intent != ESquadCoverIntent::Flank)
		{
			continue;
		}

		FVector PeekLocation;
		Candidate.bCanAttack = GetPeekLocation(Index, Query.ThreatLocation, PeekLocation);

		const float Travel = TravelDistance / Query.SearchRadius;
		const float Range = SquadCover::RangeBand(ThreatDistance, Query.PreferredRangeMin, Query.PreferredRangeMax);

		float Spacing = 0.f;
		for (const FVector& Ally : Query.AllyLocations)
		{
			const float AllyDistance = FVector::Dist2D(Ally, Cover.Location);
			if (AllyDistance < SquadCover::AllySpacing)
			{
				Spacing += 1.f - AllyDistance / SquadCover::AllySpacing;
			}
		}

		float Score = 0.f;
		switch (Query.Intent)
		{
		case ESquadCoverIntent::Engage:
			Score = 1.f + (Candidate.bCanAttack ? 0.8f : 0.f) + 0.5f * Range - 0.6f * Travel - 0.5f * Spacing;
			break;

		case ESquadCoverIntent::Flank:
		{
			// A flank only works unseen: skip spots too close to the target, or reached by running
			// straight past it.
			constexpr float FlankMinDistance = 600.f;
			constexpr float FlankRouteClearance = 700.f;
			if (ThreatDistance < FlankMinDistance
				|| FMath::PointDistToSegment(Query.ThreatLocation, Query.AgentLocation, Cover.Location) < FlankRouteClearance)
			{
				continue;
			}

			// 0 on the squad's axis, 1 when perpendicular, more when behind the target.
			const FVector FromThreat = (Cover.Location - Query.ThreatLocation).GetSafeNormal2D();
			const float Angle = SquadAxis.IsNearlyZero() ? 0.5f : FMath::Acos(FVector::DotProduct(FromThreat, SquadAxis)) / UE_HALF_PI;
			Score = 1.2f * FMath::Min(Angle, 1.4f) + (Candidate.bProtected ? 0.4f : 0.f) + (Candidate.bCanAttack ? 0.6f : 0.f)
				+ 0.3f * Range - 0.4f * Travel - 0.5f * Spacing;
			break;
		}

		case ESquadCoverIntent::Retreat:
			Score = 1.f + FMath::Clamp(ThreatDistance / (Query.PreferredRangeMax * 1.5f), 0.f, 1.f) - 0.3f * Travel - 0.3f * Spacing;
			break;
		}

		Candidate.Score = Score;
		if (OutCandidates)
		{
			OutCandidates->Add(Candidate);
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

bool USquadCoverSubsystem::ClaimCover(int32 Index, AController* Claimer)
{
	if (!Covers.IsValidIndex(Index))
	{
		return false;
	}

	FSquadCoverPoint& Cover = Covers[Index];
	if (Cover.ClaimedBy.IsValid() && Cover.ClaimedBy.Get() != Claimer)
	{
		return false;
	}

	ReleaseAllCover(Claimer);
	Cover.ClaimedBy = Claimer;
	return true;
}

void USquadCoverSubsystem::ReleaseCover(int32 Index, AController* Claimer)
{
	if (Covers.IsValidIndex(Index) && Covers[Index].ClaimedBy.Get() == Claimer)
	{
		Covers[Index].ClaimedBy.Reset();
	}
}

void USquadCoverSubsystem::ReleaseAllCover(AController* Claimer)
{
	for (FSquadCoverPoint& Cover : Covers)
	{
		if (Cover.ClaimedBy.Get() == Claimer)
		{
			Cover.ClaimedBy.Reset();
		}
	}
}
