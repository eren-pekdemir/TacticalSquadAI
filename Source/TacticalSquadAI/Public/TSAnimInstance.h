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
 *    1. 8-way directional locomotion (idle / walk / jog). The clips are root-motion clips: each one is
 *       played by distance travelled at its own measured speed (stride matching) and the root is locked,
 *       so feet stay planted whatever the capsule speed
 *    2. falling pose
 *    3. upper-body slot (fire, reload, hit reacts) layered from spine_01 up
 *    4. mesh-space aim offset for pitch
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

	void GatherLocomotionSamples(TArray<FLocomotionSample, TInlineAllocator<8>>& OutSamples) const;
	void EvaluateLocomotion(FPoseContext& Output);
	static void LockRoot(FPoseContext& Output);

	/** Root speed of a clip in cm/s, measured from its root bone on first use. */
	float GetClipSpeed(const UAnimSequenceBase* Clip, const FBoneContainer& Bones);
	double ClipTime(const UAnimSequenceBase* Clip) const;
	void ApplyUpperBodySlot(FPoseContext& Output);
	void ApplyAimOffset(FPoseContext& Output);

	// Assets, copied from the instance on initialize.
	const UAnimSequenceBase* Idle = nullptr;
	const UAnimSequenceBase* Fall = nullptr;
	TArray<const UAnimSequenceBase*> Walk;
	TArray<const UAnimSequenceBase*> Jog;
	const UBlendSpace* AimOffset = nullptr;
	int32 AimPitchAxis = 1;
	FVector2D AimPitchRange = FVector2D(-90.f, 90.f);

	// Game-thread state copied in PreUpdate.
	float Speed = 0.f;
	float Direction = 0.f;
	float AimPitch = 0.f;
	bool bFalling = false;

	// Running clocks.
	double DistanceTravelled = 0.0;
	TMap<const UAnimSequenceBase*, float> ClipSpeeds;
	float WalkSpeedRef = 295.f;
	float JogSpeedRef = 590.f;
	double IdleTime = 0.0;
	float FallWeight = 0.f;

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

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UBlendSpace> AimOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Speed = 0.f;

	/** Movement direction relative to facing, -180..180. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimPitch = 0.f;
};
