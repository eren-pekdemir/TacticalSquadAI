#include "TSGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "TSHUD.h"
#include "TSPlayerCharacter.h"

ATSGameMode::ATSGameMode()
{
	DefaultPawnClass = ATSPlayerCharacter::StaticClass();
	HUDClass = ATSHUD::StaticClass();
}

APawn* ATSGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	APawn* Pawn = Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	if (ATSCharacter* Player = Cast<ATSCharacter>(Pawn))
	{
		Player->OnDied.AddUObject(this, &ATSGameMode::HandlePlayerDied);
	}
	return Pawn;
}

void ATSGameMode::HandlePlayerDied(ATSCharacter* Player)
{
	GetWorldTimerManager().SetTimer(RestartTimer, this, &ATSGameMode::RestartLevel, RestartDelay, false);
}

void ATSGameMode::RestartLevel()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}
