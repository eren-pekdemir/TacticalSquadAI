#pragma once

#include "CoreMinimal.h"
#include "SquadAITypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSquadAI, Log, All);

/** Tactical role handed out by the squad director. Roles bias, but never dictate, an agent's utility scores. */
UENUM(BlueprintType)
enum class ESquadRole : uint8
{
	None,
	/** Keeps the target pinned with sustained fire from a protected position. */
	Suppressor,
	/** Moves around the target's side while the target is busy with the suppressor. */
	Flanker,
	/** Holds or advances along the main axis, trading fire from cover. */
	Rifleman
};

/** How an agent learned about a target. Squad-shared information is less reliable than first-hand sight. */
UENUM(BlueprintType)
enum class ESquadInfoSource : uint8
{
	Sight,
	Hearing,
	Damage,
	Squad
};

/** Things an agent can say out loud. Barks make the AI's intent readable to the player. */
UENUM(BlueprintType)
enum class ESquadBark : uint8
{
	Contact,
	Reacquired,
	LostTarget,
	Searching,
	Flanking,
	Suppressing,
	Reloading,
	ManDown,
	FallBack,
	Moving,
	Hit,
	TargetDown,
	TargetReloading
};

UENUM(BlueprintType)
enum class ESquadActionStatus : uint8
{
	Running,
	Succeeded,
	Failed
};

/** Everything an agent believes about one hostile actor. The AI acts on this belief, never on the real position. */
USTRUCT(BlueprintType)
struct SQUADAI_API FSquadTargetKnowledge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	TWeakObjectPtr<AActor> Target;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector LastKnownLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector LastKnownVelocity = FVector::ZeroVector;

	/** World time the target was last confirmed (seen, heard, told). */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	double LastConfirmedTime = 0.0;

	/** World time the target was last seen with this agent's own eyes. Negative if never. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	double LastSeenTime = -1.0;

	/** 0..1, how much the agent trusts LastKnownLocation. Decays while the target is unseen. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	float Confidence = 0.f;

	/** Radius around LastKnownLocation where the target probably is. Grows with time. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	float UncertaintyRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	ESquadInfoSource Source = ESquadInfoSource::Sight;

	/** Time spent continuously seeing the target. Drives the aim "settle" model. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	float VisibleDuration = 0.f;

	bool IsValid() const { return Target.IsValid() && Confidence > KINDA_SMALL_NUMBER; }
};

/** A procedurally generated position that offers protection from one side. */
USTRUCT(BlueprintType)
struct SQUADAI_API FSquadCoverPoint
{
	GENERATED_BODY()

	/** On the navmesh, where the agent stands while covered. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector Location = FVector::ZeroVector;

	/** Points away from the wall, towards the covered side. Threats should be on the -Normal side. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector Normal = FVector::ForwardVector;

	/** The wall is tall enough to hide a standing head. */
	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	bool bFullHeight = true;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	bool bCanPeekLeft = false;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	bool bCanPeekRight = false;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector PeekLeftLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FVector PeekRightLocation = FVector::ZeroVector;

	TWeakObjectPtr<AController> ClaimedBy;

	bool CanPeek() const { return bCanPeekLeft || bCanPeekRight || !bFullHeight; }
};

/** One evaluated option, kept for the debug overlay. */
USTRUCT(BlueprintType)
struct SQUADAI_API FSquadActionScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FName Action;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	float Score = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	bool bActive = false;
};

/** A spoken line. The game decides how to present it (subtitle, speech bubble, voice line). */
USTRUCT(BlueprintType)
struct SQUADAI_API FSquadBarkEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	ESquadBark Bark = ESquadBark::Contact;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	FText Line;

	UPROPERTY(BlueprintReadOnly, Category = "Squad AI")
	double Time = 0.0;
};
