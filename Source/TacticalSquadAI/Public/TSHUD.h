#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TSHUD.generated.h"

class ASquadAIController;
class ATSPlayerCharacter;
class UFont;

/**
 *  Canvas HUD: crosshair, health, ammo and hit feedback, plus the "AI Insight" overlay that makes
 *  the squad's reasoning visible: each soldier's role, current decision, morale and suppression,
 *  speech bubbles, and ghost markers where the AI *believes* the player is.
 */
UCLASS()
class TACTICALSQUADAI_API ATSHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

private:

	void DrawCrosshair(const ATSPlayerCharacter& Player);
	void DrawVitals(const ATSPlayerCharacter& Player);
	void DrawDamageFeedback(const ATSPlayerCharacter& Player);
	void DrawInsight(const ATSPlayerCharacter& Player);
	void DrawAgentCard(const ASquadAIController& Agent, const FVector2D& Anchor, float Distance);
	void DrawBarks(const ASquadAIController& Agent, const FVector2D& Anchor);
	void DrawBeliefs(const ATSPlayerCharacter& Player);
	void DrawSquadPanel();
	void DrawHelp(const ATSPlayerCharacter& Player);

	/** World to screen; false when behind the camera or off screen. */
	bool ProjectToScreen(const FVector& World, FVector2D& OutScreen) const;

	void DrawPanel(float X, float Y, float W, float H, const FLinearColor& Color);
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill);
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale = 1.f, bool bCentered = false);
	FVector2D MeasureText(const FString& Text, UFont* Font, float Scale = 1.f);

	float UIScale = 1.f;
};
