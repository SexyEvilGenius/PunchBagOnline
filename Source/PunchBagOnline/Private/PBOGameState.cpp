#include "PBOGameState.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

APBOGameState::APBOGameState() 
	: Super()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void APBOGameState::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
	
	if (!GetWorld()->IsPaused())
	{
		SynchronizedTime += DeltaTime;
		ServerTime += DeltaTime;
		if (SynchronizedTime != ServerTime)
		{
			SynchronizedTime = FMath::Lerp(SynchronizedTime, ServerTime, DeltaTime);
		}
		if (HasAuthority() && (LastSentTime == 0.0f || (SynchronizedTime - LastSentTime) > 10))
		{
			MulticastSendServerTime(ServerTime);
		}
	}
}

void APBOGameState::MulticastSendServerTime_Implementation(float InServerTime)
{
	if (!HasAuthority())
	{
		float ping = 0;
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		if (IsValid(Player))
		{
			if (IsValid(Player->PlayerState))
			{
				ping = Player->PlayerState->GetPing();
			}
		}
		ServerTime = InServerTime + ((ping) * 0.001);
	}
}