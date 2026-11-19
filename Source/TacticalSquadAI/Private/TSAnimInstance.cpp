#include "TSAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/BlendSpace.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/PackageName.h"
#include "TSCharacter.h"
#include "UObject/ConstructorHelpers.h"

const FName UTSAnimInstance::UpperBodySlot(TEXT("UpperBody"));

namespace TSAnim
{
	constexpr float IdleThreshold = 10.f;
	/** How fast the movement direction feeding the blend may swing, deg/s. Hides one-frame velocity jitter. */
	constexpr float DirectionInterpSpeed = 540.f;
	constexpr float FallBlendSpeed = 6.f;
	/** Kneeling down or standing up takes about a third of a second. */
	constexpr float CrouchBlendSpeed = 3.5f;
	constexpr float LeanBlendSpeed = 4.f;
	constexpr float AimBlendSpeed = 5.f;
	/** Total spine roll at full lean. */
	constexpr float LeanDegrees = 24.f;
	constexpr float LookYawLimit = 100.f;
	/** With the weapon down only the head and neck turn, and not far: the body faces where it goes. */
	constexpr float RelaxedLookYawLimit = 40.f;
	constexpr float LookInterpSpeed = 8.f;
	/** Body yaw speeds (deg/s) between which turn-in-place stepping fades in. */
	constexpr float MinTurnRate = 25.f;
	constexpr float FullTurnRate = 120.f;
	constexpr float TurnBlendSpeed = 8.f;
	/** Above this speed the feet are already moving; no extra turn steps. */
	constexpr float TurnStepMaxSpeed = 60.f;
	/** Foot travel per degree of body turn while stepping around on the spot. */
	constexpr float TurnStepPerDegree = 0.6f;
	/** Clips without root motion are assumed to be authored at this pace (cm/s). */
	constexpr float InPlaceSpeed = 150.f;
	constexpr float MinRootSpeed = 20.f;
	constexpr float MinCycle = 40.f;
	/** Stride counters wrap here; divisible by every stride count a loop is likely to hold. */
	constexpr double PhaseWrap = 60.0;

	const FName Pelvis(TEXT("pelvis"));
	const FName LeftFoot(TEXT("ball_l"));
	const FName RightFoot(TEXT("ball_r"));
	constexpr int32 FootSamplesPerSecond = 60;
	const FName UpperBodyRoot(TEXT("spine_01"));

	/** How look yaw and lean are spread along the torso. Each weight column sums to 1. */
	struct FTorsoBone
	{
		const TCHAR* Name;
		float LookYaw;	// just looking (moving, weapon low): the head and neck take a good share
		float AimYaw;	// weapon up: all in the spine, so the rifle points where the eyes do
		float Lean;
	};
	const FTorsoBone TorsoChain[] = {
		{ TEXT("spine_01"), 0.f,   0.20f, 0.22f },
		{ TEXT("spine_02"), 0.f,   0.20f, 0.22f },
		{ TEXT("spine_03"), 0.05f, 0.20f, 0.22f },
		{ TEXT("spine_04"), 0.05f, 0.20f, 0.17f },
		{ TEXT("spine_05"), 0.10f, 0.20f, 0.17f },
		{ TEXT("neck_01"),  0.25f, 0.f,   0.f },
		{ TEXT("neck_02"),  0.25f, 0.f,   0.f },
		{ TEXT("head"),     0.30f, 0.f,   0.f },
	};

	const TCHAR* const Directions[] = {
		TEXT("Fwd"), TEXT("Fwd_Right"), TEXT("Right"), TEXT("Bwd_Right"),
		TEXT("Bwd"), TEXT("Bwd_Left"), TEXT("Left"), TEXT("Fwd_Left")
	};
	constexpr int32 RightIndex = 2;
	constexpr int32 LeftIndex = 6;

	UAnimSequenceBase* LoadAnim(const FString& Path)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Finder(*Path);
		return Finder.Object;
	}

	/** Loads an asset from an optional content pack without complaining when the pack is not installed. */
	UAnimSequenceBase* LoadOptionalAnim(const FString& Path)
	{
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))
			? LoadObject<UAnimSequenceBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)
			: nullptr;
	}

	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, const FName& Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}

	bool IsUnder(const FBoneContainer& Bones, FCompactPoseBoneIndex Bone, FCompactPoseBoneIndex Ancestor)
	{
		for (FCompactPoseBoneIndex Walker = Bone; Walker != INDEX_NONE; Walker = Bones.GetParentBoneIndex(Walker))
		{
			if (Walker == Ancestor)
			{
				return true;
			}
		}
		return false;
	}

	/** Time of the earliest sync marker with the given name (or -1) and how many times it occurs. */
	float MarkerTime(const UAnimSequenceBase* Clip, FName Marker, int32& OutCount)
	{
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Clip);
		float Best = -1.f;
		OutCount = 0;
		if (Sequence)
		{
			for (const FAnimSyncMarker& Sync : Sequence->AuthoredSyncMarkers)
			{
				if (Sync.MarkerName == Marker)
				{
					++OutCount;
					if (Best < 0.f || Sync.Time < Best)
					{
						Best = Sync.Time;
					}
				}
			}
		}
		return Best;
	}

	FName FirstMarkerName(const UAnimSequenceBase* Clip)
	{
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Clip);
		const FAnimSyncMarker* First = nullptr;
		if (Sequence)
		{
			for (const FAnimSyncMarker& Sync : Sequence->AuthoredSyncMarkers)
			{
				if (!First || Sync.Time < First->Time)
				{
					First = &Sync;
				}
			}
		}
		return First ? First->MarkerName : NAME_None;
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

	// Crouch legs come from the free "Free Animation Library" pack (Fab). Without it agents simply stand.
	const FString CrouchRoot = TEXT("/Game/FreeAnimationLibrary/Animations/Crouch/");
	CrouchIdleAnimation = TSAnim::LoadOptionalAnim(CrouchRoot + TEXT("anim_Crouch_Idle.anim_Crouch_Idle"));
	for (const TCHAR* Dir : TSAnim::Directions)
	{
		const FString Name = FString::Printf(TEXT("anim_Crouch_%s_Loop"), Dir);
		CrouchAnimations.Add(TSAnim::LoadOptionalAnim(CrouchRoot + Name + TEXT(".") + Name));
	}
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
	// Below walking pace the velocity direction is mostly noise (scraping along a wall, stopping), so the
	// blend keeps the last real direction instead of flicking to a side-step.
	constexpr float DirectionMinSpeed = 50.f;
	if (Speed > DirectionMinSpeed)
	{
		// Swing the blend direction along the shortest arc instead of snapping between clips.
		const float Target = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - ActorRotation.Yaw);
		const float Delta = FRotator::NormalizeAxis(Target - Direction);
		const float Step = TSAnim::DirectionInterpSpeed * DeltaSeconds;
		Direction = FRotator::NormalizeAxis(Direction + FMath::Clamp(Delta, -Step, Step));
	}

	// How fast the body is turning, for turn-in-place steps.
	if (bHasLastYaw && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const float Rate = FRotator::NormalizeAxis(ActorRotation.Yaw - LastActorYaw) / DeltaSeconds;
		TurnRate = FMath::FInterpTo(TurnRate, Rate, DeltaSeconds, 12.f);
	}
	LastActorYaw = ActorRotation.Yaw;
	bHasLastYaw = true;

	bIsFalling = Character->GetCharacterMovement()->IsFalling();
	bIsCrouched = Character->bIsCrouched;
	bIsAiming = Character->IsStrafing();
	Lean = Character->GetLean();

	const FRotator AimDelta = (Character->GetBaseAimRotation() - ActorRotation).GetNormalized();
	AimPitch = FMath::FInterpTo(AimPitch, FMath::Clamp(AimDelta.Pitch, -90.f, 90.f), DeltaSeconds, 15.f);
	const float YawLimit = bIsAiming ? TSAnim::LookYawLimit : TSAnim::RelaxedLookYawLimit;
	LookYaw = FMath::FInterpTo(LookYaw, FMath::Clamp(AimDelta.Yaw, -YawLimit, YawLimit), DeltaSeconds, TSAnim::LookInterpSpeed);
}

// ---- Proxy: setup -------------------------------------------------------------------------------

void FTSAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	const UTSAnimInstance* Instance = CastChecked<UTSAnimInstance>(InAnimInstance);
	Idle = Instance->IdleAnimation;
	Fall = Instance->FallAnimation;
	CrouchIdle = Instance->CrouchIdleAnimation;

	auto CopySet = [](const TArray<TObjectPtr<UAnimSequenceBase>>& From, TArray<const UAnimSequenceBase*>& To)
	{
		To.Reset();
		for (const TObjectPtr<UAnimSequenceBase>& Anim : From)
		{
			if (!Anim)
			{
				To.Reset(); // An incomplete set is unusable.
				return;
			}
			To.Add(Anim.Get());
		}
		if (To.Num() != 8)
		{
			To.Reset();
		}
	};
	CopySet(Instance->WalkAnimations, Walk);
	CopySet(Instance->JogAnimations, Jog);
	CopySet(Instance->CrouchAnimations, Crouch);
	if (!CrouchIdle)
	{
		Crouch.Reset();
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

	Clips.Reset();
	bClipsMeasured = false;
	RegisterSlotNodeWithAnimInstance(UTSAnimInstance::UpperBodySlot);
}

void FTSAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UTSAnimInstance* Instance = CastChecked<UTSAnimInstance>(InAnimInstance);
	Speed = Instance->Speed;
	Direction = Instance->Direction;
	AimPitch = Instance->AimPitch;
	LookYaw = Instance->LookYaw;
	TurnRate = Instance->TurnRate;
	LeanTarget = Instance->Lean;
	bFalling = Instance->bIsFalling;
	bCrouched = Instance->bIsCrouched && !Crouch.IsEmpty();
	bAiming = Instance->bIsAiming;

	IdleTime += DeltaSeconds;
	FallWeight = FMath::FInterpConstantTo(FallWeight, bFalling ? 1.f : 0.f, DeltaSeconds, TSAnim::FallBlendSpeed);
	CrouchAlpha = FMath::FInterpConstantTo(CrouchAlpha, bCrouched ? 1.f : 0.f, DeltaSeconds, TSAnim::CrouchBlendSpeed);
	Lean = FMath::FInterpTo(Lean, LeanTarget, DeltaSeconds, TSAnim::LeanBlendSpeed);
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, bAiming ? 1.f : 0.f, DeltaSeconds, TSAnim::AimBlendSpeed);

	const float TurnTarget = Speed < TSAnim::TurnStepMaxSpeed
		? FMath::GetMappedRangeValueClamped(FVector2D(TSAnim::MinTurnRate, TSAnim::FullTurnRate), FVector2D(0.f, 1.f), FMath::Abs(TurnRate))
		: 0.f;
	TurnAlpha = FMath::FInterpTo(TurnAlpha, TurnTarget, DeltaSeconds, TSAnim::TurnBlendSpeed);
}

// ---- Proxy: clip timing -------------------------------------------------------------------------

float FTSAnimInstanceProxy::MeasureRootSpeed(const UAnimSequenceBase* Clip, const FBoneContainer& Bones) const
{
	// Root displacement between the first and last frame of a root-motion clip.
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
	return Length > KINDA_SMALL_NUMBER ? Distance / Length : 0.f;
}

FTSAnimInstanceProxy::FFootTrack FTSAnimInstanceProxy::AnalyzeFeet(const UAnimSequenceBase* Clip, const FBoneContainer& Bones) const
{
	FFootTrack Track;
	const FCompactPoseBoneIndex LeftIndex = TSAnim::FindBone(Bones, TSAnim::LeftFoot);
	const FCompactPoseBoneIndex RightIndex = TSAnim::FindBone(Bones, TSAnim::RightFoot);
	const float Length = Clip->GetPlayLength();
	if (LeftIndex == INDEX_NONE || RightIndex == INDEX_NONE || Length <= KINDA_SMALL_NUMBER)
	{
		return Track;
	}

	// Sample both feet relative to the root over one loop.
	const int32 Count = FMath::Max(24, FMath::RoundToInt(Length * TSAnim::FootSamplesPerSecond));
	TArray<FVector> Left;
	TArray<FVector> Right;
	Left.SetNum(Count);
	Right.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FCompactPose Pose;
		Pose.SetBoneContainer(&Bones);
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData Data(Pose, Curve, Attributes);
		Clip->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Length) * Index / Count, false, {}, true));

		FCSPose<FCompactPose> MeshPose;
		MeshPose.InitPose(Pose);
		const FVector Root = MeshPose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)).GetTranslation();
		Left[Index] = MeshPose.GetComponentSpaceTransform(LeftIndex).GetTranslation() - Root;
		Right[Index] = MeshPose.GetComponentSpaceTransform(RightIndex).GetTranslation() - Root;
	}

	// The lower foot carries the weight; it slides back under the body at the clip's ground speed.
	const float Step = Length / Count;
	FVector2D PlantedDrift = FVector2D::ZeroVector;
	TArray<float> Speeds;
	Speeds.Reserve(Count);
	bool bFoundPlant = false;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Next = (Index + 1) % Count;
		const int32 Previous = (Index + Count - 1) % Count;
		const bool bRightDown = Right[Index].Z < Left[Index].Z;
		const TArray<FVector>& Planted = bRightDown ? Right : Left;
		Speeds.Add(FVector::Dist2D(Planted[Next], Planted[Index]) / Step);
		PlantedDrift += FVector2D(Planted[Next] - Planted[Index]);

		// A right-foot plant: weight moves from the left foot onto the right one.
		const bool bWasRightDown = Right[Previous].Z < Left[Previous].Z;
		if (bRightDown && !bWasRightDown)
		{
			++Track.Plants;
			if (!bFoundPlant)
			{
				Track.FirstPlantPhase = static_cast<float>(Index) / Count;
				bFoundPlant = true;
			}
		}
	}

	Speeds.Sort();
	Track.Speed = Speeds[Speeds.Num() / 2];
	UE_LOG(LogAnimation, Verbose, TEXT("TSAnim: %s planted foot drifts towards %.0f deg in mesh space"), *Clip->GetName(),
		FMath::RadiansToDegrees(FMath::Atan2(PlantedDrift.Y, PlantedDrift.X)));
	return Track;
}

void FTSAnimInstanceProxy::MeasureClips(const FBoneContainer& Bones)
{
	TArray<const UAnimSequenceBase*, TInlineAllocator<32>> All;
	All.Append(Walk);
	All.Append(Jog);
	All.Append(Crouch);
	if (CrouchIdle)
	{
		All.Add(CrouchIdle);
	}

	// Every clip is aligned on the same foot event: the first sync marker of the forward walk.
	const FName Reference = Walk.IsEmpty() ? NAME_None : TSAnim::FirstMarkerName(Walk[0]);

	for (const UAnimSequenceBase* Clip : All)
	{
		if (!Clip || Clips.Contains(Clip))
		{
			continue;
		}

		FClipInfo Info;
		const float Length = FMath::Max(Clip->GetPlayLength(), 0.01f);
		const FFootTrack Feet = AnalyzeFeet(Clip, Bones);

		// Speed: root motion when the clip has it, otherwise read off the planted foot.
		const float RootSpeed = MeasureRootSpeed(Clip, Bones);
		Info.Speed = RootSpeed >= TSAnim::MinRootSpeed ? RootSpeed : Feet.Speed;

		// Strides and alignment: authored sync markers when present, otherwise the detected right-foot plants.
		int32 MarkerCount = 0;
		const float Marker = Reference.IsNone() ? -1.f : TSAnim::MarkerTime(Clip, Reference, MarkerCount);
		if (MarkerCount > 0)
		{
			Info.Strides = MarkerCount;
			Info.MarkerPhase = Marker / Length;
		}
		else
		{
			Info.Strides = FMath::Max(1, Feet.Plants);
			Info.MarkerPhase = Feet.FirstPlantPhase;
		}

		Info.Stride = (Info.Speed >= TSAnim::MinRootSpeed ? Info.Speed : TSAnim::InPlaceSpeed) * Length / Info.Strides;
		Clips.Add(Clip, Info);
		UE_LOG(LogAnimation, Verbose, TEXT("TSAnim: %s speed %.0f cm/s (root %.0f, feet %.0f), %.0f stride(s) of %.0f cm, plant at %.2f"),
			*Clip->GetName(), Info.Speed, RootSpeed, Feet.Speed, Info.Strides, Info.Stride, Info.MarkerPhase);
	}

	if (!Walk.IsEmpty() && !Jog.IsEmpty())
	{
		WalkSpeedRef = FMath::Max(50.f, Clips.FindRef(Walk[0]).Speed);
		JogSpeedRef = FMath::Max(WalkSpeedRef + 50.f, Clips.FindRef(Jog[0]).Speed);
	}
	if (!Crouch.IsEmpty())
	{
		const float CrouchSpeed = Clips.FindRef(Crouch[0]).Speed;
		CrouchSpeedRef = CrouchSpeed >= TSAnim::MinRootSpeed ? CrouchSpeed : TSAnim::InPlaceSpeed;
	}

	bClipsMeasured = true;
}

double FTSAnimInstanceProxy::PhaseTime(const UAnimSequenceBase* Clip, double InPhase) const
{
	const double Length = Clip->GetPlayLength();
	const FClipInfo* Info = Clips.Find(Clip);
	if (!Info)
	{
		return FMath::Frac(InPhase) * Length;
	}
	// Stride count -> position in this clip's loop, starting from its reference foot marker.
	return FMath::Frac(Info->MarkerPhase + InPhase / Info->Strides) * Length;
}

double FTSAnimInstanceProxy::LoopTime(const UAnimSequenceBase* Clip) const
{
	return FMath::Fmod(IdleTime, FMath::Max(static_cast<double>(Clip->GetPlayLength()), 0.01));
}

float FTSAnimInstanceProxy::CycleOf(const UAnimSequenceBase* Clip, float FallbackSpeed) const
{
	const FClipInfo* Info = Clips.Find(Clip);
	return Info ? Info->Stride : FallbackSpeed * Clip->GetPlayLength();
}

// ---- Proxy: sample selection --------------------------------------------------------------------

void FTSAnimInstanceProxy::AddDirectional(FSampleList& Out, const TArray<const UAnimSequenceBase*>& Set, float Weight, double InPhase,
	float Influence, float& CycleSum, float& CycleWeight) const
{
	if (Set.Num() != 8 || Weight <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Blend the two neighbouring 45-degree clips, all on the shared step phase.
	const float Angle = FMath::Fmod(Direction + 360.f, 360.f);
	const int32 First = FMath::FloorToInt(Angle / 45.f) % 8;
	const int32 Second = (First + 1) % 8;
	const float Alpha = (Angle - First * 45.f) / 45.f;

	for (const TPair<int32, float>& Entry : { TPair<int32, float>(First, 1.f - Alpha), TPair<int32, float>(Second, Alpha) })
	{
		const UAnimSequenceBase* Clip = Set[Entry.Key];
		const float ClipWeight = Weight * Entry.Value;
		if (ClipWeight > KINDA_SMALL_NUMBER)
		{
			Out.Add({ Clip, ClipWeight, PhaseTime(Clip, InPhase) });
			CycleSum += ClipWeight * Influence * CycleOf(Clip, TSAnim::InPlaceSpeed);
			CycleWeight += ClipWeight * Influence;
		}
	}
}

void FTSAnimInstanceProxy::AddTurnStep(FSampleList& Out, const TArray<const UAnimSequenceBase*>& Set, float Weight) const
{
	if (Set.Num() != 8 || Weight <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Turning right on the spot = little side steps to the right (and vice versa).
	const UAnimSequenceBase* Clip = Set[TurnRate > 0.f ? TSAnim::RightIndex : TSAnim::LeftIndex];
	Out.Add({ Clip, Weight, PhaseTime(Clip, TurnPhase) });
}

void FTSAnimInstanceProxy::Normalize(FSampleList& Samples)
{
	float Total = 0.f;
	for (const FLocomotionSample& Sample : Samples)
	{
		Total += Sample.Weight;
	}
	for (FLocomotionSample& Sample : Samples)
	{
		Sample.Weight /= FMath::Max(Total, KINDA_SMALL_NUMBER);
	}
}

void FTSAnimInstanceProxy::BuildSamples(float DeltaSeconds)
{
	StandSamples.Reset();
	CrouchSamples.Reset();

	float CycleSum = 0.f;
	float CycleWeight = 0.f;
	const float Upright = 1.f - FallWeight;
	const float StandInfluence = 1.f - CrouchAlpha;

	// Standing gait from speed: idle -> walk -> jog (sprint = faster jog).
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

	const float TurnW = IdleW * TurnAlpha;
	IdleW -= TurnW;

	if (Idle && IdleW * Upright > KINDA_SMALL_NUMBER)
	{
		StandSamples.Add({ Idle, IdleW * Upright, LoopTime(Idle) });
	}
	AddTurnStep(StandSamples, Walk, TurnW * Upright);
	AddDirectional(StandSamples, Walk, WalkW * Upright, Phase, StandInfluence, CycleSum, CycleWeight);
	AddDirectional(StandSamples, Jog, JogW * Upright, Phase, StandInfluence, CycleSum, CycleWeight);
	if (Fall && FallWeight > KINDA_SMALL_NUMBER)
	{
		StandSamples.Add({ Fall, FallWeight, LoopTime(Fall) });
	}
	Normalize(StandSamples);

	// Crouched gait, used for the legs only.
	if (!Crouch.IsEmpty() && CrouchAlpha > KINDA_SMALL_NUMBER)
	{
		// No turn steps while kneeling: the crouched side-step clips swing a leg far out, and a kneeling
		// soldier pivots on the spot anyway.
		const float MoveW = Speed > TSAnim::IdleThreshold ? FMath::Clamp(Speed / (CrouchSpeedRef * 0.5f), 0.f, 1.f) : 0.f;
		const float CrouchIdleW = 1.f - MoveW;

		if (CrouchIdleW > KINDA_SMALL_NUMBER)
		{
			CrouchSamples.Add({ CrouchIdle, CrouchIdleW, LoopTime(CrouchIdle) });
		}
		AddDirectional(CrouchSamples, Crouch, MoveW, Phase, CrouchAlpha, CycleSum, CycleWeight);
		Normalize(CrouchSamples);
	}

	// Advance the shared step phase by distance travelled over the blended stride length.
	if (CycleWeight > KINDA_SMALL_NUMBER)
	{
		const float Cycle = FMath::Max(CycleSum / CycleWeight, TSAnim::MinCycle);
		Phase = FMath::Fmod(Phase + Speed * DeltaSeconds / Cycle, TSAnim::PhaseWrap);
	}

	if (TurnAlpha > KINDA_SMALL_NUMBER && Walk.Num() == 8)
	{
		const float TurnCycle = FMath::Max(CycleOf(Walk[TSAnim::RightIndex], TSAnim::InPlaceSpeed), TSAnim::MinCycle);
		TurnPhase = FMath::Fmod(TurnPhase + FMath::Abs(TurnRate) * TSAnim::TurnStepPerDegree * DeltaSeconds / TurnCycle, TSAnim::PhaseWrap);
	}
}

void FTSAnimInstanceProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);

	GetSlotWeight(UTSAnimInstance::UpperBodySlot, SlotNodeWeight, SlotSourceWeight, SlotTotalWeight);
	UpdateSlotNodeWeight(UTSAnimInstance::UpperBodySlot, SlotNodeWeight, 1.f);

	BuildSamples(DeltaSeconds);
}

// ---- Proxy: evaluation --------------------------------------------------------------------------

void FTSAnimInstanceProxy::EvaluateSamples(const FSampleList& Samples, FPoseContext& Output) const
{
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
	TArray<FCompactPose, TInlineAllocator<12>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<12>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<12>> Attributes;
	TArray<float, TInlineAllocator<12>> Weights;
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

void FTSAnimInstanceProxy::ApplyCrouchLegs(FPoseContext& Output)
{
	if (CrouchAlpha <= ZERO_ANIMWEIGHT_THRESH || CrouchSamples.IsEmpty())
	{
		return;
	}

	FPoseContext CrouchPose(this);
	EvaluateSamples(CrouchSamples, CrouchPose);

	// Hips and legs kneel; everything from spine_01 up keeps holding the rifle.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex UpperRoot = TSAnim::FindBone(Bones, TSAnim::UpperBodyRoot);
	for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
	{
		if (UpperRoot != INDEX_NONE && TSAnim::IsUnder(Bones, BoneIndex, UpperRoot))
		{
			continue;
		}
		FTransform Blended;
		Blended.Blend(Output.Pose[BoneIndex], CrouchPose.Pose[BoneIndex], CrouchAlpha);
		Output.Pose[BoneIndex] = Blended;
	}
}

void FTSAnimInstanceProxy::CounterRotateTorso(FPoseContext& Output) const
{
	if (CrouchAlpha <= ZERO_ANIMWEIGHT_THRESH)
	{
		return;
	}

	// The kneeling hips tilt; square the torso back up to where it was standing so the aim is unchanged.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex PelvisIndex = TSAnim::FindBone(Bones, TSAnim::Pelvis);
	const FCompactPoseBoneIndex SpineIndex = TSAnim::FindBone(Bones, TSAnim::UpperBodyRoot);
	if (PelvisIndex == INDEX_NONE || SpineIndex == INDEX_NONE || Bones.GetParentBoneIndex(SpineIndex) != PelvisIndex)
	{
		return;
	}

	const FQuat Fix = Output.Pose[PelvisIndex].GetRotation().Inverse() * StandPelvisRotation;
	Output.Pose[SpineIndex].SetRotation((Fix * Output.Pose[SpineIndex].GetRotation()).GetNormalized());
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
	const FCompactPoseBoneIndex RootIndex = TSAnim::FindBone(Bones, TSAnim::UpperBodyRoot);
	if (RootIndex == INDEX_NONE)
	{
		return;
	}

	for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
	{
		if (TSAnim::IsUnder(Bones, BoneIndex, RootIndex))
		{
			Output.Pose[BoneIndex] = SlotPose.Pose[BoneIndex];
		}
	}
}

void FTSAnimInstanceProxy::ApplyTorsoLookAndLean(FPoseContext& Output) const
{
	const float LeanAngle = Lean * TSAnim::LeanDegrees;
	if (FMath::Abs(LookYaw) < 0.5f && FMath::Abs(LeanAngle) < 0.2f)
	{
		return;
	}

	// Body axes in mesh space (the mesh is rotated relative to the actor).
	const FTransform& Component = GetComponentTransform();
	const FTransform& Actor = GetActorTransform();
	const FVector Forward = Component.InverseTransformVectorNoScale(Actor.GetUnitAxis(EAxis::X));
	const FVector Right = Component.InverseTransformVectorNoScale(Actor.GetUnitAxis(EAxis::Y));
	const FVector Up = Component.InverseTransformVectorNoScale(FVector::UpVector);
	// A positive rotation about A turns V towards A x V: these turn forward->right and up->right.
	const FVector YawAxis = FVector::CrossProduct(Forward, Right).GetSafeNormal();
	const FVector LeanAxis = FVector::CrossProduct(Up, Right).GetSafeNormal();

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FCSPose<FCompactPose> MeshPose;
	MeshPose.InitPose(Output.Pose);

	for (const TSAnim::FTorsoBone& Entry : TSAnim::TorsoChain)
	{
		const FCompactPoseBoneIndex Index = TSAnim::FindBone(Bones, Entry.Name);
		if (Index == INDEX_NONE)
		{
			continue;
		}

		const float YawShare = FMath::Lerp(Entry.LookYaw, Entry.AimYaw, AimAlpha);
		const FQuat Delta = FQuat(YawAxis, FMath::DegreesToRadians(LookYaw * YawShare))
			* FQuat(LeanAxis, FMath::DegreesToRadians(LeanAngle * Entry.Lean));

		// Parents are already turned, so each bone's transform includes the chain above it.
		FTransform Transform = MeshPose.GetComponentSpaceTransform(Index);
		Transform.SetRotation((Delta * Transform.GetRotation()).GetNormalized());
		MeshPose.SetComponentSpaceTransform(Index, Transform);
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(MeshPose, Output.Pose);
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

	// The aim offset is for the torso. Its samples also carry a hip/leg twist, which would turn the legs
	// away from the direction of travel; keep everything below spine_01 out of it.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex UpperRoot = TSAnim::FindBone(Bones, TSAnim::UpperBodyRoot);
	if (UpperRoot != INDEX_NONE)
	{
		for (const FCompactPoseBoneIndex BoneIndex : Additive.Pose.ForEachBoneIndex())
		{
			if (!TSAnim::IsUnder(Bones, BoneIndex, UpperRoot))
			{
				// Additive identity: no rotation, no translation and *zero* scale delta.
				Additive.Pose[BoneIndex] = FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
			}
		}
	}

	FAnimationPoseData BaseData(Output);
	FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToLocalPose(BaseData, AdditiveData, 1.f);
}

bool FTSAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	if (!bClipsMeasured)
	{
		MeasureClips(Bones);
	}

	EvaluateSamples(StandSamples, Output);

	const FCompactPoseBoneIndex PelvisIndex = TSAnim::FindBone(Bones, TSAnim::Pelvis);
	StandPelvisRotation = PelvisIndex != INDEX_NONE ? Output.Pose[PelvisIndex].GetRotation() : FQuat::Identity;

	ApplyCrouchLegs(Output);
	ApplyUpperBodySlot(Output);
	CounterRotateTorso(Output);
	ApplyTorsoLookAndLean(Output);
	ApplyAimOffset(Output);
	return true;
}
