#include "TSWeapon.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "SquadAIStatics.h"
#include "TimerManager.h"
#include "TSCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace TSWeaponDefaults
{
	const FName TracerMuzzlePosition(TEXT("MuzzlePosition"));
	const FName TracerImpactPositions(TEXT("ImpactPositions"));
	const FName TracerTrigger(TEXT("Trigger"));
	const FName HeadBone(TEXT("head"));
	constexpr float MuzzleFlashDuration = 0.05f;
	constexpr float MuzzleFlashIntensity = 6000.f;
	constexpr float ImpactImpulse = 6000.f;
}

ATSWeapon::ATSWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetCastShadow(true);
	RootComponent = Mesh;

	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(Mesh);
	MuzzleLight->SetRelativeLocation(MuzzleOffset);
	MuzzleLight->SetIntensity(0.f);
	MuzzleLight->SetAttenuationRadius(400.f);
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.7f, 0.35f));
	MuzzleLight->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RifleMesh(TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"));
	if (RifleMesh.Succeeded())
	{
		Mesh->SetStaticMesh(RifleMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Tracer(TEXT("/Game/Effects/Particles/Weapons/NS_WeaponFire_Tracer.NS_WeaponFire_Tracer"));
	if (Tracer.Succeeded())
	{
		TracerSystem = Tracer.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> Gunshot(TEXT("/Game/FreeWeaponSFX/Cue/AssaultRifle/Gunshots/sfx_wpn_ar_gunshot_01_Cue.sfx_wpn_ar_gunshot_01_Cue"));
	if (Gunshot.Succeeded())
	{
		FireSound = Gunshot.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> Reload(TEXT("/Game/FreeWeaponSFX/Cue/AssaultRifle/Reload/sfx_wpn_ar_reload_01_magOut_Cue.sfx_wpn_ar_reload_01_magOut_Cue"));
	if (Reload.Succeeded())
	{
		ReloadSound = Reload.Object;
	}
}

void ATSWeapon::BeginPlay()
{
	Super::BeginPlay();
	Ammo = MagazineSize;
	MuzzleLight->SetRelativeLocation(MuzzleOffset);
}

void ATSWeapon::SetAim(const FVector& InAimPoint, float InSpreadDegrees)
{
	AimPoint = InAimPoint;
	SpreadDegrees = InSpreadDegrees;
}

FVector ATSWeapon::GetMuzzleLocation() const
{
	return Mesh->GetComponentTransform().TransformPosition(MuzzleOffset);
}

void ATSWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (MuzzleFlashTime > 0.f)
	{
		MuzzleFlashTime -= DeltaTime;
		MuzzleLight->SetIntensity(MuzzleFlashTime > 0.f ? TSWeaponDefaults::MuzzleFlashIntensity : 0.f);
	}

	if (!bTriggerHeld || bReloading)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextShotTime)
	{
		return;
	}

	if (Ammo <= 0)
	{
		StartReload();
		return;
	}

	NextShotTime = Now + FireInterval;
	FireShot();
}

void ATSWeapon::FireShot()
{
	--Ammo;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const FVector Muzzle = GetMuzzleLocation();
	const FVector AimDirection = (AimPoint - Muzzle).GetSafeNormal();
	const FVector ShotDirection = FMath::VRandCone(AimDirection, FMath::DegreesToRadians(SpreadDegrees * 0.5f));
	const FVector TraceEnd = Muzzle + ShotDirection * MaxRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSWeaponShot), true, this);
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, TraceEnd, ECC_Visibility, Params);
	const FVector ShotEnd = bHit ? Hit.ImpactPoint : TraceEnd;

	// Squad mates do not hurt each other; the bullet still flies (and still suppresses).
	const ATSCharacter* HitCharacter = Cast<ATSCharacter>(Hit.GetActor());
	const ATSCharacter* Shooter = Cast<ATSCharacter>(OwnerPawn);
	const bool bFriendly = HitCharacter && Shooter && HitCharacter->GetGenericTeamId() == Shooter->GetGenericTeamId();

	if (bHit && Hit.GetActor() && !bFriendly)
	{
		const bool bHeadshot = Hit.BoneName == TSWeaponDefaults::HeadBone;
		const float ShotDamage = Damage * (bHeadshot ? HeadshotMultiplier : 1.f);
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;

		const ATSCharacter* Victim = Cast<ATSCharacter>(Hit.GetActor());
		const bool bWasAlive = Victim && Victim->IsCombatantAlive();
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), ShotDamage, ShotDirection, Hit, InstigatorController, this, UDamageType::StaticClass());

		if (bWasAlive)
		{
			OnHitPawn.Broadcast(!Victim->IsCombatantAlive(), bHeadshot);
		}

		if (UPrimitiveComponent* HitComponent = Hit.GetComponent(); HitComponent && HitComponent->IsSimulatingPhysics())
		{
			HitComponent->AddImpulseAtLocation(ShotDirection * TSWeaponDefaults::ImpactImpulse, Hit.ImpactPoint, Hit.BoneName);
		}
	}

	SpawnTracer(Muzzle, ShotEnd);
	MuzzleFlashTime = TSWeaponDefaults::MuzzleFlashDuration;

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Muzzle, FMath::FRandRange(0.85f, 1.f), FMath::FRandRange(0.95f, 1.05f));
	}

	// The AI hears the shot and feels bullets that fly past.
	USquadAIStatics::ReportShot(this, OwnerPawn, Muzzle, ShotEnd, 1.f, GunshotNoiseRange);

	OnFired.Broadcast();
}

void ATSWeapon::SpawnTracer(const FVector& Start, const FVector& End) const
{
	if (!TracerSystem)
	{
		return;
	}

	UNiagaraComponent* Tracer = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), TracerSystem, Start, FRotator::ZeroRotator, FVector(0.1f));
	if (Tracer)
	{
		Tracer->SetVariableVec3(TSWeaponDefaults::TracerMuzzlePosition, Start);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Tracer, TSWeaponDefaults::TracerImpactPositions, { End });
		Tracer->SetVariableBool(TSWeaponDefaults::TracerTrigger, true);
	}
}

void ATSWeapon::StartReload()
{
	if (!CanReload())
	{
		return;
	}

	bReloading = true;
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &ATSWeapon::FinishReload, ReloadTime, false);

	if (ReloadSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetActorLocation());
	}

	// Enemies close enough can hear a reload and use the window to push.
	USquadAIStatics::ReportNoise(this, Cast<APawn>(GetOwner()), GetActorLocation(), 0.6f, ReloadNoiseRange, TEXT("Reload"));
}

void ATSWeapon::CancelReload()
{
	bReloading = false;
	GetWorldTimerManager().ClearTimer(ReloadTimer);
}

void ATSWeapon::FinishReload()
{
	bReloading = false;
	Ammo = MagazineSize;
}

void ATSWeapon::Drop()
{
	bTriggerHeld = false;
	CancelReload();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Mesh->SetSimulatePhysics(true);
	SetLifeSpan(20.f);
}
