#include "SquadAIDebug.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SquadAIController.h"
#include "SquadBrainComponent.h"
#include "SquadCoverSubsystem.h"
#include "SquadKnowledgeComponent.h"

namespace SquadAIDebug
{
	static TAutoConsoleVariable<int32> CVarDebugAgents(
		TEXT("squad.Debug"), 0,
		TEXT("Squad AI agent debug drawing. 0: off, 1: beliefs and cover, 2: also utility scores."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarDebugCover(
		TEXT("squad.DebugCover"), 0,
		TEXT("Draw generated cover points near the player. 0: off, 1: on."),
		ECVF_Cheat);

	static const TCHAR* RoleName(ESquadRole Role)
	{
		switch (Role)
		{
		case ESquadRole::Suppressor: return TEXT("SUPPRESSOR");
		case ESquadRole::Flanker:    return TEXT("FLANKER");
		case ESquadRole::Rifleman:   return TEXT("RIFLEMAN");
		default:                     return TEXT("-");
		}
	}

	void DrawAgent(const ASquadAIController& Agent)
	{
#if ENABLE_DRAW_DEBUG
		const int32 Level = CVarDebugAgents.GetValueOnGameThread();
		const APawn* Pawn = Agent.GetPawn();
		UWorld* World = Agent.GetWorld();
		if (Level <= 0 || !Pawn || !World)
		{
			return;
		}

		const FVector Head = Pawn->GetActorLocation() + FVector(0.f, 0.f, 110.f);

		FString Text = FString::Printf(TEXT("%s | %s\nmorale %.2f  supp %.2f"),
			RoleName(Agent.GetRole()), *Agent.GetBrain()->GetActiveActionName().ToString(), Agent.GetMorale(), Agent.GetSuppression());

		if (Level >= 2)
		{
			for (const FSquadActionScore& Score : Agent.GetBrain()->GetLastScores())
			{
				if (Score.Score > 0.f)
				{
					Text += FString::Printf(TEXT("\n%s%s %.2f"), Score.bActive ? TEXT("> ") : TEXT("  "), *Score.Action.ToString(), Score.Score);
				}
			}
		}
		DrawDebugString(World, Head, Text, nullptr, FColor::White, 0.f, true, 1.f);

		// What the agent believes, not where the target really is.
		if (const FSquadTargetKnowledge* Target = Agent.GetKnowledge()->GetPrimaryTarget())
		{
			const FColor BeliefColor = Target->bVisible ? FColor::Red : FColor::Orange;
			DrawDebugLine(World, Head, Target->LastKnownLocation, BeliefColor, false, 0.f, 0, 1.5f);
			DrawDebugCapsule(World, Target->LastKnownLocation, 90.f, 35.f, FQuat::Identity, BeliefColor, false, 0.f, 0, 1.f);
			if (Target->UncertaintyRadius > 0.f)
			{
				DrawDebugCircle(World, Target->LastKnownLocation - FVector(0.f, 0.f, 85.f), Target->UncertaintyRadius, 32, FColor::Yellow, false, 0.f, 0, 2.f, FVector::ForwardVector, FVector::RightVector, false);
			}
		}

		if (const FSquadCoverPoint* Cover = Agent.GetCurrentCover())
		{
			DrawDebugLine(World, Pawn->GetActorLocation(), Cover->Location + FVector(0.f, 0.f, 20.f), FColor::Cyan, false, 0.f, 0, 1.f);
			DrawDebugSphere(World, Cover->Location + FVector(0.f, 0.f, 20.f), 25.f, 8, FColor::Cyan, false, 0.f, 0, 2.f);
		}
#endif
	}

	void DrawCover(UWorld& World)
	{
#if ENABLE_DRAW_DEBUG
		if (CVarDebugCover.GetValueOnGameThread() <= 0)
		{
			return;
		}

		const APlayerController* Player = World.GetFirstPlayerController();
		const APawn* PlayerPawn = Player ? Player->GetPawn() : nullptr;
		const USquadCoverSubsystem* CoverSystem = World.GetSubsystem<USquadCoverSubsystem>();
		if (!PlayerPawn || !CoverSystem)
		{
			return;
		}

		const FVector Origin = PlayerPawn->GetActorLocation();
		for (const FSquadCoverPoint& Cover : CoverSystem->GetAllCover())
		{
			if (FVector::DistSquared2D(Cover.Location, Origin) > FMath::Square(3000.f))
			{
				continue;
			}

			const FVector Base = Cover.Location + FVector(0.f, 0.f, 10.f);
			const FColor Color = Cover.ClaimedBy.IsValid() ? FColor::Cyan : (Cover.bFullHeight ? FColor::Green : FColor(120, 200, 60));
			DrawDebugDirectionalArrow(&World, Base, Base + Cover.Normal * 40.f, 15.f, Color, false, 0.f, 0, 1.5f);

			if (Cover.bCanPeekLeft)
			{
				DrawDebugPoint(&World, Cover.PeekLeftLocation + FVector(0.f, 0.f, 10.f), 6.f, FColor::Magenta, false, 0.f);
			}
			if (Cover.bCanPeekRight)
			{
				DrawDebugPoint(&World, Cover.PeekRightLocation + FVector(0.f, 0.f, 10.f), 6.f, FColor::Magenta, false, 0.f);
			}
		}
#endif
	}
}
