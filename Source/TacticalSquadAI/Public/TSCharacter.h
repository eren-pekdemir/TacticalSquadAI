#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "SquadCombatant.h"
#include "TSCharacter.generated.h"

class ATSWeapon;
class UAnimSequenceBase;
class USkeletalMesh;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, class ATSCharacter*);

/**
 *  Rifle-carrying soldier body, used by both the player and the AI.
 *  Implements ISquadCombatant so the Squad AI plugin can drive it without knowing any game code.
 */
UCLASS(Abstract)
class TACTICALSQUADAI_API ATSCharacter : public ACharacter, public ISquadCombatant, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:

	ATSCharacter();

	virtual void Tick(float DeltaTime) override;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ---- ISquadCombatant --------------------------------------------------------------------

	virtual bool IsCombatantAlive() const override { return !bDead; }
	virtual float GetCombatantHealthFraction() const override { return Health / MaxHealth; }
	virtual FVector GetCombatantEyeLocation() const override;
	virtual void SetCombatantFiring(bool bFire, const FVector& AimPoint, float SpreadDegrees) override;
	virtual float GetCombatantAmmoFraction() const override;
	virtual bool IsCombatantReloading() const override;
	virtual void StartCombatantReload() override;
	virtual void SetCombatantSprinting(bool bSprint) override;
	virtual void SetCombatantStrafing(bool bStrafe) override;
	virtual float GetCombatantEngagementRange() const override;
	virtual int32 GetCombatantSquadId() const override { return SquadId; }
	virtual float GetCombatantBravery() const override { return Bravery; }
	virtual void SetCombatantCrouched(bool bCrouch) override;
	virtual void SetCombatantLean(float InLean) override { Lean = FMath::Clamp(InLean, -1.f, 1.f); }

	// ---- IGenericTeamAgentInterface ---------------------------------------------------------

	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamId); }

	// ---- State ------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Character")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Character")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Character")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Character")
	bool IsSprinting() const { return bSprinting; }

	UFUNCTION(BlueprintPure, Category = "Character")
	bool IsStrafing() const { return bStrafing; }

	/** Requested lean around cover, -1 left .. +1 right. The animation eases into it. */
	float GetLean() const { return Lean; }

	ATSWeapon* GetWeapon() const { return Weapon; }

	/** World time of the last hit taken, for HUD damage feedback. */
	double GetLastDamageTime() const { return LastDamageTime; }
	FVector GetLastDamageSource() const { return LastDamageSource; }

	FOnCharacterDied OnDied;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Die(const FVector& KillImpulse, const FName& HitBone);
	void ApplyMovementSpeed();
	void PlayUpperBodyAnim(UAnimSequenceBase* Animation, float PlayRate = 1.f) const;

	UPROPERTY(EditAnywhere, Category = "Character")
	uint8 TeamId = 0;

	UPROPERTY(EditAnywhere, Category = "Character")
	int32 SquadId = 0;

	/** 0..1 personality trait read by the AI's morale model. */
	UPROPERTY(EditAnywhere, Category = "Character", meta = (ClampMin = "0", ClampMax = "1"))
	float Bravery = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Character", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, Category = "Character|Movement", meta = (Units = "cm/s"))
	float WalkSpeed = 260.f;

	UPROPERTY(EditAnywhere, Category = "Character|Movement", meta = (Units = "cm/s"))
	float JogSpeed = 420.f;

	UPROPERTY(EditAnywhere, Category = "Character|Movement", meta = (Units = "cm/s"))
	float SprintSpeed = 620.f;

	UPROPERTY(EditAnywhere, Category = "Character|Weapon")
	TSubclassOf<ATSWeapon> WeaponClass;

	UPROPERTY(EditAnywhere, Category = "Character|Weapon")
	/** SKM_Manny_Simple has no weapon_r bone, so the rifle rides on the right hand. */
	FName WeaponSocket = TEXT("hand_r");

	UPROPERTY(EditAnywhere, Category = "Character|Weapon")
	FTransform WeaponAttachOffset;

	UPROPERTY(EditAnywhere, Category = "Character|Animation")
	TObjectPtr<UAnimSequenceBase> FireAnimation;

	UPROPERTY(EditAnywhere, Category = "Character|Animation")
	TObjectPtr<UAnimSequenceBase> ReloadAnimation;

	UPROPERTY(EditAnywhere, Category = "Character|Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> HitReactAnimations;

	/** Armour paint. Applied through dynamic material instances on every body slot. */
	UPROPERTY(EditAnywhere, Category = "Character|Look")
	FLinearColor PaintTint = FLinearColor(0.8f, 0.8f, 0.8f);

	/** Glowing chest/visor logo: the team colour players read at a glance. */
	UPROPERTY(EditAnywhere, Category = "Character|Look")
	FLinearColor AccentTint = FLinearColor(0.2f, 0.8f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Character|Look", meta = (ClampMin = "0", ClampMax = "1"))
	float PaintMetallic = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Character|Look", meta = (ClampMin = "0", ClampMax = "1"))
	float PaintRoughness = 0.55f;

	UPROPERTY(EditAnywhere, Category = "Character|Look", meta = (ClampMin = "0"))
	float AccentGlow = 8.f;

	void ApplyLook();

	/**
	 *  Optional character models. When set, one is picked at random and shown instead of the mannequin,
	 *  which keeps running animation, collision and ragdoll hidden underneath (leader pose). Any mesh
	 *  built on the UE5 mannequin bone names works.
	 */
	UPROPERTY(EditAnywhere, Category = "Character|Look")
	TArray<TSoftObjectPtr<USkeletalMesh>> VisualMeshes;

	/** Shown model, following the mannequin's pose. Null when the mannequin itself is shown. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> VisualBody;

	void SetupVisualBody();

	UPROPERTY()
	TObjectPtr<ATSWeapon> Weapon;

	float Health = 100.f;
	bool bDead = false;
	bool bSprinting = false;
	bool bStrafing = false;
	bool bAiming = false;
	float Lean = 0.f;
	double LastDamageTime = -100.0;
	FVector LastDamageSource = FVector::ZeroVector;

private:

	void HandleWeaponFired();
};
