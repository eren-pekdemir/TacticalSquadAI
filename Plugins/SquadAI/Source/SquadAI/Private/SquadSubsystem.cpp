#include "SquadSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Pawn.h"
#include "SquadAIController.h"
#include "SquadAIDebug.h"
#include "SquadKnowledgeComponent.h"

namespace SquadDirector
{
	constexpr float RoleUpdateInterval = 1.f;
	constexpr float FlankerTimeout = 14.f;
	constexpr float FlankCooldown = 8.f;
	constexpr float TokenCooldown = 0.8f;
	constexpr float SuppressionRadius = 250.f;
	constexpr float SuppressionPerBullet = 0.35f;
	constexpr float DeathMoraleRadius = 2000.f;
	constexpr float HeatmapCellSize = 400.f;
	constexpr float HeatmapSampleInterval = 0.5f;

	FIntPoint HeatCell(const FVector& Location)
	{
		return FIntPoint(FMath::FloorToInt(Location.X / HeatmapCellSize), FMath::FloorToInt(Location.Y / HeatmapCellSize));
	}
}

void USquadSubsystem::RegisterAgent(ASquadAIController* Agent, int32 SquadId)
{
	if (!Agent)
	{
		return;
	}

	UnregisterAgent(Agent);

	FSquadState& Squad = Squads.FindOrAdd(SquadId);
	Squad.Members.Add(Agent);
	Squad.Roles.Add(Agent, ESquadRole::Rifleman);
	Squad.InitialSize = FMath::Max(Squad.InitialSize, Squad.Members.Num());
	AgentSquad.Add(Agent, SquadId);
}

void USquadSubsystem::UnregisterAgent(ASquadAIController* Agent)
{
	const int32* SquadId = AgentSquad.Find(Agent);
	if (!SquadId)
	{
		return;
	}

	if (FSquadState* Squad = Squads.Find(*SquadId))
	{
		Squad->Members.Remove(Agent);
		Squad->Roles.Remove(Agent);
		Squad->AttackTokens.Remove(Agent);
		if (Squad->Flanker.Get() == Agent)
		{
			Squad->Flanker.Reset();
		}
	}

	AgentSquad.Remove(Agent);
}

FSquadState* USquadSubsystem::FindSquadOf(const ASquadAIController* Agent)
{
	const int32* SquadId = AgentSquad.Find(const_cast<ASquadAIController*>(Agent));
	return SquadId ? Squads.Find(*SquadId) : nullptr;
}

const FSquadState* USquadSubsystem::FindSquadOf(const ASquadAIController* Agent) const
{
	const int32* SquadId = AgentSquad.Find(const_cast<ASquadAIController*>(Agent));
	return SquadId ? Squads.Find(*SquadId) : nullptr;
}

TArray<ASquadAIController*> USquadSubsystem::GetSquadMates(const ASquadAIController* Agent, bool bIncludeSelf) const
{
	TArray<ASquadAIController*> Result;
	if (const FSquadState* Squad = FindSquadOf(Agent))
	{
		for (const TWeakObjectPtr<ASquadAIController>& Member : Squad->Members)
		{
			ASquadAIController* Mate = Member.Get();
			if (Mate && Mate->IsAgentAlive() && (bIncludeSelf || Mate != Agent))
			{
				Result.Add(Mate);
			}
		}
	}
	return Result;
}

void USquadSubsystem::NotifyAgentDied(ASquadAIController* Agent)
{
	FSquadState* Squad = FindSquadOf(Agent);
	if (!Squad)
	{
		return;
	}

	++Squad->Casualties;
	const APawn* DeadPawn = Agent->GetPawn();
	const FVector DeathLocation = DeadPawn ? DeadPawn->GetActorLocation() : FVector::ZeroVector;
	const float LossRatio = Squad->InitialSize > 0 ? static_cast<float>(Squad->Casualties) / Squad->InitialSize : 0.f;

	bool bCalledOut = false;
	for (ASquadAIController* Mate : GetSquadMates(Agent))
	{
		const APawn* MatePawn = Mate->GetPawn();
		if (!MatePawn)
		{
			continue;
		}

		// Seeing a friend drop up close hurts more; losing half the squad hurts everyone.
		const float Distance = FVector::Dist(MatePawn->GetActorLocation(), DeathLocation);
		const float Proximity = 1.f - FMath::Clamp(Distance / SquadDirector::DeathMoraleRadius, 0.f, 1.f);
		Mate->AddMorale(-(0.15f + 0.25f * Proximity + 0.2f * LossRatio));

		if (!bCalledOut && Distance < SquadDirector::DeathMoraleRadius)
		{
			bCalledOut = Mate->Bark(ESquadBark::ManDown, 2.f);
		}
	}

	UnregisterAgent(Agent);
}

void USquadSubsystem::ShareKnowledge(ASquadAIController* From, const FSquadTargetKnowledge& Knowledge)
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (ASquadAIController* Mate : GetSquadMates(From))
	{
		FSquadMessage& Message = PendingMessages.AddDefaulted_GetRef();
		Message.Recipient = Mate;
		Message.Knowledge = Knowledge;
		Message.DeliverTime = Now + FMath::FRandRange(CommsDelay.X, CommsDelay.Y);
	}
}

void USquadSubsystem::MarkTargetVulnerable(ASquadAIController* Reporter, float Duration)
{
	if (FSquadState* Squad = FindSquadOf(Reporter))
	{
		Squad->TargetVulnerableUntil = GetWorld()->GetTimeSeconds() + Duration;
	}
}

bool USquadSubsystem::IsTargetVulnerable(const ASquadAIController* Agent) const
{
	const FSquadState* Squad = FindSquadOf(Agent);
	return Squad && Squad->TargetVulnerableUntil > GetWorld()->GetTimeSeconds();
}

ESquadRole USquadSubsystem::GetRole(const ASquadAIController* Agent) const
{
	if (const FSquadState* Squad = FindSquadOf(Agent))
	{
		if (const ESquadRole* Role = Squad->Roles.Find(const_cast<ASquadAIController*>(Agent)))
		{
			return *Role;
		}
	}
	return ESquadRole::None;
}

void USquadSubsystem::NotifyFlankFinished(ASquadAIController* Agent)
{
	FSquadState* Squad = FindSquadOf(Agent);
	if (Squad && Squad->Flanker.Get() == Agent)
	{
		Squad->Flanker.Reset();
		Squad->FlankCooldownUntil = GetWorld()->GetTimeSeconds() + SquadDirector::FlankCooldown;
		Squad->Roles.Add(Agent, ESquadRole::Rifleman);
	}
}

bool USquadSubsystem::CanTakeAttackToken(const ASquadAIController* Agent) const
{
	const FSquadState* Squad = FindSquadOf(Agent);
	if (!Squad)
	{
		return true;
	}

	TWeakObjectPtr<ASquadAIController> Key(const_cast<ASquadAIController*>(Agent));
	if (Squad->AttackTokens.Contains(Key))
	{
		return true;
	}

	const double* Cooldown = Squad->TokenCooldowns.Find(Key);
	if (Cooldown && *Cooldown > GetWorld()->GetTimeSeconds())
	{
		return false;
	}

	return Squad->AttackTokens.Num() < MaxAttackTokens;
}

bool USquadSubsystem::TakeAttackToken(ASquadAIController* Agent, float Duration)
{
	FSquadState* Squad = FindSquadOf(Agent);
	if (!Squad)
	{
		return true;
	}
	if (!CanTakeAttackToken(Agent))
	{
		return false;
	}

	Squad->AttackTokens.Add(Agent, GetWorld()->GetTimeSeconds() + Duration);
	return true;
}

void USquadSubsystem::ReleaseAttackToken(ASquadAIController* Agent)
{
	if (FSquadState* Squad = FindSquadOf(Agent))
	{
		if (Squad->AttackTokens.Remove(Agent) > 0)
		{
			Squad->TokenCooldowns.Add(Agent, GetWorld()->GetTimeSeconds() + SquadDirector::TokenCooldown);
		}
	}
}

bool USquadSubsystem::HasAttackToken(const ASquadAIController* Agent) const
{
	const FSquadState* Squad = FindSquadOf(Agent);
	return !Squad || Squad->AttackTokens.Contains(const_cast<ASquadAIController*>(Agent));
}

bool USquadSubsystem::TryBark(ASquadAIController* Agent, ESquadBark Bark, float Cooldown)
{
	FSquadState* Squad = FindSquadOf(Agent);
	if (!Squad)
	{
		return true;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	double& Last = Squad->LastBarkTime.FindOrAdd(Bark, -1000.0);
	if (Now - Last < Cooldown)
	{
		return false;
	}

	Last = Now;
	return true;
}

FVector USquadSubsystem::GetSquadAxis(const ASquadAIController* Agent, const FVector& ThreatLocation) const
{
	FVector Centroid = FVector::ZeroVector;
	int32 Count = 0;
	for (ASquadAIController* Mate : GetSquadMates(Agent, true))
	{
		if (const APawn* Pawn = Mate->GetPawn())
		{
			Centroid += Pawn->GetActorLocation();
			++Count;
		}
	}

	return Count > 0 ? (Centroid / Count - ThreatLocation).GetSafeNormal2D() : FVector::ZeroVector;
}

void USquadSubsystem::ReportBulletPath(AActor* Shooter, const FVector& Start, const FVector& End)
{
	const FGenericTeamId ShooterTeam = FGenericTeamId::GetTeamIdentifier(Shooter);

	for (const auto& Pair : AgentSquad)
	{
		ASquadAIController* Agent = Pair.Key.Get();
		const APawn* Pawn = Agent ? Agent->GetPawn() : nullptr;
		if (!Pawn || Pawn == Shooter || !Agent->IsAgentAlive() || Agent->GetGenericTeamId() == ShooterTeam)
		{
			continue;
		}

		const FVector Head = Pawn->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		const float Distance = FMath::PointDistToSegment(Head, Start, End);
		if (Distance < SquadDirector::SuppressionRadius)
		{
			Agent->AddSuppression(SquadDirector::SuppressionPerBullet * (1.f - Distance / SquadDirector::SuppressionRadius));
		}
	}
}

void USquadSubsystem::GetHotspotsNear(const FVector& Origin, float Radius, int32 MaxCount, TArray<FVector>& OutLocations) const
{
	TArray<TPair<float, FVector>> Candidates;
	for (const auto& Cell : HostileHeatmap)
	{
		const FVector Center((Cell.Key.X + 0.5f) * SquadDirector::HeatmapCellSize, (Cell.Key.Y + 0.5f) * SquadDirector::HeatmapCellSize, Origin.Z);
		const float Distance = FVector::Dist2D(Center, Origin);
		if (Distance <= Radius)
		{
			// Hot and close beats hot and far.
			Candidates.Emplace(Cell.Value / (1.f + Distance / 1000.f), Center);
		}
	}

	Candidates.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B) { return A.Key > B.Key; });
	for (int32 Index = 0; Index < FMath::Min(MaxCount, Candidates.Num()); ++Index)
	{
		OutLocations.Add(Candidates[Index].Value);
	}
}

void USquadSubsystem::Tick(float DeltaTime)
{
	const double Now = GetWorld()->GetTimeSeconds();

	// Deliver radio messages whose delay has elapsed.
	for (int32 Index = PendingMessages.Num() - 1; Index >= 0; --Index)
	{
		const FSquadMessage& Message = PendingMessages[Index];
		if (Message.DeliverTime > Now)
		{
			continue;
		}

		ASquadAIController* Recipient = Message.Recipient.Get();
		if (Recipient && Recipient->IsAgentAlive())
		{
			Recipient->GetKnowledge()->ReceiveSharedKnowledge(Message.Knowledge);
		}
		PendingMessages.RemoveAtSwap(Index);
	}

	for (auto& Pair : Squads)
	{
		UpdateTokens(Pair.Value, Now);
	}

	if (Now >= NextRoleUpdate)
	{
		NextRoleUpdate = Now + SquadDirector::RoleUpdateInterval;
		for (auto& Pair : Squads)
		{
			UpdateRoles(Pair.Value, Now);
		}
	}

	RecordHostilePositions(DeltaTime);
	SquadAIDebug::DrawCover(*GetWorld());
}

void USquadSubsystem::UpdateTokens(FSquadState& Squad, double Now)
{
	for (auto It = Squad.AttackTokens.CreateIterator(); It; ++It)
	{
		const ASquadAIController* Holder = It.Key().Get();
		if (!Holder || !Holder->IsAgentAlive() || It.Value() < Now)
		{
			Squad.TokenCooldowns.Add(It.Key(), Now + SquadDirector::TokenCooldown);
			It.RemoveCurrent();
		}
	}
}

void USquadSubsystem::UpdateRoles(FSquadState& Squad, double Now)
{
	TArray<ASquadAIController*> Alive;
	for (const TWeakObjectPtr<ASquadAIController>& Member : Squad.Members)
	{
		if (ASquadAIController* Agent = Member.Get(); Agent && Agent->IsAgentAlive() && Agent->GetPawn())
		{
			Alive.Add(Agent);
		}
	}

	// Find the squad's shared picture of the enemy.
	const FSquadTargetKnowledge* Threat = nullptr;
	for (ASquadAIController* Agent : Alive)
	{
		const FSquadTargetKnowledge* Primary = Agent->GetKnowledge()->GetPrimaryTarget();
		if (Primary && (!Threat || Primary->Confidence > Threat->Confidence))
		{
			Threat = Primary;
		}
	}

	if (!Threat || Alive.Num() < 2)
	{
		for (ASquadAIController* Agent : Alive)
		{
			Squad.Roles.Add(Agent, ESquadRole::Rifleman);
		}
		Squad.Flanker.Reset();
		return;
	}

	const FVector ThreatLocation = Threat->LastKnownLocation;

	// Suppressor: the agent best placed to keep the target's head down right now.
	ASquadAIController* Suppressor = nullptr;
	float BestSuppressScore = -1.f;
	for (ASquadAIController* Agent : Alive)
	{
		const FSquadTargetKnowledge* Record = Agent->GetKnowledge()->Find(Threat->Target.Get());
		const bool bSees = Record && Record->bVisible;
		const float Score = (bSees ? 2.f : 0.f)
			+ (Agent->IsInCoverFrom(ThreatLocation) ? 1.f : 0.f)
			+ Agent->GetMorale() * 0.5f;
		if (Score > BestSuppressScore && Agent != Squad.Flanker.Get())
		{
			BestSuppressScore = Score;
			Suppressor = Agent;
		}
	}

	// Flanker: keep the current one until it arrives or times out; otherwise pick the bravest
	// agent already furthest off the main axis, so the move looks natural.
	const bool bFlankerValid = Squad.Flanker.IsValid() && Squad.Flanker->IsAgentAlive() && Now - Squad.FlankerAssignedTime < SquadDirector::FlankerTimeout;
	if (!bFlankerValid)
	{
		Squad.Flanker.Reset();

		if (Alive.Num() >= 3 && Now >= Squad.FlankCooldownUntil)
		{
			const FVector Axis = GetSquadAxis(Alive[0], ThreatLocation);
			float BestFlankScore = -1.f;
			for (ASquadAIController* Agent : Alive)
			{
				if (Agent == Suppressor || Agent->GetMorale() < 0.5f)
				{
					continue;
				}
				const FVector FromThreat = (Agent->GetPawn()->GetActorLocation() - ThreatLocation).GetSafeNormal2D();
				const float OffAxis = 1.f - FMath::Abs(FVector::DotProduct(FromThreat, Axis));
				const float Score = OffAxis + Agent->GetMorale();
				if (Score > BestFlankScore)
				{
					BestFlankScore = Score;
					Squad.Flanker = Agent;
				}
			}
			Squad.FlankerAssignedTime = Now;
		}
	}

	for (ASquadAIController* Agent : Alive)
	{
		ESquadRole Role = ESquadRole::Rifleman;
		if (Agent == Squad.Flanker.Get())
		{
			Role = ESquadRole::Flanker;
		}
		else if (Agent == Suppressor)
		{
			Role = ESquadRole::Suppressor;
		}
		Squad.Roles.Add(Agent, Role);
	}
}

void USquadSubsystem::RecordHostilePositions(float DeltaTime)
{
	HeatmapAccumulator += DeltaTime;
	if (HeatmapAccumulator < SquadDirector::HeatmapSampleInterval)
	{
		return;
	}

	const float Sample = HeatmapAccumulator;
	HeatmapAccumulator = 0.f;

	// Only learn from player-controlled pawns: these are the habits worth exploiting.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (It->IsPlayerControlled())
		{
			HostileHeatmap.FindOrAdd(SquadDirector::HeatCell(It->GetActorLocation())) += Sample;
		}
	}
}
