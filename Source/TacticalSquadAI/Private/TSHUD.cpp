#include "TSHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "SquadAIController.h"
#include "SquadBrainComponent.h"
#include "SquadKnowledgeComponent.h"
#include "TSPlayerCharacter.h"
#include "TSWeapon.h"

namespace TSHUDStyle
{
	const FLinearColor Panel(0.02f, 0.025f, 0.03f, 0.72f);
	const FLinearColor PanelEdge(1.f, 1.f, 1.f, 0.08f);
	const FLinearColor Text(0.92f, 0.94f, 0.96f, 1.f);
	const FLinearColor Dim(0.6f, 0.64f, 0.68f, 1.f);
	const FLinearColor Health(0.35f, 0.85f, 0.45f, 1.f);
	const FLinearColor HealthLow(0.95f, 0.3f, 0.25f, 1.f);
	const FLinearColor Morale(0.35f, 0.65f, 1.f, 1.f);
	const FLinearColor Suppression(1.f, 0.62f, 0.15f, 1.f);
	const FLinearColor Belief(1.f, 0.55f, 0.1f, 1.f);
	const FLinearColor Crosshair(1.f, 1.f, 1.f, 0.9f);
	const FLinearColor HitMarker(1.f, 1.f, 1.f, 1.f);
	const FLinearColor KillMarker(1.f, 0.25f, 0.2f, 1.f);

	constexpr float InsightRange = 4500.f;
	constexpr float BarkFadeTime = 3.5f;
	constexpr float HitMarkerTime = 0.18f;

	FLinearColor RoleColor(ESquadRole AgentRole)
	{
		switch (AgentRole)
		{
		case ESquadRole::Suppressor: return FLinearColor(1.f, 0.62f, 0.15f, 1.f);
		case ESquadRole::Flanker:    return FLinearColor(0.85f, 0.35f, 1.f, 1.f);
		case ESquadRole::Rifleman:   return FLinearColor(0.35f, 0.75f, 1.f, 1.f);
		default:                     return FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
		}
	}

	const TCHAR* RoleLabel(ESquadRole AgentRole)
	{
		switch (AgentRole)
		{
		case ESquadRole::Suppressor: return TEXT("SUPPRESSOR");
		case ESquadRole::Flanker:    return TEXT("FLANKER");
		case ESquadRole::Rifleman:   return TEXT("RIFLEMAN");
		default:                     return TEXT("SOLDIER");
		}
	}

	FString ActionLabel(const FName& Action)
	{
		static const TMap<FName, FString> Labels = {
			{ TEXT("Idle"),            TEXT("Holding") },
			{ TEXT("Search"),          TEXT("Searching") },
			{ TEXT("TakeCover"),       TEXT("Taking cover") },
			{ TEXT("AttackFromCover"), TEXT("Peek & fire") },
			{ TEXT("Suppress"),        TEXT("Suppressing") },
			{ TEXT("Flank"),           TEXT("Flanking") },
			{ TEXT("Advance"),         TEXT("Pushing") },
			{ TEXT("Reload"),          TEXT("Reloading") },
			{ TEXT("Retreat"),         TEXT("Falling back") },
			{ TEXT("EngageInOpen"),    TEXT("Engaging") },
		};
		const FString* Label = Labels.Find(Action);
		return Label ? *Label : Action.ToString();
	}
}

// ---- Drawing helpers ----------------------------------------------------------------------------

void ATSHUD::DrawPanel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	DrawRect(Color, X, Y, W, H);
	DrawRect(TSHUDStyle::PanelEdge, X, Y, W, 1.f);
}

void ATSHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill)
{
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.12f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
}

FVector2D ATSHUD::MeasureText(const FString& Text, UFont* Font, float Scale)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	return FVector2D(Width, Height);
}

void ATSHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCentered)
{
	if (bCentered)
	{
		X -= MeasureText(Text, Font, Scale).X * 0.5f;
	}
	DrawText(Text, Color, X, Y, Font, Scale);
}

bool ATSHUD::ProjectToScreen(const FVector& World, FVector2D& OutScreen) const
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager || !Canvas)
	{
		return false;
	}

	const FVector CameraLocation = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector CameraForward = PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector();
	if (FVector::DotProduct(World - CameraLocation, CameraForward) <= 0.f)
	{
		return false;
	}

	const FVector Projected = Canvas->Project(World);
	OutScreen = FVector2D(Projected.X, Projected.Y);
	return OutScreen.X > -50.f && OutScreen.Y > -50.f && OutScreen.X < Canvas->SizeX + 50.f && OutScreen.Y < Canvas->SizeY + 50.f;
}

// ---- Frame --------------------------------------------------------------------------------------

void ATSHUD::DrawHUD()
{
	Super::DrawHUD();

	const ATSPlayerCharacter* Player = PlayerOwner ? Cast<ATSPlayerCharacter>(PlayerOwner->GetPawn()) : nullptr;
	if (!Player || !Canvas)
	{
		return;
	}

	UIScale = FMath::Max(0.75f, Canvas->SizeY / 1080.f);

	if (Player->IsInsightOverlayEnabled())
	{
		DrawBeliefs(*Player);
		DrawInsight(*Player);
		DrawSquadPanel();
	}

	DrawDamageFeedback(*Player);

	if (!Player->IsDead())
	{
		DrawCrosshair(*Player);
	}

	DrawVitals(*Player);
	DrawHelp(*Player);
}

void ATSHUD::DrawCrosshair(const ATSPlayerCharacter& Player)
{
	const float CX = Canvas->SizeX * 0.5f;
	const float CY = Canvas->SizeY * 0.5f;

	// The gap is the real spread cone projected on screen.
	const float FOV = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetFOVAngle() : 90.f;
	const float HalfSpread = FMath::DegreesToRadians(Player.GetCurrentSpread() * 0.5f);
	const float Gap = FMath::Max(4.f, FMath::Tan(HalfSpread) / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f)) * CX);
	const float Length = 9.f * UIScale;

	DrawLine(CX - Gap - Length, CY, CX - Gap, CY, TSHUDStyle::Crosshair, 2.f);
	DrawLine(CX + Gap, CY, CX + Gap + Length, CY, TSHUDStyle::Crosshair, 2.f);
	DrawLine(CX, CY - Gap - Length, CX, CY - Gap, TSHUDStyle::Crosshair, 2.f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Length, TSHUDStyle::Crosshair, 2.f);

	const float SinceHit = GetWorld()->GetTimeSeconds() - Player.GetLastHitMarkerTime();
	if (SinceHit < TSHUDStyle::HitMarkerTime)
	{
		const FLinearColor Color = Player.WasLastHitKill() ? TSHUDStyle::KillMarker : TSHUDStyle::HitMarker;
		const float S = 10.f * UIScale;
		const float I = 4.f * UIScale;
		DrawLine(CX - S, CY - S, CX - I, CY - I, Color, 2.5f);
		DrawLine(CX + S, CY - S, CX + I, CY - I, Color, 2.5f);
		DrawLine(CX - S, CY + S, CX - I, CY + I, Color, 2.5f);
		DrawLine(CX + S, CY + S, CX + I, CY + I, Color, 2.5f);
	}
}

void ATSHUD::DrawVitals(const ATSPlayerCharacter& Player)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Margin = 32.f * UIScale;
	const float W = 280.f * UIScale;
	const float H = 54.f * UIScale;
	const float Y = Canvas->SizeY - Margin - H;

	// Health
	const float HealthFraction = Player.GetHealth() / Player.GetMaxHealth();
	DrawPanel(Margin, Y, W, H, TSHUDStyle::Panel);
	DrawLabel(TEXT("HEALTH"), Margin + 12.f * UIScale, Y + 6.f * UIScale, TSHUDStyle::Dim, Font, 0.9f * UIScale);
	DrawBar(Margin + 12.f * UIScale, Y + 32.f * UIScale, W - 24.f * UIScale, 10.f * UIScale, HealthFraction,
		HealthFraction < 0.35f ? TSHUDStyle::HealthLow : TSHUDStyle::Health);

	// Ammo
	if (const ATSWeapon* Weapon = Player.GetWeapon())
	{
		const float AX = Canvas->SizeX - Margin - W;
		DrawPanel(AX, Y, W, H, TSHUDStyle::Panel);
		const FString Ammo = Weapon->IsReloading() ? FString(TEXT("RELOADING")) : FString::Printf(TEXT("%d / %d"), Weapon->GetAmmo(), Weapon->GetMagazineSize());
		DrawLabel(TEXT("RIFLE"), AX + 12.f * UIScale, Y + 6.f * UIScale, TSHUDStyle::Dim, Font, 0.9f * UIScale);
		DrawLabel(Ammo, AX + W - 12.f * UIScale - MeasureText(Ammo, GEngine->GetLargeFont(), UIScale).X, Y + 12.f * UIScale,
			Weapon->GetAmmo() <= 5 ? TSHUDStyle::HealthLow : TSHUDStyle::Text, GEngine->GetLargeFont(), UIScale);
	}

	if (Player.IsDead())
	{
		DrawLabel(TEXT("YOU WERE OUTPLAYED"), Canvas->SizeX * 0.5f, Canvas->SizeY * 0.42f, TSHUDStyle::HealthLow, GEngine->GetLargeFont(), 1.6f * UIScale, true);
		DrawLabel(TEXT("Restarting..."), Canvas->SizeX * 0.5f, Canvas->SizeY * 0.42f + 44.f * UIScale, TSHUDStyle::Dim, Font, UIScale, true);
	}
}

void ATSHUD::DrawDamageFeedback(const ATSPlayerCharacter& Player)
{
	const float SinceDamage = GetWorld()->GetTimeSeconds() - Player.GetLastDamageTime();
	const float HealthFraction = Player.GetHealth() / Player.GetMaxHealth();

	// Low-health vignette.
	const float Vignette = FMath::Clamp((0.5f - HealthFraction) * 1.2f, 0.f, 0.45f);
	if (Vignette > 0.f)
	{
		const float Band = Canvas->SizeY * 0.12f;
		DrawRect(FLinearColor(0.6f, 0.f, 0.f, Vignette), 0.f, 0.f, Canvas->SizeX, Band);
		DrawRect(FLinearColor(0.6f, 0.f, 0.f, Vignette), 0.f, Canvas->SizeY - Band, Canvas->SizeX, Band);
	}

	// Direction of the last hit, as an arc segment around the crosshair.
	if (SinceDamage < 1.2f && PlayerOwner->PlayerCameraManager)
	{
		const FVector ToSource = (Player.GetLastDamageSource() - Player.GetActorLocation()).GetSafeNormal2D();
		const float CameraYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
		const float Angle = FMath::DegreesToRadians(ToSource.Rotation().Yaw - CameraYaw);
		const float Radius = 120.f * UIScale;
		const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
		const FLinearColor Color(1.f, 0.2f, 0.15f, 0.9f * (1.f - SinceDamage / 1.2f));

		for (int32 Segment = -3; Segment < 3; ++Segment)
		{
			const float A0 = Angle + Segment * 0.08f;
			const float A1 = A0 + 0.08f;
			DrawLine(Center.X + FMath::Sin(A0) * Radius, Center.Y - FMath::Cos(A0) * Radius,
				Center.X + FMath::Sin(A1) * Radius, Center.Y - FMath::Cos(A1) * Radius, Color, 4.f);
		}
	}
}

// ---- AI insight ---------------------------------------------------------------------------------

void ATSHUD::DrawInsight(const ATSPlayerCharacter& Player)
{
	const FVector CameraLocation = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : Player.GetActorLocation();

	for (TActorIterator<ASquadAIController> It(GetWorld()); It; ++It)
	{
		const ASquadAIController* Agent = *It;
		const APawn* Pawn = Agent->GetPawn();
		if (!Pawn || !Agent->IsAgentAlive())
		{
			continue;
		}

		const float Distance = FVector::Dist(CameraLocation, Pawn->GetActorLocation());
		FVector2D Anchor;
		if (Distance > TSHUDStyle::InsightRange || !ProjectToScreen(Pawn->GetActorLocation() + FVector(0.f, 0.f, 125.f), Anchor))
		{
			continue;
		}

		DrawAgentCard(*Agent, Anchor, Distance);
		DrawBarks(*Agent, Anchor);
	}
}

void ATSHUD::DrawAgentCard(const ASquadAIController& Agent, const FVector2D& Anchor, float Distance)
{
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = UIScale * FMath::GetMappedRangeValueClamped(FVector2D(500.f, 3500.f), FVector2D(1.1f, 0.8f), Distance);

	const ESquadRole AgentRole = Agent.GetRole();
	const FString RoleText = TSHUDStyle::RoleLabel(AgentRole);
	const FString ActionText = TSHUDStyle::ActionLabel(Agent.GetBrain()->GetActiveActionName());

	const float W = 132.f * Scale;
	const float H = 46.f * Scale;
	const float X = Anchor.X - W * 0.5f;
	const float Y = Anchor.Y - H;

	DrawPanel(X, Y, W, H, TSHUDStyle::Panel);
	DrawRect(TSHUDStyle::RoleColor(AgentRole), X, Y, 3.f * Scale, H);

	DrawLabel(RoleText, X + 8.f * Scale, Y + 3.f * Scale, TSHUDStyle::RoleColor(AgentRole), Font, Scale);
	DrawLabel(ActionText, X + 8.f * Scale, Y + 16.f * Scale, TSHUDStyle::Text, Font, Scale);

	const float BarW = (W - 22.f * Scale) * 0.5f;
	DrawBar(X + 8.f * Scale, Y + 34.f * Scale, BarW, 4.f * Scale, Agent.GetMorale(), TSHUDStyle::Morale);
	DrawBar(X + 14.f * Scale + BarW, Y + 34.f * Scale, BarW, 4.f * Scale, Agent.GetSuppression(), TSHUDStyle::Suppression);
}

void ATSHUD::DrawBarks(const ASquadAIController& Agent, const FVector2D& Anchor)
{
	const TArray<FSquadBarkEvent>& Barks = Agent.GetRecentBarks();
	if (Barks.IsEmpty())
	{
		return;
	}

	const FSquadBarkEvent& Latest = Barks.Last();
	const float Age = GetWorld()->GetTimeSeconds() - Latest.Time;
	const float Alpha = 1.f - FMath::Clamp((Age - (TSHUDStyle::BarkFadeTime - 0.6f)) / 0.6f, 0.f, 1.f);
	if (Alpha <= 0.f)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const FString Line = FString::Printf(TEXT("\"%s\""), *Latest.Line.ToString());
	const FVector2D Size = MeasureText(Line, Font, UIScale);
	const float PadX = 10.f * UIScale;
	const float PadY = 5.f * UIScale;
	const float X = Anchor.X - Size.X * 0.5f - PadX;
	const float Y = Anchor.Y - 52.f * UIScale - Size.Y - PadY * 2.f;

	DrawRect(FLinearColor(0.95f, 0.95f, 0.92f, 0.92f * Alpha), X, Y, Size.X + PadX * 2.f, Size.Y + PadY * 2.f);
	DrawLabel(Line, X + PadX, Y + PadY, FLinearColor(0.05f, 0.05f, 0.05f, Alpha), Font, UIScale);
}

void ATSHUD::DrawBeliefs(const ATSPlayerCharacter& Player)
{
	// Collapse the squad's beliefs about the player into a few markers.
	TArray<TPair<FVector, float>> Beliefs;
	for (TActorIterator<ASquadAIController> It(GetWorld()); It; ++It)
	{
		if (!It->IsAgentAlive())
		{
			continue;
		}

		const FSquadTargetKnowledge* Record = It->GetKnowledge()->Find(&Player);
		// A belief that sits right on the player is accurate, not interesting: only show guesses.
		if (!Record || Record->bVisible || Record->Confidence < 0.05f || FVector::Dist2D(Record->LastKnownLocation, Player.GetActorLocation()) < 150.f)
		{
			continue;
		}

		bool bMerged = false;
		for (TPair<FVector, float>& Existing : Beliefs)
		{
			if (FVector::DistSquared(Existing.Key, Record->LastKnownLocation) < FMath::Square(150.f))
			{
				Existing.Value = FMath::Max(Existing.Value, Record->Confidence);
				bMerged = true;
				break;
			}
		}
		if (!bMerged)
		{
			Beliefs.Emplace(Record->LastKnownLocation, Record->Confidence);
		}
	}

	UFont* Font = GEngine->GetSmallFont();
	for (const TPair<FVector, float>& Belief : Beliefs)
	{
		FVector2D Screen;
		if (!ProjectToScreen(Belief.Key, Screen))
		{
			continue;
		}

		const FLinearColor Color(TSHUDStyle::Belief.R, TSHUDStyle::Belief.G, TSHUDStyle::Belief.B, 0.35f + 0.6f * Belief.Value);
		const float R = 16.f * UIScale;
		for (int32 Segment = 0; Segment < 16; ++Segment)
		{
			if (Segment % 2 == 1)
			{
				continue; // Dashed ring: this is a guess, not a fact.
			}
			const float A0 = Segment * UE_TWO_PI / 16.f;
			const float A1 = (Segment + 1) * UE_TWO_PI / 16.f;
			DrawLine(Screen.X + FMath::Cos(A0) * R, Screen.Y + FMath::Sin(A0) * R, Screen.X + FMath::Cos(A1) * R, Screen.Y + FMath::Sin(A1) * R, Color, 2.f);
		}
		DrawLabel(TEXT("?"), Screen.X, Screen.Y - 8.f * UIScale, Color, GEngine->GetMediumFont(), UIScale, true);
		DrawLabel(FString::Printf(TEXT("AI THINKS YOU'RE HERE  %d%%"), FMath::RoundToInt(Belief.Value * 100.f)),
			Screen.X, Screen.Y + R + 2.f, Color, Font, UIScale, true);
	}
}

void ATSHUD::DrawSquadPanel()
{
	TArray<const ASquadAIController*> Agents;
	for (TActorIterator<ASquadAIController> It(GetWorld()); It; ++It)
	{
		Agents.Add(*It);
	}
	if (Agents.IsEmpty())
	{
		return;
	}

	UFont* Font = GEngine->GetSmallFont();
	const float Margin = 24.f * UIScale;
	const float RowH = 20.f * UIScale;
	const float W = 330.f * UIScale;
	const float H = (Agents.Num() + 1) * RowH + 14.f * UIScale;

	DrawPanel(Margin, Margin, W, H, TSHUDStyle::Panel);
	DrawLabel(TEXT("SQUAD AI  -  live decisions"), Margin + 10.f * UIScale, Margin + 6.f * UIScale, TSHUDStyle::Dim, Font, UIScale);

	float Y = Margin + 6.f * UIScale + RowH;
	int32 Index = 1;
	for (const ASquadAIController* Agent : Agents)
	{
		const bool bAlive = Agent->IsAgentAlive();
		const ESquadRole AgentRole = Agent->GetRole();
		const FLinearColor RoleColor = bAlive ? TSHUDStyle::RoleColor(AgentRole) : TSHUDStyle::Dim;

		DrawLabel(FString::Printf(TEXT("#%d"), Index++), Margin + 10.f * UIScale, Y, TSHUDStyle::Dim, Font, UIScale);
		DrawLabel(bAlive ? TSHUDStyle::RoleLabel(AgentRole) : TEXT("KIA"), Margin + 40.f * UIScale, Y, RoleColor, Font, UIScale);
		if (bAlive)
		{
			DrawLabel(TSHUDStyle::ActionLabel(Agent->GetBrain()->GetActiveActionName()), Margin + 135.f * UIScale, Y, TSHUDStyle::Text, Font, UIScale);
			DrawBar(Margin + 250.f * UIScale, Y + 6.f * UIScale, 66.f * UIScale, 5.f * UIScale, Agent->GetMorale(), TSHUDStyle::Morale);
		}
		Y += RowH;
	}
}

void ATSHUD::DrawHelp(const ATSPlayerCharacter& Player)
{
	const FString Help = FString::Printf(TEXT("F1  AI insight %s     F2  cover debug     RMB  aim     SHIFT  sprint (loud)     R  reload (enemies can hear it)"),
		Player.IsInsightOverlayEnabled() ? TEXT("ON") : TEXT("OFF"));
	const FVector2D Size = MeasureText(Help, GEngine->GetSmallFont(), UIScale);
	const float Y = Canvas->SizeY - 24.f * UIScale;
	DrawRect(TSHUDStyle::Panel, Canvas->SizeX * 0.5f - Size.X * 0.5f - 10.f * UIScale, Y - 3.f * UIScale, Size.X + 20.f * UIScale, Size.Y + 6.f * UIScale);
	DrawLabel(Help, Canvas->SizeX * 0.5f, Y, TSHUDStyle::Text, GEngine->GetSmallFont(), UIScale, true);
}
