#include "PBOGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

float APBOGameState::GetServerWorldTimeSeconds() const
{
	float PlayerPing = 0;
	if (GetNetMode() != NM_DedicatedServer && IsValid(GetWorld()))
	{
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		if (IsValid(Player))
		{
			if (IsValid(Player->PlayerState))
			{
				PlayerPing = GetWorld()->GetFirstPlayerController()->PlayerState->GetPing();
			}
		}
	}
	return Super::GetServerWorldTimeSeconds() + (PlayerPing*2*0.001);
}
