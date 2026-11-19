#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "SquadAITypes.h"
#include "SquadCoverSubsystem.h"
#include "SquadAIController.generated.h"

class ISquadCombatant;
class UAISenseConfig_Damage;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class USquadBrainComponent;
class USquadKnowledgeComponent;
class USquadSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSquadBarkSignature, ASquadAIController*, Speaker, const FSquadBarkEvent&, Bark);

/**
 *  Drives one squad member.
 *  Owns the agent's senses (sight, hearing, damage), memory and utility brain, plus the
 *  "body language" helpers actions use: moving, aiming, shooting, taking cover and talking.
 *  Emotional state (morale, suppression) lives here because every system reads it.
 */
UCLASS()
class SQUADAI_API ASquadAIController : public AAIController
{
	GENERATED_BODY()

public:

	ASquadAIController();

	virtual void Tick(float DeltaTime) override;

	// ---- Accessors -------------------------------------------------------------------------

	ISquadCombatant* GetCombatant() const;
	USquadKnowledgeComponent* GetKnowledge() const { return Knowledge; }
	USquadBrainComponent* GetBrain() const { return Brain; }
	USquadSubsystem* GetSquadSubsystem() const;
	USquadCoverSubsystem* GetCoverSubsystem() const;

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	bool IsAgentAlive() const;

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	float GetMorale() const { return Morale; }

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	float GetSuppression() const { return Suppression; }

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	ESquadRole GetRole() const;

	int32 GetSquadId() const { return SquadId; }

	// ---- Emotional state -------------------------------------------------------------------

	void AddMorale(float Delta);
	void AddSuppression(float Amount);

	// ---- Cover -----------------------------------------------------------------------------

	/** Finds, claims and remembers the best cover for the intent. Returns false if none. */
	bool AcquireCover(ESquadCoverIntent Intent, const FVector& ThreatLocation, float RangeMin, float RangeMax, float SearchRadius = 2200.f, bool bExcludeCurrent = false);
	void ReleaseCover();

	/** The claimed cover could not be reached (blocked path, wall in the way): avoid it for a while. */
	void MarkCoverUnreachable();
	int32 GetCoverIndex() const { return CoverIndex; }
	const FSquadCoverPoint* GetCurrentCover() const;

	/**
	 *  Where to look while waiting for a threat. Behind tall cover that is the corner the agent will peek
	 *  around (holding the angle), never the wall itself; anywhere else, the threat.
	 */
	FVector GetWatchPoint(const FVector& ThreatLocation) const;

	/** The corner to hold behind tall cover, else the threat; GetWatchPoint adds the wall check on top. */
	FVector GetCornerOrThreatPoint(const FVector& ThreatLocation) const;

	/** Standing at the claimed cover and it still hides the agent from ThreatLocation. */
	bool IsInCoverFrom(const FVector& ThreatLocation) const;

	// ---- Body ------------------------------------------------------------------------------

	void MoveToPoint(const FVector& Destination, bool bSprint, float AcceptanceRadius = 40.f);
	bool HasReached(const FVector& Destination, float Tolerance = 60.f) const;
	bool IsMoving() const;
	void StopMoving();

	/** Look and point the weapon at a world location (strafe-aims while moving). */
	void AimAt(const FVector& Point);
	void ClearAim();

	/** Shoot at the target (or its last known position). Spread comes from the accuracy model. */
	void FireAt(const FSquadTargetKnowledge& Target, bool bSuppressive);
	void StopFiring();

	/**
	 *  Attack actions call this while they deliberately show themselves to shoot: the agent stands up
	 *  from low cover, or leans out (Lean -1 left, +1 right) from the edge of tall cover.
	 */
	void SetExposed(bool bInExposed, float Lean = 0.f);

	/** Inaccuracy in degrees: settles while the target stays visible, worsens under suppression. */
	float ComputeSpread(const FSquadTargetKnowledge& Target, bool bSuppressive) const;

	// ---- Voice -----------------------------------------------------------------------------

	/** Says a line if the squad has not just said the same thing. */
	bool Bark(ESquadBark Type, float SquadCooldown = 3.f);

	UPROPERTY(BlueprintAssignable, Category = "Squad AI")
	FOnSquadBarkSignature OnBark;

	const TArray<FSquadBarkEvent>& GetRecentBarks() const { return RecentBarks; }

	// ---- Debug -----------------------------------------------------------------------------

	const TArray<FSquadCoverCandidate>& GetLastCoverCandidates() const { return LastCoverCandidates; }

protected:

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandlePawnDamaged(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	void HandleDeath();
	void UpdateVisibleTargets(float DeltaTime);
	void UpdateEmotions(float DeltaTime);
	void UpdateMovementStyle();

	/** Crouch behind low cover or when pinned down, stand to move or shoot, lean when peeking. */
	void UpdatePosture(float DeltaTime);

	/** With no explicit aim, keep eyes on where the threat probably is (never stare at a wall). */
	void UpdateThreatWatch();

	/** True when a squad mate stands between the muzzle and the aim point. */
	bool IsFriendlyInLineOfFire(const FVector& AimPoint) const;

	UPROPERTY(VisibleAnywhere, Category = "Squad AI")
	TObjectPtr<USquadKnowledgeComponent> Knowledge;

	UPROPERTY(VisibleAnywhere, Category = "Squad AI")
	TObjectPtr<USquadBrainComponent> Brain;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	/** Hearing stimulus tag the game uses for reload sounds. Hearing it tells the squad to push. */
	UPROPERTY(EditDefaultsOnly, Category = "Squad AI")
	FName ReloadNoiseTag = TEXT("Reload");

	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "deg"))
	float BaseSpread = 1.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "deg"))
	float UnsettledSpread = 10.f;

	/** Seconds of continuous sight before the aim is fully settled. The first shots deliberately miss. */
	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "s"))
	float AimSettleTime = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "deg"))
	float SuppressedSpread = 6.f;

	/** Human reaction time: how long a newly seen target must stay visible before the first shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "s"))
	FVector2D ReactionTime = FVector2D(0.25f, 0.6f);

	UPROPERTY(EditDefaultsOnly, Category = "Squad AI|Accuracy", meta = (Units = "deg"))
	float SuppressiveFireSpread = 4.f;

private:

	float Morale = 1.f;
	float Suppression = 0.f;
	double LastDamageTime = -100.0;
	double NextShareTime = 0.0;

	int32 SquadId = 0;
	int32 CoverIndex = INDEX_NONE;
	bool bDead = false;
	bool bAiming = false;
	bool bWatching = false;
	bool bExposed = false;
	/** Cover index -> world time until which it is skipped. */
	TMap<int32, double> UnreachableCovers;
	double LastAimTime = -100.0;
	bool bCrouched = false;
	float ExposedLean = 0.f;
	/** Rolled per agent from ReactionTime on possess. */
	float Reaction = 0.4f;
	/** Time spent pinned (or settled) before crouching; avoids bobbing up and down. */
	float CrouchUrge = 0.f;
	bool bSprintRequested = false;

	TArray<FSquadBarkEvent> RecentBarks;
	TArray<FSquadCoverCandidate> LastCoverCandidates;
};
