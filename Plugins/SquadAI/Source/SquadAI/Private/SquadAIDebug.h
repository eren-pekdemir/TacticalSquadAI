#pragma once

#include "CoreMinimal.h"

class ASquadAIController;
class UWorld;

/** World-space debug drawing, toggled with the squad.Debug / squad.DebugCover console variables. */
namespace SquadAIDebug
{
	void DrawAgent(const ASquadAIController& Agent);
	void DrawCover(UWorld& World);
}
