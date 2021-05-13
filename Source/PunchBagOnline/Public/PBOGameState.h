#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "PBOGameState.generated.h"

UCLASS()
class PUNCHBAGONLINE_API APBOGameState : public AGameStateBase
{
	GENERATED_BODY()
	
public:
	APBOGameState();

	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;

	UFUNCTION(NetMulticast, unreliable)
	void MulticastSendServerTime(float InServerTime);

	UFUNCTION(BlueprintPure)
	float GetSynchronizedTime() { return ServerTime; }
private:
	float SynchronizedTime = 0.0f;
	float ServerTime = 0.0f;
	float LastSentTime = 0.0f;
};
