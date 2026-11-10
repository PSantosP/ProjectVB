// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBStartingLoadout.generated.h"

/**
 * 이 맵에서 새로 시작한 플레이어가 처음 가진 무기. 맵들이 GameMode 를 공유해 맵에 놓는 액터로 둔다.
 * 놓지 않은 맵은 무기 전부로 시작하고, 이어하기는 저장을 쓴다. 둘 이상이면 첫째를 쓰고 경고한다.
 */
UCLASS(HideCategories=(Input, Replication, Rendering, Collision, HLOD, Physics, Networking, LevelInstance, Cooking, DataLayers,
	WorldPartition))
class VB_API AVBStartingLoadout : public AInfo
{
	GENERATED_BODY()

public:
	// 빈 목록 = 무기 없이 시작한다(프롤로그 - 무기는 주워야 생긴다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapon")
	TArray<EVBWeaponType> StartingWeapons;
};
