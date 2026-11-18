#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SquadAction.h"
#include "SquadBrainComponent.generated.h"

/**
 *  Utility-based decision maker. Every EvaluationInterval it scores all actions, adds a momentum
 *  bonus to the running one (so agents commit instead of dithering) and switches when another
 *  action clearly wins.
 */
UCLASS(ClassGroup = (SquadAI), meta = (BlueprintSpawnableComponent))
class SQUADAI_API USquadBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	USquadBrainComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Stops the current action and halts decision making (death, possession change). */
	void Shutdown();

	UFUNCTION(BlueprintPure, Category = "Squad AI")
	FName GetActiveActionName() const;

	const TArray<FSquadActionScore>& GetLastScores() const { return LastScores; }

	const FSquadContext& GetLastContext() const { return LastContext; }

protected:

	UPROPERTY(EditAnywhere, Instanced, Category = "Squad AI")
	TArray<TObjectPtr<USquadAction>> Actions;

	UPROPERTY(EditAnywhere, Category = "Squad AI", meta = (ClampMin = "0.05", Units = "s"))
	float EvaluationInterval = 0.25f;

	/** Bonus added to the running action's score. Prevents flip-flopping between close options. */
	UPROPERTY(EditAnywhere, Category = "Squad AI", meta = (ClampMin = "0"))
	float MomentumBonus = 0.15f;

private:

	FSquadContext BuildContext() const;
	void Evaluate(const FSquadContext& Context);
	void SwitchTo(USquadAction* NewAction, const FSquadContext& Context);
	void FinishActive(const FSquadContext& Context, ESquadActionStatus Result);

	UPROPERTY()
	TObjectPtr<USquadAction> ActiveAction;

	TArray<FSquadActionScore> LastScores;
	FSquadContext LastContext;
	float EvaluationTimer = 0.f;
	bool bShutdown = false;
};
