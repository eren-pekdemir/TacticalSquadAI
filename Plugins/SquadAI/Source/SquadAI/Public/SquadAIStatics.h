#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SquadAIStatics.generated.h"

/** Entry points a game calls to feed the squad AI with events it cannot perceive on its own. */
UCLASS()
class SQUADAI_API USquadAIStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 *  A shot was fired. Makes a gunshot noise at Start (agents hear it) and suppresses agents
	 *  close to the bullet's path (near misses are scary).
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad AI", meta = (WorldContext = "WorldContextObject"))
	static void ReportShot(const UObject* WorldContextObject, AActor* Shooter, const FVector& Start, const FVector& End, float Loudness = 1.f, float NoiseRange = 4000.f);

	/** A noise agents may hear: footsteps, landing, reloading (use the "Reload" tag). */
	UFUNCTION(BlueprintCallable, Category = "Squad AI", meta = (WorldContext = "WorldContextObject"))
	static void ReportNoise(const UObject* WorldContextObject, AActor* Instigator, const FVector& Location, float Loudness, float Range, FName Tag = NAME_None);
};
