#include "TSCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TSAnimInstance.h"
#include "TSWeapon.h"
#include "UObject/ConstructorHelpers.h"

namespace TSCharacterDefaults
{
		// Velocity change in cm/s applied to the bone that took the killing shot.
	constexpr float DeathImpulse = 450.f;
	constexpr float CorpseLifeSpan = 45.f;
	constexpr float EyeHeightOffset = 70.f;
	/** Kneeling: the capsule shrinks to this and the eyes drop to roughly 1.1 m. */
	constexpr float CrouchedHalfHeight = 64.f;
	constexpr float CrouchedEyeHeightOffset = 45.f;
	constexpr float CrouchedSpeed = 170.f;
}

ATSCharacter::ATSCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);
	// Bullets and sight traces should hit the skeletal mesh (for headshots), not the capsule.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));
	Body->SetCollisionProfileName(TEXT("CharacterMesh"));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MannyMesh.Succeeded())
	{
		Body->SetSkeletalMesh(MannyMesh.Object);
	}

	// Locomotion, aim offset and upper-body actions are evaluated natively (no Animation Blueprint).
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UTSAnimInstance::StaticClass());

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Fire(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire.MM_Rifle_Fire"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Reload(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"));
	FireAnimation = Fire.Object;
	ReloadAnimation = Reload.Object;

	static const TCHAR* HitReactPaths[] = {
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01.MM_HitReact_Front_Lgt_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_02.MM_HitReact_Front_Lgt_02"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_03.MM_HitReact_Front_Lgt_03"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01.MM_HitReact_Front_Med_01"),
	};
	for (const TCHAR* Path : HitReactPaths)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequenceBase> HitReact(Path);
		if (HitReact.Succeeded())
		{
			HitReactAnimations.Add(HitReact.Object);
		}
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->MaxWalkSpeed = JogSpeed;
	Movement->MaxAcceleration = 1800.f;
	Movement->BrakingDecelerationWalking = 1600.f;
	Movement->GroundFriction = 6.f;
	Movement->bUseSeparateBrakingFriction = false;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	Movement->SetCrouchedHalfHeight(TSCharacterDefaults::CrouchedHalfHeight);
	Movement->MaxWalkSpeedCrouched = TSCharacterDefaults::CrouchedSpeed;

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	WeaponClass = ATSWeapon::StaticClass();
	// Grip position comes from the rifle clips' weapon_r track (relative to hand_r); the rotation
	// turns SM_Rifle's +Y barrel axis to point where the idle aim pose points. Measured in editor.
	WeaponAttachOffset = FTransform(FRotator(9.83f, 89.48f, 11.9f), FVector(-7.22f, 3.42f, -0.95f));
}

void ATSCharacter::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;
	ApplyMovementSpeed();
	SetupVisualBody();
	ApplyLook();

	if (WeaponClass)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.Instigator = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Weapon = GetWorld()->SpawnActor<ATSWeapon>(WeaponClass, GetActorTransform(), Params);
		if (Weapon)
		{
			Weapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponSocket);
			Weapon->SetActorRelativeTransform(WeaponAttachOffset);
			Weapon->OnFired.AddUObject(this, &ATSCharacter::HandleWeaponFired);
		}
	}
}

void ATSCharacter::SetupVisualBody()
{
	if (VisualMeshes.IsEmpty())
	{
		return;
	}

	USkeletalMesh* Model = VisualMeshes[FMath::RandRange(0, VisualMeshes.Num() - 1)].LoadSynchronous();
	if (!Model)
	{
		return;
	}

	VisualBody = NewObject<USkeletalMeshComponent>(this, TEXT("VisualBody"));
	VisualBody->SetupAttachment(GetMesh());
	VisualBody->SetSkeletalMesh(Model);
	VisualBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualBody->SetCastShadow(true);
	VisualBody->RegisterComponent();
	VisualBody->SetLeaderPoseComponent(GetMesh());

	// The mannequin keeps animating, colliding and ragdolling; it just is not drawn.
	GetMesh()->SetVisibility(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void ATSCharacter::ApplyLook()
{
	if (VisualBody)
	{
		return; // Authored models keep their own materials.
	}

	USkeletalMeshComponent* Body = GetMesh();
	for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
	{
		UMaterialInstanceDynamic* Material = Body->CreateAndSetMaterialInstanceDynamic(Slot);
		if (!Material)
		{
			continue;
		}
		Material->SetVectorParameterValue(TEXT("Paint Tint"), PaintTint);
		Material->SetVectorParameterValue(TEXT("LogoTint"), AccentTint * AccentGlow);
		Material->SetScalarParameterValue(TEXT("MetalPaintMetallic"), PaintMetallic);
		Material->SetScalarParameterValue(TEXT("MetalPaintRoughness"), PaintRoughness);
		Material->SetScalarParameterValue(TEXT("EmissivePower"), AccentGlow);
	}
}

void ATSCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Weapon && !bDead)
	{
		Weapon->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void ATSCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FVector ATSCharacter::GetCombatantEyeLocation() const
{
	const float Offset = bIsCrouched ? TSCharacterDefaults::CrouchedEyeHeightOffset : TSCharacterDefaults::EyeHeightOffset;
	return GetActorLocation() + FVector(0.f, 0.f, Offset);
}

void ATSCharacter::SetCombatantCrouched(bool bCrouch)
{
	if (bDead)
	{
		return;
	}

	if (bCrouch)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}

// ---- Weapon -------------------------------------------------------------------------------------

void ATSCharacter::SetCombatantFiring(bool bFire, const FVector& AimPoint, float SpreadDegrees)
{
	if (!Weapon || bDead)
	{
		return;
	}

	if (bFire)
	{
		Weapon->SetAim(AimPoint, SpreadDegrees);
	}
	Weapon->SetTriggerHeld(bFire);
}

float ATSCharacter::GetCombatantAmmoFraction() const
{
	return Weapon ? Weapon->GetAmmoFraction() : 0.f;
}

bool ATSCharacter::IsCombatantReloading() const
{
	return Weapon && Weapon->IsReloading();
}

void ATSCharacter::StartCombatantReload()
{
	if (!Weapon || bDead || !Weapon->CanReload())
	{
		return;
	}

	Weapon->StartReload();
	if (ReloadAnimation)
	{
		PlayUpperBodyAnim(ReloadAnimation, ReloadAnimation->GetPlayLength() / Weapon->ReloadTime);
	}
}

float ATSCharacter::GetCombatantEngagementRange() const
{
	return Weapon ? Weapon->GetEffectiveRange() : 2000.f;
}

void ATSCharacter::HandleWeaponFired()
{
	PlayUpperBodyAnim(FireAnimation);
}

// ---- Movement -----------------------------------------------------------------------------------

void ATSCharacter::SetCombatantSprinting(bool bSprint)
{
	if (bSprinting != bSprint)
	{
		bSprinting = bSprint;
		ApplyMovementSpeed();
	}
}

void ATSCharacter::SetCombatantStrafing(bool bStrafe)
{
	if (bStrafing == bStrafe)
	{
		return;
	}

	bStrafing = bStrafe;
	GetCharacterMovement()->bOrientRotationToMovement = !bStrafe;
	GetCharacterMovement()->bUseControllerDesiredRotation = bStrafe;
	ApplyMovementSpeed();
}

void ATSCharacter::ApplyMovementSpeed()
{
	float Speed = JogSpeed;
	if (bSprinting && !bStrafing)
	{
		Speed = SprintSpeed;
	}
	else if (bStrafing)
	{
		Speed = WalkSpeed;
	}
	GetCharacterMovement()->MaxWalkSpeed = Speed;
}

// ---- Damage -------------------------------------------------------------------------------------

float ATSCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDead)
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied <= 0.f)
	{
		return 0.f;
	}

	Health = FMath::Max(0.f, Health - Applied);
	LastDamageTime = GetWorld()->GetTimeSeconds();
	if (const APawn* Shooter = EventInstigator ? EventInstigator->GetPawn() : nullptr)
	{
		LastDamageSource = Shooter->GetActorLocation();
	}

	FVector ShotDirection = (GetActorLocation() - LastDamageSource).GetSafeNormal();
	FName HitBone = NAME_None;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		ShotDirection = PointDamage.ShotDirection;
		HitBone = PointDamage.HitInfo.BoneName;
	}

	if (Health <= 0.f)
	{
		Die(ShotDirection * TSCharacterDefaults::DeathImpulse, HitBone);
	}
	else if (HitReactAnimations.Num() > 0)
	{
		PlayUpperBodyAnim(HitReactAnimations[FMath::RandRange(0, HitReactAnimations.Num() - 1)]);
	}

	return Applied;
}

void ATSCharacter::Die(const FVector& KillImpulse, const FName& HitBone)
{
	bDead = true;

	if (Weapon)
	{
		Weapon->Drop();
	}

	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Full ragdoll, pushed by the killing shot.
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetSimulatePhysics(true);
	Body->AddImpulse(KillImpulse, HitBone, true);

	SetLifeSpan(TSCharacterDefaults::CorpseLifeSpan);
	OnDied.Broadcast(this);
}

void ATSCharacter::PlayUpperBodyAnim(UAnimSequenceBase* Animation, float PlayRate) const
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Animation && AnimInstance && !bDead)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Animation, UTSAnimInstance::UpperBodySlot, 0.08f, 0.15f, PlayRate);
	}
}
