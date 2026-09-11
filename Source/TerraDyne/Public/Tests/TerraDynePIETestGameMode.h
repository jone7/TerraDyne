// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SpectatorPawn.h"
#include "Tests/TerraDyneDisabledUIPIETestController.h"
#include "TerraDynePIETestGameMode.generated.h"

UCLASS()
class TERRADYNE_API ATerraDynePIETestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATerraDynePIETestGameMode()
	{
		PlayerControllerClass = ATerraDyneDisabledUIPIETestController::StaticClass();
		DefaultPawnClass = ASpectatorPawn::StaticClass();
		SpectatorClass = ASpectatorPawn::StaticClass();
		HUDClass = AHUD::StaticClass();
		bStartPlayersAsSpectators = true;
	}
};
