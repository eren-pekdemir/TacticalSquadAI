#include "TSPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "SquadAIStatics.h"
#include "TimerManager.h"
#include "TSWeapon.h"

namespace TSPlayer
{
	constexpr float AimTraceDistance = 10000.f;
	constexpr float CameraInterpSpeed = 12.f;
	constexpr float HipSpread = 2.2f;
	constexpr float AimSpread = 0.5f;
	constexpr float SpreadPerShot = 0.35f;
	constexpr float MaxSpread = 7.f;
	constexpr float SpreadRecovery = 6.f;
	constexpr float MoveSpreadAtFullSpeed = 3.f;
	constexpr float AirSpread = 4.f;
	constexpr float FootstepInterval = 0.35f;
	constexpr float NoisySpeed = 350.f;
}

ATSPlayerCharacter::ATSPlayerCharacter()
{
	TeamId = 0;
	SquadId = -1;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = HipArmLength;
	CameraBoom->SocketOffset = HipSocketOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 18.f;
	CameraBoom->ProbeSize = 16.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(HipFOV);

	// Showcase model (local Fab pack); falls back to the tinted mannequin when absent.
	VisualMeshes.Add(TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Adventures_Pack/Mesh/Adventure_05/Mesh_UE5/Full/SK_UE5_Adventure_05_Full_01.SK_UE5_Adventure_05_Full_01"))));

	// Desert tan kit with a cyan glow, clearly not one of them.
	PaintTint = FLinearColor(0.55f, 0.47f, 0.34f);
	AccentTint = FLinearColor(0.1f, 0.75f, 1.f);
	PaintMetallic = 0.3f;
	PaintRoughness = 0.5f;

	// The player is a bit sturdier than a grunt and is never "scared".
	MaxHealth = 150.f;
	Bravery = 1.f;
}

void ATSPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Weapon)
	{
		Weapon->Damage = 26.f;
		Weapon->OnFired.AddWeakLambda(this, [this]()
		{
			CurrentSpread = FMath::Min(CurrentSpread + TSPlayer::SpreadPerShot, TSPlayer::MaxSpread);
		});
		Weapon->OnHitPawn.AddWeakLambda(this, [this](bool bKilled, bool bHeadshot)
		{
			LastHitMarkerTime = GetWorld()->GetTimeSeconds();
			bLastHitKill = bKilled;
		});
	}
}

// ---- Input --------------------------------------------------------------------------------------

void ATSPlayerCharacter::BuildInput()
{
	if (MappingContext)
	{
		return;
	}

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};

	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	FireAction = MakeAction(TEXT("IA_Fire"), EInputActionValueType::Boolean);
	AimAction = MakeAction(TEXT("IA_Aim"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	ReloadAction = MakeAction(TEXT("IA_Reload"), EInputActionValueType::Boolean);
	InsightAction = MakeAction(TEXT("IA_Insight"), EInputActionValueType::Boolean);
	CoverDebugAction = MakeAction(TEXT("IA_CoverDebug"), EInputActionValueType::Boolean);

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Player"));

	// WASD -> 2D axis: W/S drive Y (swizzled), A/D drive X, S and A negated.
	auto MapMoveKey = [this](const FKey& Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(MoveAction, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(MappingContext));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
	};
	MapMoveKey(EKeys::W, true, false);
	MapMoveKey(EKeys::S, true, true);
	MapMoveKey(EKeys::D, false, false);
	MapMoveKey(EKeys::A, false, true);

	FEnhancedActionKeyMapping& LookMapping = MappingContext->MapKey(LookAction, EKeys::Mouse2D);
	UInputModifierNegate* InvertY = NewObject<UInputModifierNegate>(MappingContext);
	InvertY->bX = false;
	InvertY->bY = true;
	InvertY->bZ = false;
	LookMapping.Modifiers.Add(InvertY);

	MappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	MappingContext->MapKey(FireAction, EKeys::LeftMouseButton);
	MappingContext->MapKey(AimAction, EKeys::RightMouseButton);
	MappingContext->MapKey(SprintAction, EKeys::LeftShift);
	MappingContext->MapKey(ReloadAction, EKeys::R);
	MappingContext->MapKey(InsightAction, EKeys::F1);
	MappingContext->MapKey(CoverDebugAction, EKeys::F2);
}

void ATSPlayerCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	BuildInput();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

void ATSPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATSPlayerCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATSPlayerCharacter::Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(FireAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::FirePressed);
	Input->BindAction(FireAction, ETriggerEvent::Completed, this, &ATSPlayerCharacter::FireReleased);
	Input->BindAction(AimAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::AimPressed);
	Input->BindAction(AimAction, ETriggerEvent::Completed, this, &ATSPlayerCharacter::AimReleased);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::SprintPressed);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ATSPlayerCharacter::SprintReleased);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::ReloadPressed);
	Input->BindAction(InsightAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::ToggleInsight);
	Input->BindAction(CoverDebugAction, ETriggerEvent::Started, this, &ATSPlayerCharacter::ToggleCoverDebug);
}

void ATSPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (bDead || !Controller)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void ATSPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Sensitivity = bAimHeld ? AimFOV / HipFOV : 1.f;
	AddControllerYawInput(Axis.X * Sensitivity);
	AddControllerPitchInput(Axis.Y * Sensitivity);
}

void ATSPlayerCharacter::FirePressed()
{
	bFireHeld = true;
	if (Weapon && Weapon->IsReloading())
	{
		return;
	}
	if (Weapon && Weapon->GetAmmo() <= 0)
	{
		StartCombatantReload();
	}
}

void ATSPlayerCharacter::FireReleased()
{
	bFireHeld = false;
}

void ATSPlayerCharacter::AimPressed()
{
	bAimHeld = true;
}

void ATSPlayerCharacter::AimReleased()
{
	bAimHeld = false;
}

void ATSPlayerCharacter::SprintPressed()
{
	bSprintHeld = true;
}

void ATSPlayerCharacter::SprintReleased()
{
	bSprintHeld = false;
}

void ATSPlayerCharacter::ReloadPressed()
{
	StartCombatantReload();
}

void ATSPlayerCharacter::ToggleInsight()
{
	bInsightOverlay = !bInsightOverlay;
}

void ATSPlayerCharacter::ToggleCoverDebug()
{
	bCoverDebug = !bCoverDebug;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->ConsoleCommand(bCoverDebug ? TEXT("squad.DebugCover 1") : TEXT("squad.DebugCover 0"));
	}
}

// ---- Per frame ----------------------------------------------------------------------------------

void ATSPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDead)
	{
		return;
	}

	const bool bCombatStance = bAimHeld || bFireHeld;
	SetCombatantStrafing(bCombatStance);
	SetCombatantSprinting(bSprintHeld && !bCombatStance);

	UpdateCamera(DeltaTime);
	UpdateWeaponAim(DeltaTime);
	UpdateFootstepNoise(DeltaTime);
	UpdateRegen(DeltaTime);
}

void ATSPlayerCharacter::UpdateCamera(float DeltaTime)
{
	const float Alpha = FMath::Clamp(DeltaTime * TSPlayer::CameraInterpSpeed, 0.f, 1.f);
	CameraBoom->TargetArmLength = FMath::Lerp(CameraBoom->TargetArmLength, bAimHeld ? AimArmLength : HipArmLength, Alpha);
	CameraBoom->SocketOffset = FMath::Lerp(CameraBoom->SocketOffset, bAimHeld ? AimSocketOffset : HipSocketOffset, Alpha);
	FollowCamera->SetFieldOfView(FMath::Lerp(FollowCamera->FieldOfView, bAimHeld ? AimFOV : HipFOV, Alpha));
}

void ATSPlayerCharacter::UpdateWeaponAim(float DeltaTime)
{
	if (!Weapon)
	{
		return;
	}

	// Bloom: grows per shot (see BeginPlay), recovers over time, widened by movement and jumping.
	const float BaseSpread = bAimHeld ? TSPlayer::AimSpread : TSPlayer::HipSpread;
	const float MoveSpread = FMath::Clamp(GetVelocity().Size2D() / SprintSpeed, 0.f, 1.f) * TSPlayer::MoveSpreadAtFullSpeed;
	const float AirSpread = GetCharacterMovement()->IsFalling() ? TSPlayer::AirSpread : 0.f;
	const float Target = BaseSpread + MoveSpread + AirSpread;
	CurrentSpread = FMath::Max(Target, CurrentSpread - TSPlayer::SpreadRecovery * DeltaTime);

	// Shoot where the crosshair points, not where the gun points.
	const FVector CameraLocation = FollowCamera->GetComponentLocation();
	const FVector CameraForward = FollowCamera->GetForwardVector();
	const FVector TraceEnd = CameraLocation + CameraForward * TSPlayer::AimTraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSPlayerAim), true, this);
	if (Weapon)
	{
		Params.AddIgnoredActor(Weapon);
	}

	FHitResult Hit;
	const FVector AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, TraceEnd, ECC_Visibility, Params) ? Hit.ImpactPoint : TraceEnd;

	const bool bCanFire = bFireHeld && !bSprinting;
	SetCombatantFiring(bCanFire, AimPoint, CurrentSpread);
}

void ATSPlayerCharacter::UpdateFootstepNoise(float DeltaTime)
{
	const float Speed = GetVelocity().Size2D();
	if (Speed < TSPlayer::NoisySpeed || GetCharacterMovement()->IsFalling())
	{
		FootstepTimer = 0.f;
		return;
	}

	// Walking while aiming is silent; jogging and especially sprinting give the player away.
	FootstepTimer -= DeltaTime;
	if (FootstepTimer <= 0.f)
	{
		FootstepTimer = TSPlayer::FootstepInterval;
		const float Range = bSprinting ? 1400.f : 700.f;
		USquadAIStatics::ReportNoise(this, this, GetActorLocation(), bSprinting ? 0.8f : 0.4f, Range, TEXT("Footstep"));
	}
}

void ATSPlayerCharacter::UpdateRegen(float DeltaTime)
{
	if (GetWorld()->GetTimeSeconds() - LastDamageTime > RegenDelay)
	{
		Health = FMath::Min(MaxHealth, Health + RegenPerSecond * DeltaTime);
	}
}

void ATSPlayerCharacter::SquadTestScenario(float ExposeSeconds)
{
	SquadTestTeleport(-1300.f, -200.f);

	FTimerHandle HideTimer;
	GetWorldTimerManager().SetTimer(HideTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// Behind the bunker, out of every enemy's sight.
		SetActorLocation(FVector(0.f, -1800.f, GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Display, TEXT("SquadTestScenario: player hidden."));
	}), ExposeSeconds, false);
}

void ATSPlayerCharacter::SquadTestTeleport(float X, float Y)
{
	SetActorLocation(FVector(X, Y, GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
	if (Weapon)
	{
		USquadAIStatics::ReportShot(this, this, GetActorLocation(), GetActorLocation() + GetActorForwardVector() * 1000.f);
	}
	UE_LOG(LogTemp, Display, TEXT("SquadTestTeleport: player moved to (%.0f, %.0f)."), X, Y);
}
