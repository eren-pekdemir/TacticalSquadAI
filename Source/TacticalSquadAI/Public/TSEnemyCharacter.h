#pragma once

#include "CoreMinimal.h"
#include "TSCharacter.h"
#include "TSEnemyCharacter.generated.h"

/**
 *  Hostile soldier driven by the Squad AI plugin. Place several in a level with the same
 *  SquadId and they fight as a team.
 */
UCLASS()
class TACTICALSQUADAI_API ATSEnemyCharacter : public ATSCharacter
{
	GENERATED_BODY()

public:

	ATSEnemyCharacter();

protected:

	virtual void BeginPlay() override;

	/** Damage per bullet. Lower than the player's: the squad wins by tactics, not by aimbot. */
	UPROPERTY(EditAnywhere, Category = "Character|Weapon", meta = (ClampMin = "0"))
	float EnemyDamage = 4.f;

	/** Pick a random bravery in this range on spawn, so every squad has steady and nervous members. */
	UPROPERTY(EditAnywhere, Category = "Character")
	FVector2D BraveryRange = FVector2D(0.45f, 0.95f);

	UPROPERTY(EditAnywhere, Category = "Character")
	bool bRandomizeBravery = true;
};
