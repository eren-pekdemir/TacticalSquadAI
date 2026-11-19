#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "TSAnimInstance.generated.h"

class UAnimSequenceBase;
class UBlendSpace;
class UTSAnimInstance;

/**
 *  Worker-thread side of the rifle animation. Evaluates the whole pose in code, with no
 *  Animation Blueprint graph:
 *    1. 8-way directional locomotion (idle / walk / jog). Every clip runs on one shared, distance-driven
 *       step phase aligned on the clips' foot sync markers, so neighbouring directions and gaits blend
 *       foot-for-foot (no scissoring legs) and feet do not slide. Root motion is locked out
 *    2. turn in place: when the body turns on the spot the feet step around instead of skating
 *    3. crouch layer: legs and hips from the crouch clips, torso kept from the rifle pose and
 *       counter-rotated so the weapon keeps pointing exactly where it did standing
 *    4. falling pose
 *    5. upper-body slot (fire, reload, hit reacts) layered from spine_01 up
 *    6. procedural torso / head look (yaw) towards the aim and a lean for peeking around cover
 *    7. mesh-space aim offset for pitch
 */
USTRUCT()
struct TACTICALSQUADAI_API FTSAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTSAnimInstanceProxy() = default;
	explicit FTSAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

protected:

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:

	struct FLocomotionSample
	{
		const UAnimSequenceBase* Animation = nullptr;
		float Weight = 0.f;
		double Time = 0.0;
	};
	using FSampleList = TArray<FLocomotionSample, TInlineAllocator<12>>;

	/**
	 *  Measured once per clip: root speed (cm/s), distance per stride (cm), how many strides the clip
	 *  holds (a loop often contains two or three) and the phase of its first reference foot marker.
	 */
	struct FClipInfo
	{
		float Speed = 0.f;
		float Stride = 0.f;
		float Strides = 1.f;
		float MarkerPhase = 0.f;
	};

	void MeasureClips(const FBoneContainer& Bones);
	float MeasureRootSpeed(const UAnimSequenceBase* Clip, const FBoneContainer& Bones) const;

	/** What the feet do in a clip: ground speed of the planted foot, right-foot plants per loop, first plant phase. */
	struct FFootTrack
	{
		float Speed = 0.f;
		int32 Plants = 0;
		float FirstPlantPhase = 0.f;
	};

	/** Reads the gait straight from the foot bones. Works for in-place clips, which have no root motion to measure. */
	FFootTrack AnalyzeFeet(const UAnimSequenceBase* Clip, const FBoneContainer& Bones) const;
	/** Clip time for a stride count: every clip lands on the same foot at the same moment. */
	double PhaseTime(const UAnimSequenceBase* Clip, double InPhase) const;
	double LoopTime(const UAnimSequenceBase* Clip) const;
	float CycleOf(const UAnimSequenceBase* Clip, float FallbackSpeed) const;

	/** Adds the two neighbouring direction clips of an 8-way set; accumulates their cycle length. */
	void AddDirectional(FSampleList& Out, const TArray<const UAnimSequenceBase*>& Set, float Weight, double InPhase,
		float Influence, float& CycleSum, float& CycleWeight) const;
	void AddTurnStep(FSampleList& Out, const TArray<const UAnimSequenceBase*>& Set, float Weight) const;
	static void Normalize(FSampleList& Samples);
	void BuildSamples(float DeltaSeconds);

	void EvaluateSamples(const FSampleList& Samples, FPoseContext& Output) const;
	void ApplyCrouchLegs(FPoseContext& Output);
	void ApplyUpperBodySlot(FPoseContext& Output);
	void CounterRotateTorso(FPoseContext& Output) const;
	void ApplyTorsoLookAndLean(FPoseContext& Output) const;
	void ApplyAimOffset(FPoseContext& Output);
	static void LockRoot(FPoseContext& Output);

	// Assets, copied from the instance on initialize.
	const UAnimSequenceBase* Idle = nullptr;
	const UAnimSequenceBase* Fall = nullptr;
	const UAnimSequenceBase* CrouchIdle = nullptr;
	TArray<const UAnimSequenceBase*> Walk;
	TArray<const UAnimSequenceBase*> Jog;
	TArray<const UAnimSequenceBase*> Crouch;
	const UBlendSpace* AimOffset = nullptr;
	int32 AimPitchAxis = 1;
	FVector2D AimPitchRange = FVector2D(-90.f, 90.f);

	// Game-thread state copied in PreUpdate.
	float Speed = 0.f;
	float Direction = 0.f;
	float AimPitch = 0.f;
	float LookYaw = 0.f;
	float TurnRate = 0.f;
	float LeanTarget = 0.f;
	bool bFalling = false;
	bool bCrouched = false;
	bool bAiming = false;

	// Running state.
	TMap<const UAnimSequenceBase*, FClipInfo> Clips;
	bool bClipsMeasured = false;
	float WalkSpeedRef = 150.f;
	float JogSpeedRef = 350.f;
	float CrouchSpeedRef = 150.f;
	/** Strides walked so far (wrapped), shared by every locomotion clip. */
	double Phase = 0.0;
	double TurnPhase = 0.0;
	double IdleTime = 0.0;
	float FallWeight = 0.f;
	float CrouchAlpha = 0.f;
	float TurnAlpha = 0.f;
	float Lean = 0.f;
	/** 1 when the weapon is up: the whole look yaw goes into the spine so the rifle follows the eyes. */
	float AimAlpha = 0.f;

	FSampleList StandSamples;
	FSampleList CrouchSamples;
	/** Standing pelvis rotation, kept so the crouched torso can be squared back up to it. */
	FQuat StandPelvisRotation = FQuat::Identity;

	// Upper-body slot weights computed in Update, used in Evaluate.
	float SlotNodeWeight = 0.f;
	float SlotSourceWeight = 1.f;
	float SlotTotalWeight = 0.f;
};

/** Rifle soldier animation, fully driven from C++. Assign directly as the mesh's anim class. */
UCLASS(Transient, Blueprintable)
class TACTICALSQUADAI_API UTSAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

	friend struct FTSAnimInstanceProxy;

public:

	UTSAnimInstance();

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	static const FName UpperBodySlot;

protected:

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FTSAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FTSAnimInstanceProxy*>(InProxy); }

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> FallAnimation;

	/** Fwd, Fwd-Right, Right, Bwd-Right, Bwd, Bwd-Left, Left, Fwd-Left. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> WalkAnimations;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> JogAnimations;

	/** Optional. Only the legs and hips are used; the torso keeps the rifle pose. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> CrouchIdleAnimation;

	/** Optional, same direction order as the walk set. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> CrouchAnimations;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UBlendSpace> AimOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Speed = 0.f;

	/** Movement direction relative to facing, -180..180. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Direction = 0.f;

	/** Body yaw speed in deg/s; drives the turn-in-place steps. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float TurnRate = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsCrouched = false;

	/** Body squared up to the aim (strafing) rather than facing the path. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	bool bIsAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimPitch = 0.f;

	/** Aim yaw relative to the body, taken up by the torso, neck and head. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float LookYaw = 0.f;

	/** -1 left .. +1 right. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float Lean = 0.f;

private:

	float LastActorYaw = 0.f;
	bool bHasLastYaw = false;
};
