#include "SquadAIStatics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Perception/AISense_Hearing.h"
#include "SquadSubsystem.h"

void USquadAIStatics::ReportShot(const UObject* WorldContextObject, AActor* Shooter, const FVector& Start, const FVector& End, float Loudness, float NoiseRange)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return;
	}

	UAISense_Hearing::ReportNoiseEvent(World, Start, Loudness, Shooter, NoiseRange, TEXT("Gunshot"));

	if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		Squad->ReportBulletPath(Shooter, Start, End);
	}
}

void USquadAIStatics::ReportNoise(const UObject* WorldContextObject, AActor* Instigator, const FVector& Location, float Loudness, float Range, FName Tag)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		UAISense_Hearing::ReportNoiseEvent(World, Location, Loudness, Instigator, Range, Tag);
	}
}
