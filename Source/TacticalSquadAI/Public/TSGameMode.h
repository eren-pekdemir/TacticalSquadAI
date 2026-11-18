#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TSGameMode.generated.h"

class ATSCharacter;

/** Spawns the player, wires the HUD and restarts the level shortly after the player dies. */
UCLASS()
class TACTICALSQUADAI_API ATSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	ATSGameMode();

	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

protected:

	UPROPERTY(EditAnywhere, Category = "Game", meta = (Units = "s"))
	float RestartDelay = 4.f;

private:

	void HandlePlayerDied(ATSCharacter* Player);
	void RestartLevel();

	FTimerHandle RestartTimer;
};
