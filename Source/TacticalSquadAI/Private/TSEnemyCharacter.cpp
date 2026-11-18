#include "TSEnemyCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "SquadAIController.h"
#include "TSWeapon.h"
#include "UObject/ConstructorHelpers.h"

ATSEnemyCharacter::ATSEnemyCharacter()
{
	TeamId = 1;
	SquadId = 1;

	AIControllerClass = ASquadAIController::StaticClass();

	// Showcase models (local Fab pack). Missing content falls back to the tinted mannequin below.
	static const TCHAR* SquadModels[] = {
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_01/Mesh_UE5/Full/SK_UE5_Adventure_01_Full_01.SK_UE5_Adventure_01_Full_01"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_01/Mesh_UE5/Full/SK_UE5_Adventure_01_Full_02.SK_UE5_Adventure_01_Full_02"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_02/Mesh_UE5/Full/SK_UE5_Adventure_02_Full_01.SK_UE5_Adventure_02_Full_01"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_02/Mesh_UE5/Full/SK_UE5_Adventure_02_Full_02.SK_UE5_Adventure_02_Full_02"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_02/Mesh_UE5/Full/SK_UE5_Adventure_02_Full_03.SK_UE5_Adventure_02_Full_03"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_03/Mesh_UE5/Full/SK_UE5_Adventure_03_Full_01.SK_UE5_Adventure_03_Full_01"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_03/Mesh_UE5/Full/SK_UE5_Adventure_03_Full_02.SK_UE5_Adventure_03_Full_02"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_04/Mesh_UE5/Full/SK_UE5_Adventure_04_Full_01.SK_UE5_Adventure_04_Full_01"),
		TEXT("/Game/Adventures_Pack/Mesh/Adventure_04/Mesh_UE5/Full/SK_UE5_Adventure_04_Full_02.SK_UE5_Adventure_04_Full_02"),
	};
	for (const TCHAR* Model : SquadModels)
	{
		VisualMeshes.Add(TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Model)));
	}

	// Hostile look: dark matte kit with a red glow.
	PaintTint = FLinearColor(0.11f, 0.12f, 0.085f);
	AccentTint = FLinearColor(1.f, 0.12f, 0.05f);
	PaintMetallic = 0.15f;
	PaintRoughness = 0.65f;

	// Turn like a person carrying a rifle, not a turret.
	GetCharacterMovement()->RotationRate = FRotator(0.f, 360.f, 0.f);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Quinn body so enemies read differently from the player at a glance.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> QuinnMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (QuinnMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(QuinnMesh.Object);
	}
}

void ATSEnemyCharacter::BeginPlay()
{
	if (bRandomizeBravery)
	{
		Bravery = FMath::FRandRange(BraveryRange.X, BraveryRange.Y);
	}

	// Each soldier's kit is a slightly different shade of the squad's olive drab.
	const float Shade = FMath::FRandRange(0.85f, 1.15f);
	PaintTint = FLinearColor(0.11f * Shade, 0.12f * Shade, 0.085f * Shade);

	Super::BeginPlay();

	if (Weapon)
	{
		Weapon->Damage = EnemyDamage;
		Weapon->FireInterval = 0.15f;
	}
}
