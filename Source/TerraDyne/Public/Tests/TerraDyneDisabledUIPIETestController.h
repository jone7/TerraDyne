// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneEditController.h"
#include "TerraDyneDisabledUIPIETestController.generated.h"

UCLASS()
class TERRADYNE_API ATerraDyneDisabledUIPIETestController : public ATerraDyneEditController
{
	GENERATED_BODY()

public:
	ATerraDyneDisabledUIPIETestController()
	{
		bEnablePlayModeToolUI = false;
		UIClass = UTerraDyneToolWidget::StaticClass();
	}
};
