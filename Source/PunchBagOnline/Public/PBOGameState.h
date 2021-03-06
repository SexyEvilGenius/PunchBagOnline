#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "PBOGameState.generated.h"

UCLASS()
class PUNCHBAGONLINE_API APBOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Returns the simulated TimeSeconds on the server, will be synchronized on client and server */
	/** ping is added */
	virtual float GetServerWorldTimeSeconds() const override;
};
