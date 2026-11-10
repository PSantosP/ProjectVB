// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Progress/VBProgressCatalogSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Narrative/Progress/VBChapterDefinition.h"
#include "Narrative/Progress/VBChapterProgress.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "VB/VB.h"

namespace
{
	template <typename TDefinition>
	void LoadAll(const FPrimaryAssetType Type, TArray<TObjectPtr<TDefinition>>& Out)
	{
		UAssetManager* Manager = UAssetManager::GetIfInitialized();
		if (!Manager)
		{
			VB_LOG(Warning, "진행 카탈로그: 에셋 매니저가 아직 없다 - %s 정의를 못 읽었다", *Type.ToString());
			return;
		}

		TArray<FPrimaryAssetId> Ids;
		Manager->GetPrimaryAssetIdList(Type, Ids);
		for (const FPrimaryAssetId& Id : Ids)
		{
			if (TDefinition* Definition = Cast<TDefinition>(Manager->GetPrimaryAssetPath(Id).TryLoad()))
			{
				Out.Add(Definition);
			}
			else
			{
				VB_LOG(Warning, "진행 카탈로그: %s 를 읽지 못했다", *Id.ToString());
			}
		}
	}
}

UVBProgressCatalogSubsystem* UVBProgressCatalogSubsystem::Get(const UObject* WorldContextObject)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
	return GameInstance ? GameInstance->GetSubsystem<UVBProgressCatalogSubsystem>() : nullptr;
}

void UVBProgressCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadAll(UVBChapterDefinition::GetChapterAssetType(), Chapters);
	LoadAll(UVBRegionDefinition::GetRegionAssetType(), Regions);
	VB_LOG(Log, "진행 카탈로그: 챕터 %d · 지역 %d", Chapters.Num(), Regions.Num());
}

const UVBChapterDefinition* UVBProgressCatalogSubsystem::ResolveChapter(const FVBStoryState& Story) const
{
	return VBChapterProgress::ResolveCurrent(Chapters, Story);
}

const UVBRegionDefinition* UVBProgressCatalogSubsystem::FindRegion(FGameplayTag RegionId) const
{
	for (const TObjectPtr<UVBRegionDefinition>& Region : Regions)
	{
		if (Region && Region->RegionId == RegionId)
		{
			return Region;
		}
	}

	return nullptr;
}
