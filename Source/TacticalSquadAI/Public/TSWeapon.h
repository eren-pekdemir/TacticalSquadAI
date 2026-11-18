#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TSWeapon.generated.h"

class UNiagaraSystem;
class UPointLightComponent;
class USoundBase;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE(FOnWeaponFired);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWeaponHit, bool /*bKilled*/, bool /*bHeadshot*/);

/**
 *  Automatic hitscan rifle shared by the player and the AI.
 *  The owner decides where to aim (camera centre for the player, accuracy model for the AI);
 *  the weapon handles fire rate, ammo, reloading, damage, feedback and telling the AI about the shot.
 */
UCLASS()
class TACTICALSQUADAI_API ATSWeapon : public AActor
{
	GENERATED_BODY()

public:

	ATSWeapon();

	virtual void Tick(float DeltaTime) override;

	void SetTriggerHeld(bool bHeld) { bTriggerHeld = bHeld; }

	/** Where the next shot should go before spread, and the spread cone in degrees. */
	void SetAim(const FVector& InAimPoint, float InSpreadDegrees);

	void StartReload();
	void CancelReload();

	bool IsReloading() const { return bReloading; }
	bool CanReload() const { return !bReloading && Ammo < MagazineSize; }
	int32 GetAmmo() const { return Ammo; }
	int32 GetMagazineSize() const { return MagazineSize; }
	float GetAmmoFraction() const { return MagazineSize > 0 ? static_cast<float>(Ammo) / MagazineSize : 0.f; }
	float GetEffectiveRange() const { return EffectiveRange; }

	FVector GetMuzzleLocation() const;

	/** Drops the weapon on the floor (owner died). */
	void Drop();

	FOnWeaponFired OnFired;
	FOnWeaponHit OnHitPawn;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "0"))
	float Damage = 22.f;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "1"))
	float HeadshotMultiplier = 2.5f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "0.02", Units = "s"))
	float FireInterval = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "0.1", Units = "s"))
	float ReloadTime = 2.2f;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (Units = "cm"))
	float MaxRange = 8000.f;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (Units = "cm"))
	float EffectiveRange = 2500.f;

	/** How far the gunshot can be heard by the AI. */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (Units = "cm"))
	float GunshotNoiseRange = 4500.f;

	/** Reloading makes noise the enemy can exploit. */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (Units = "cm"))
	float ReloadNoiseRange = 1600.f;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPointLightComponent> MuzzleLight;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	TObjectPtr<UNiagaraSystem> TracerSystem;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	TObjectPtr<USoundBase> ReloadSound;

	/** Muzzle offset from the mesh origin. SM_Rifle's barrel runs along +Y. */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (Units = "cm"))
	FVector MuzzleOffset = FVector(0.f, 52.f, 4.f);

private:

	void FireShot();
	void FinishReload();
	void SpawnTracer(const FVector& Start, const FVector& End) const;

	FVector AimPoint = FVector::ZeroVector;
	float SpreadDegrees = 0.f;
	double NextShotTime = 0.0;
	float MuzzleFlashTime = 0.f;
	int32 Ammo = 0;
	bool bTriggerHeld = false;
	bool bReloading = false;
	FTimerHandle ReloadTimer;
};
