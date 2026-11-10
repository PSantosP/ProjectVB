// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VBMenuGameMode.generated.h"

/** 메인 메뉴 레벨 전용 GameMode. 메뉴 전용 PC 만 스폰하고 기본 폰은 만들지 않는다(DefaultPawnClass=nullptr). */
UCLASS()
class VB_API AVBMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVBMenuGameMode();
};
