#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadAITypes.h"
#include "SquadSubsystem.generated.h"

class ASquadAIController;

/** Runtime state of one squad. */
struct FSquadState
{
	TArray<TWeakObjectPtr<ASquadAIController>> Members;
	TMap<TWeakObjectPtr<ASquadAIController>, ESquadRole> Roles;

	/** Agents currently allowed to shoot, with the time their token expires. */
	TMap<TWeakObjectPtr<ASquadAIController>, double> AttackTokens;
	TMap<TWeakObjectPtr<ASquadAIController>, double> TokenCooldowns;

	TMap<ESquadBark, double> LastBarkTime;

	TWeakObjectPtr<ASquadAIController> Flanker;
	double FlankerAssignedTime = 0.0;
	double FlankCooldownUntil = 0.0;

	/** The target is reloading / exposed: a good moment to push. */
	double TargetVulnerableUntil = 0.0;

	int32 Casualties = 0;
	int32 InitialSize = 0;
};

/** A piece of information travelling between squad mates. Voice takes time. */
struct FSquadMessage
{
	TWeakObjectPtr<ASquadAIController> Recipient;
	FSquadTargetKnowledge Knowledge;
	double DeliverTime = 0.0;
};

/**
 *  The squad director. Agents decide for themselves; the director only shapes those decisions:
 *  it hands out roles (suppressor / flanker / rifleman), limits how many agents shoot at once
 *  (attack tokens, so the player is pressured but never focus-fired to death), relays what agents
 *  see with a small "radio" delay, applies suppression from near misses and rate-limits barks.
 */
UCLASS()
class SQUADAI_API USquadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USquadSubsystem, STATGROUP_Tickables); }

	void RegisterAgent(ASquadAIController* Agent, int32 SquadId);
	void UnregisterAgent(ASquadAIController* Agent);

	/** The agent died. Nearby squad mates lose morale and someone calls it out. */
	void NotifyAgentDied(ASquadAIController* Agent);

	/** Relay a first-hand observation to the rest of the squad after a short delay. */
	void ShareKnowledge(ASquadAIController* From, const FSquadTargetKnowledge& Knowledge);

	/** The target did something that makes it vulnerable (reloading, healing). */
	void MarkTargetVulnerable(ASquadAIController* Reporter, float Duration);
	bool IsTargetVulnerable(const ASquadAIController* Agent) const;

	ESquadRole GetRole(const ASquadAIController* Agent) const;

	/** Flanker reached its position or gave up. */
	void NotifyFlankFinished(ASquadAIController* Agent);

	bool CanTakeAttackToken(const ASquadAIController* Agent) const;
	bool TakeAttackToken(ASquadAIController* Agent, float Duration);
	void ReleaseAttackToken(ASquadAIController* Agent);
	bool HasAttackToken(const ASquadAIController* Agent) const;

	/** Squad-wide cooldown so five agents never shout "Reloading!" in the same second. */
	bool TryBark(ASquadAIController* Agent, ESquadBark Bark, float Cooldown);

	TArray<ASquadAIController*> GetSquadMates(const ASquadAIController* Agent, bool bIncludeSelf = false) const;

	/** Unit direction from the threat towards the squad's centre of mass. */
	FVector GetSquadAxis(const ASquadAIController* Agent, const FVector& ThreatLocation) const;

	/** A bullet travelled from Start to End. Agents near the path get suppressed. */
	void ReportBulletPath(AActor* Shooter, const FVector& Start, const FVector& End);

	/** Places the hostile has spent time at, weighted. Used to guess where to search. */
	void GetHotspotsNear(const FVector& Origin, float Radius, int32 MaxCount, TArray<FVector>& OutLocations) const;

	/** Maximum number of agents per squad shooting at the same time. */
	int32 MaxAttackTokens = 2;

	/** Delay for information to travel between squad mates, in seconds. */
	FVector2D CommsDelay = FVector2D(0.35f, 0.8f);

private:

	FSquadState* FindSquadOf(const ASquadAIController* Agent);
	const FSquadState* FindSquadOf(const ASquadAIController* Agent) const;

	void UpdateRoles(FSquadState& Squad, double Now);
	void UpdateTokens(FSquadState& Squad, double Now);
	void RecordHostilePositions(float DeltaTime);

	TMap<int32, FSquadState> Squads;
	TMap<TWeakObjectPtr<ASquadAIController>, int32> AgentSquad;
	TArray<FSquadMessage> PendingMessages;

	/** Coarse 2D grid of seconds hostiles spent per cell. */
	TMap<FIntPoint, float> HostileHeatmap;

	double NextRoleUpdate = 0.0;
	float HeatmapAccumulator = 0.f;
};
