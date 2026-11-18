#pragma once

#include "CoreMinimal.h"
#include "TSCharacter.h"
#include "TSPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

/**
 *  Over-the-shoulder player soldier. Input is built in code (no input assets needed), the camera
 *  zooms when aiming, health regenerates out of combat, and movement makes noise the AI can hear.
 */
UCLASS()
class TACTICALSQUADAI_API ATSPlayerCharacter : public ATSCharacter
{
	GENERATED_BODY()

public:

	ATSPlayerCharacter();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Player")
	bool IsAiming() const { return bAimHeld; }

	/** Current crosshair spread in degrees. */
	float GetCurrentSpread() const { return CurrentSpread; }

	/** Time of the last hit landed on an enemy, and whether it killed / was a headshot (hit marker). */
	double GetLastHitMarkerTime() const { return LastHitMarkerTime; }
	bool WasLastHitKill() const { return bLastHitKill; }

	bool IsInsightOverlayEnabled() const { return bInsightOverlay; }

	/** Console: move the player to (X, Y) on the floor and fire one shot. For demos and automated tests. */
	UFUNCTION(Exec)
	void SquadTestTeleport(float X, float Y);

	/** Console: stand in the open for ExposeSeconds, then vanish behind the bunker. Exercises search behaviour. */
	UFUNCTION(Exec)
	void SquadTestScenario(float ExposeSeconds = 15.f);

protected:

	virtual void BeginPlay() override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector HipSocketOffset = FVector(0.f, 55.f, 65.f);

	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector AimSocketOffset = FVector(0.f, 45.f, 60.f);

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "cm"))
	float HipArmLength = 260.f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "cm"))
	float AimArmLength = 120.f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "deg"))
	float HipFOV = 85.f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (Units = "deg"))
	float AimFOV = 65.f;

	UPROPERTY(EditAnywhere, Category = "Health", meta = (Units = "s"))
	float RegenDelay = 5.f;

	UPROPERTY(EditAnywhere, Category = "Health")
	float RegenPerSecond = 20.f;

private:

	void BuildInput();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void FirePressed();
	void FireReleased();
	void AimPressed();
	void AimReleased();
	void SprintPressed();
	void SprintReleased();
	void ReloadPressed();
	void ToggleInsight();
	void ToggleCoverDebug();

	void UpdateCamera(float DeltaTime);
	void UpdateWeaponAim(float DeltaTime);
	void UpdateFootstepNoise(float DeltaTime);
	void UpdateRegen(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY()
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY()
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY()
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY()
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY()
	TObjectPtr<UInputAction> InsightAction;

	UPROPERTY()
	TObjectPtr<UInputAction> CoverDebugAction;

	float CurrentSpread = 1.f;
	float FootstepTimer = 0.f;
	double LastHitMarkerTime = -100.0;
	bool bLastHitKill = false;
	bool bFireHeld = false;
	bool bAimHeld = false;
	bool bSprintHeld = false;
	bool bInsightOverlay = true;
	bool bCoverDebug = false;
};
