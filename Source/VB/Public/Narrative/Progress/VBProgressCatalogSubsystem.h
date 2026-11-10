// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VBProgressCatalogSubsystem.generated.h"

class UVBChapterDefinition;
class UVBRegionDefinition;
struct FVBStoryState;

/**
 * 챕터, 지역 정의 목록. 메뉴(슬롯 메타)와 게임(HUD, 지역 효과)이 같은 목록을 쓴다.
 * Initialize 에서 동기로 다 읽는다 - 작은 자산이고, 비동기면 읽기 전 슬롯 메타, 첫 배너가 빈 채로 굳는다.
 */
UCLASS()
class VB_API UVBProgressCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UVBProgressCatalogSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 사실에서 센 지금 챕터. 없으면 nullptr.
	const UVBChapterDefinition* ResolveChapter(const FVBStoryState& Story) const;
	const UVBRegionDefinition* FindRegion(FGameplayTag RegionId) const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBChapterDefinition>> Chapters;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBRegionDefinition>> Regions;
};
