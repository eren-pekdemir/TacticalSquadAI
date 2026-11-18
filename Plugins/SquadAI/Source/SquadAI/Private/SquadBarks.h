#pragma once

#include "CoreMinimal.h"
#include "SquadAITypes.h"

/** Default voice lines. Several variants per bark so repeated situations do not sound robotic. */
namespace SquadBarks
{
	inline FText PickLine(ESquadBark Bark)
	{
		static const TMap<ESquadBark, TArray<FString>> Lines = {
			{ ESquadBark::Contact,         { TEXT("Contact!"), TEXT("Enemy spotted!"), TEXT("Got eyes on him!") } },
			{ ESquadBark::Reacquired,      { TEXT("There he is!"), TEXT("I see him again!"), TEXT("He's back!") } },
			{ ESquadBark::LostTarget,      { TEXT("Lost visual!"), TEXT("Where'd he go?"), TEXT("He's gone quiet...") } },
			{ ESquadBark::Searching,       { TEXT("Heard something."), TEXT("Check that noise."), TEXT("Over there, go look.") } },
			{ ESquadBark::Flanking,        { TEXT("I'm flanking!"), TEXT("Moving around his side!"), TEXT("Keep him busy, I'm going around!") } },
			{ ESquadBark::Suppressing,     { TEXT("Suppressing!"), TEXT("Keep his head down!"), TEXT("Covering fire!") } },
			{ ESquadBark::Reloading,       { TEXT("Reloading, cover me!"), TEXT("Changing mag!"), TEXT("I'm dry!") } },
			{ ESquadBark::ManDown,         { TEXT("Man down!"), TEXT("We lost one!"), TEXT("He got Davis!") } },
			{ ESquadBark::FallBack,        { TEXT("Fall back!"), TEXT("I need to pull back!"), TEXT("Too hot, falling back!") } },
			{ ESquadBark::Moving,          { TEXT("Moving up!"), TEXT("Pushing forward!"), TEXT("Closing in!") } },
			{ ESquadBark::Hit,             { TEXT("I'm hit!"), TEXT("Agh!"), TEXT("Taking fire!") } },
			{ ESquadBark::TargetDown,      { TEXT("Target down."), TEXT("He's down."), TEXT("Got him.") } },
			{ ESquadBark::TargetReloading, { TEXT("He's reloading, push!"), TEXT("He's dry! Go, go!"), TEXT("Now, while he reloads!") } },
		};

		const TArray<FString>* Variants = Lines.Find(Bark);
		if (!Variants || Variants->IsEmpty())
		{
			return FText::GetEmpty();
		}
		return FText::FromString((*Variants)[FMath::RandRange(0, Variants->Num() - 1)]);
	}
}
