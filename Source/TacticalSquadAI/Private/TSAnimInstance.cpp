#include "TSAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/BlendSpace.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TSCharacter.h"
#include "UObject/ConstructorHelpers.h"

const FName UTSAnimInstance::UpperBodySlot(TEXT("UpperBody"));

namespace TSAnim
{
	constexpr float IdleThreshold = 10.f;
	/** How fast the movement direction feeding the blend may swing, deg/s. Hides one-frame velocity jitter. */
	constexpr float DirectionInterpSpeed = 540.f;
	constexpr float FallBlendSpeed = 6.f;
	const FName UpperBodyRoot(TEXT("spine_01"));

	const TCHAR* const Directions[] = {
		TEXT("Fwd"), TEXT("Fwd_Right"), TEXT("Right"), TEXT("Bwd_Right"),
		TEXT("Bwd"), TEXT("Bwd_Left"), TEXT("Left"), TEXT("Fwd_Left")
	};

	UAnimSequenceBase* LoadAnim(const FString& Path)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Finder(*Path);
		return Finder.Object;
	}
}

// ---- Game thread --------------------------------------------------------------------------------

UTSAnimInstance::UTSAnimInstance()
{
	const FString Root = TEXT("/Game/Characters/Mannequins/Anims/Rifle/");

	IdleAnimation = TSAnim::LoadAnim(Root + TEXT("MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
	FallAnimation = TSAnim::LoadAnim(Root + TEXT("Jump/MM_Rifle_Jump_Fall_Loop.MM_Rifle_Jump_Fall_Loop"));

	for (const TCHAR* Dir : TSAnim::Directions)
	{
		WalkAnimations.Add(TSAnim::LoadAnim(FString::Printf(TEXT("%sWalk/MF_Rifle_Walk_%s.MF_Rifle_Walk_%s"), *Root, Dir, Dir)));
		JogAnimations.Add(TSAnim::LoadAnim(FString::Printf(TEXT("%sJog/MF_Rifle_Jog_%s.MF_Rifle_Jog_%s"), *Root, Dir, Dir)));
	}

	static ConstructorHelpers::FObjectFinder<UBlendSpace> AimOffsetFinder(TEXT("/Game/Characters/Mannequins/Anims/Rifle/AIM/AO_Rifle.AO_Rifle"));
	AimOffset = AimOffsetFinder.Object;
}

void UTSAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ATSCharacter* Character = Cast<ATSCharacter>(TryGetPawnOwner());
	if (!Character)
	{
		return;
	}

	const FVector Velocity = Character->GetVelocity();
	const FRotator ActorRotation = Character->GetActorRotation();

	Speed = Velocity.Size2D();
	if (Speed > TSAnim::IdleThreshold)
	{
		// Swing the blend direction along the shortest arc instead of snapping between clips.
		const float Target = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - ActorRotation.Yaw);
		const float Delta = FRotator::NormalizeAxis(Target - Direction);
		const float Step = TSAnim::DirectionInterpSpeed * DeltaSeconds;
		Direction = FRotator::NormalizeAxis(Direction + FMath::Clamp(Delta, -Step, Step));
	}
	bIsFalling = Character->GetCharacterMovement()->IsFalling();

	const FRotator AimDelta = (Character->GetBaseAimRotation() - ActorRotation).GetNormalized();
	AimPitch = FMath::FInterpTo(AimPitch, FMath::Clamp(AimDelta.Pitch, -90.f, 90.f), DeltaSeconds, 15.f);
}

// ---- Proxy --------------------------------------------------------------------------------------

void FTSAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	const UTSAnimInstance* Instance = CastChecked<UTSAnimInstance>(InAnimInstance);
	Idle = Instance->IdleAnimation;
	Fall = Instance->FallAnimation;
	Walk.Reset();
	Jog.Reset();
	for (const TObjectPtr<UAnimSequenceBase>& Anim : Instance->WalkAnimations)
	{
		Walk.Add(Anim.Get());
	}
	for (const TObjectPtr<UAnimSequenceBase>& Anim : Instance->JogAnimations)
	{
		Jog.Add(Anim.Get());
	}
	AimOffset = Instance->AimOffset;

	// The aim offset's pitch axis is the one its samples are spread along.
	if (AimOffset)
	{
		FVector Min(TNumericLimits<float>::Max());
		FVector Max(-TNumericLimits<float>::Max());
		for (const FBlendSample& Sample : AimOffset->GetBlendSamples())
		{
			Min = Min.ComponentMin(Sample.SampleValue);
			Max = Max.ComponentMax(Sample.SampleValue);
		}
		AimPitchAxis = (Max.Y - Min.Y) >= (Max.X - Min.X) ? 1 : 0;
		const FBlendParameter& Parameter = AimOffset->GetBlendParameter(AimPitchAxis);
		AimPitchRange = FVector2D(Parameter.Min, Parameter.Max);
	}

	RegisterSlotNodeWithAnimInstance(UTSAnimInstance::UpperBodySlot);
}

void FTSAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UTSAnimInstance* Instance = CastChecked<UTSAnimInstance>(InAnimInstance);
	Speed = Instance->Speed;
	Direction = Instance->Direction;
	AimPitch = Instance->AimPitch;
	bFalling = Instance->bIsFalling;

	IdleTime += DeltaSeconds;
	FallWeight = FMath::FInterpConstantTo(FallWeight, bFalling ? 1.f : 0.f, DeltaSeconds, TSAnim::FallBlendSpeed);

	// Distance drives every locomotion clip (see ClipTime), so feet never slide.
	DistanceTravelled += Speed * DeltaSeconds;
}

float FTSAnimInstanceProxy::GetClipSpeed(const UAnimSequenceBase* Clip, const FBoneContainer& Bones)
{
	if (const float* Cached = ClipSpeeds.Find(Clip))
	{
		return *Cached;
	}

	// Root displacement between the first and last frame of the root-motion clip.
	auto RootAt = [&Bones, Clip](double Time)
	{
		FCompactPose Pose;
		Pose.SetBoneContainer(&Bones);
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData Data(Pose, Curve, Attributes);
		Clip->GetAnimationPose(Data, FAnimExtractContext(Time, false, {}, false));
		return Pose[FCompactPoseBoneIndex(0)].GetTranslation();
	};

	const float Length = Clip->GetPlayLength();
	const float Distance = FVector::Dist2D(RootAt(0.0), RootAt(Length));
	const float RootSpeed = Length > KINDA_SMALL_NUMBER ? Distance / Length : 0.f;
	ClipSpeeds.Add(Clip, RootSpeed);
	return RootSpeed;
}

double FTSAnimInstanceProxy::ClipTime(const UAnimSequenceBase* Clip) const
{
	const float* ClipSpeed = ClipSpeeds.Find(Clip);
	const double Length = Clip->GetPlayLength();
	if (!ClipSpeed || *ClipSpeed < 1.f || Length <= 0.0)
	{
		return FMath::Fmod(IdleTime, FMath::Max(Length, 0.01));
	}
	return FMath::Fmod(DistanceTravelled / *ClipSpeed, Length);
}

void FTSAnimInstanceProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);

	GetSlotWeight(UTSAnimInstance::UpperBodySlot, SlotNodeWeight, SlotSourceWeight, SlotTotalWeight);
	UpdateSlotNodeWeight(UTSAnimInstance::UpperBodySlot, SlotNodeWeight, 1.f);
}

void FTSAnimInstanceProxy::GatherLocomotionSamples(TArray<FLocomotionSample, TInlineAllocator<8>>& OutSamples) const
{
	auto Add = [&OutSamples](const UAnimSequenceBase* Animation, float Weight, double Time)
	{
		if (Animation && Weight > KINDA_SMALL_NUMBER)
		{
			OutSamples.Add({ Animation, Weight, Time });
		}
	};

	// Gait weights from speed: idle -> walk -> jog (sprint = faster jog).
	float IdleW = 1.f;
	float WalkW = 0.f;
	float JogW = 0.f;
	if (Speed > TSAnim::IdleThreshold)
	{
		// Below half walking speed, mix in idle so a slow shuffle does not over-stride.
		const float WalkIn = WalkSpeedRef * 0.5f;
		if (Speed < WalkIn)
		{
			WalkW = Speed / WalkIn;
			IdleW = 1.f - WalkW;
		}
		else if (Speed < WalkSpeedRef)
		{
			WalkW = 1.f;
			IdleW = 0.f;
		}
		else
		{
			JogW = FMath::GetMappedRangeValueClamped(FVector2D(WalkSpeedRef, JogSpeedRef), FVector2D(0.f, 1.f), Speed);
			WalkW = 1.f - JogW;
			IdleW = 0.f;
		}
	}

	const float Upright = 1.f - FallWeight;
	Add(Idle, IdleW * Upright, Idle ? FMath::Fmod(IdleTime, static_cast<double>(Idle->GetPlayLength())) : 0.0);

	// Direction: blend the two neighbouring 45-degree clips.
	if (Walk.Num() == 8 && Jog.Num() == 8)
	{
		const float Angle = FMath::Fmod(Direction + 360.f, 360.f);
		const int32 First = FMath::FloorToInt(Angle / 45.f) % 8;
		const int32 Second = (First + 1) % 8;
		const float Alpha = (Angle - First * 45.f) / 45.f;

		auto AddDirectional = [&](const TArray<const UAnimSequenceBase*>& Set, float GaitWeight)
		{
			for (const TPair<int32, float>& Entry : { TPair<int32, float>(First, 1.f - Alpha), TPair<int32, float>(Second, Alpha) })
			{
				const UAnimSequenceBase* Clip = Set[Entry.Key];
				Add(Clip, GaitWeight * Entry.Value * Upright, Clip ? ClipTime(Clip) : 0.0);
			}
		};
		AddDirectional(Walk, WalkW);
		AddDirectional(Jog, JogW);
	}

	Add(Fall, FallWeight, Fall ? FMath::Fmod(IdleTime, static_cast<double>(Fall->GetPlayLength())) : 0.0);

	float Total = 0.f;
	for (const FLocomotionSample& Sample : OutSamples)
	{
		Total += Sample.Weight;
	}
	for (FLocomotionSample& Sample : OutSamples)
	{
		Sample.Weight /= FMath::Max(Total, KINDA_SMALL_NUMBER);
	}
}

void FTSAnimInstanceProxy::EvaluateLocomotion(FPoseContext& Output)
{
	// Measure every directional clip once (needs a bone container, so it happens here).
	if (ClipSpeeds.IsEmpty() && Walk.Num() == 8 && Jog.Num() == 8 && Walk[0] && Jog[0])
	{
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		for (const UAnimSequenceBase* Clip : Walk)
		{
			if (Clip)
			{
				GetClipSpeed(Clip, Bones);
			}
		}
		for (const UAnimSequenceBase* Clip : Jog)
		{
			if (Clip)
			{
				GetClipSpeed(Clip, Bones);
			}
		}
		WalkSpeedRef = FMath::Max(50.f, ClipSpeeds.FindRef(Walk[0]));
		JogSpeedRef = FMath::Max(WalkSpeedRef + 50.f, ClipSpeeds.FindRef(Jog[0]));
	}

	TArray<FLocomotionSample, TInlineAllocator<8>> Samples;
	GatherLocomotionSamples(Samples);

	if (Samples.IsEmpty())
	{
		Output.ResetToRefPose();
		return;
	}

	if (Samples.Num() == 1)
	{
		FAnimationPoseData PoseData(Output);
		Samples[0].Animation->GetAnimationPose(PoseData, FAnimExtractContext(Samples[0].Time, false, {}, true));
		LockRoot(Output);
		return;
	}

	const int32 Count = Samples.Num();
	TArray<FCompactPose, TInlineAllocator<8>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<8>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<8>> Attributes;
	TArray<float, TInlineAllocator<8>> Weights;
	Poses.SetNum(Count);
	Curves.SetNum(Count);
	Attributes.SetNum(Count);
	Weights.SetNum(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		Poses[Index].SetBoneContainer(&Output.Pose.GetBoneContainer());
		Curves[Index].InitFrom(Output.Curve);
		FAnimationPoseData PoseData(Poses[Index], Curves[Index], Attributes[Index]);
		Samples[Index].Animation->GetAnimationPose(PoseData, FAnimExtractContext(Samples[Index].Time, false, {}, true));
		Weights[Index] = Samples[Index].Weight;
	}

	FAnimationPoseData OutData(Output);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, OutData);
	LockRoot(Output);
}

void FTSAnimInstanceProxy::LockRoot(FPoseContext& Output)
{
	// The clips carry root motion, but the capsule already moves the character: pin the root.
	Output.Pose[FCompactPoseBoneIndex(0)] = FTransform::Identity;
}

void FTSAnimInstanceProxy::ApplyUpperBodySlot(FPoseContext& Output)
{
	if (SlotNodeWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		return;
	}

	FPoseContext SlotPose(this);
	FAnimationPoseData SlotData(SlotPose);
	SlotEvaluatePose(UTSAnimInstance::UpperBodySlot, FAnimationPoseData(Output), SlotSourceWeight, SlotData, SlotNodeWeight, SlotTotalWeight);

	// Copy the slot result onto spine_01 and everything below it; legs keep the locomotion.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FBoneReference UpperRoot(TSAnim::UpperBodyRoot);
	UpperRoot.Initialize(Bones);
	const FCompactPoseBoneIndex RootIndex = UpperRoot.GetCompactPoseIndex(Bones);
	if (RootIndex == INDEX_NONE)
	{
		return;
	}

	for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
	{
		for (FCompactPoseBoneIndex Walker = BoneIndex; Walker != INDEX_NONE; Walker = Bones.GetParentBoneIndex(Walker))
		{
			if (Walker == RootIndex)
			{
				Output.Pose[BoneIndex] = SlotPose.Pose[BoneIndex];
				break;
			}
		}
	}
}

void FTSAnimInstanceProxy::ApplyAimOffset(FPoseContext& Output)
{
	if (!AimOffset)
	{
		return;
	}

	FVector BlendInput = FVector::ZeroVector;
	// The asset's axis is normalised (-1 = straight down, 1 = straight up); map degrees onto it.
	BlendInput[AimPitchAxis] = FMath::GetMappedRangeValueClamped(FVector2D(-90.f, 90.f), AimPitchRange, AimPitch);

	TArray<FBlendSampleData> SampleData;
	int32 CachedTriangle = INDEX_NONE;
	if (!AimOffset->GetSamplesFromBlendInput(BlendInput, SampleData, CachedTriangle, true))
	{
		return;
	}

	FPoseContext Additive(this, true);
	FAnimationPoseData AdditiveData(Additive);
	AimOffset->GetAnimationPose(SampleData, FAnimExtractContext(0.0), AdditiveData);

	FAnimationPoseData BaseData(Output);
	FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToLocalPose(BaseData, AdditiveData, 1.f);
}

bool FTSAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	EvaluateLocomotion(Output);
	ApplyUpperBodySlot(Output);
	ApplyAimOffset(Output);
	return true;
}
