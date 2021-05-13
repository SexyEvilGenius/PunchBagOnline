#include "PBOGameMode.h"
#include "PBOGameState.h"

APBOGameMode::APBOGameMode() 
	: Super()
{
	GameStateClass = APBOGameState::StaticClass();
}