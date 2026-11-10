// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveGame.generated.h"

/**
 * VB 세이브 데이터 컨테이너. 같은 클래스를 설정 슬롯과 게임 슬롯에 나눠 쓴다.
 * 저장 멤버에는 반드시 UPROPERTY(SaveGame) 를 붙인다(없으면 조용히 빈 저장이 된다). 필드는 덧붙이기만 한다.
 */
UCLASS(BlueprintType)
class VB_API UVBSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 세이브 포맷 버전. 필드 의미가 바뀌면 증가시키고 마이그레이션 분기를 추가한다
	static constexpr int32 CurrentSaveVersion = 1;

	// 이 세이브가 기록된 시점의 포맷 버전. 로드 때 마이그레이션 판단 기준
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Meta")
	int32 SaveVersion = CurrentSaveVersion;

	// 저장 시각(UTC). 슬롯 목록 정렬과 표시용
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Meta")
	FDateTime SaveTimestamp;

	// 슬롯에 표시할 사람이 읽는 이름(예: "챕터 1 - 서막")
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Meta")
	FString SlotDisplayName;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBSettingsSaveData Settings;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBProgressSaveData Progress;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBPlayerBuildSaveData PlayerBuild;

	// 가진 아이템. append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBInventorySaveData Inventory;

	// 퀘스트 일지. append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBQuestSaveData Quests;

	// 상점에서 판 수. append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBShopSaveData Shops;

	// 서랍장에 남은 몫. append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBLootContainerSaveData LootContainers;

	// 입은 장비. append-only - 옛 슬롯은 비어 있다(= 아무것도 안 입음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBEquipmentSaveData Equipment;

	// 성장(누적 경험치, 연 스킬 칸). append-only - 옛 슬롯은 0 과 빈 목록(= 처음 상태)
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBGrowthSaveData Growth;

	// 월드 물체 자리. append-only - 옛 슬롯은 빈 맵(= 배치 자리 그대로).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBWorldObjectSaveData WorldObjects;

	// 튜토리얼 개인 진행. append-only - 옛 슬롯은 빈 목록(= 아무 안내도 안 봄).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBTutorialSaveData Tutorials;

	// 난이도. append-only - 옛 슬롯은 빈 태그(= 기본 단계).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBDifficultySaveData Difficulty;

	// 강화 단계. append-only - 옛 슬롯은 빈 목록(= 모두 +0).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBEnhancementSaveData Enhancement;

	// 회차. append-only - 옛 슬롯은 빈 값(= 1회차, 끝낸 결말 없음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBCampaignSaveData Campaign;

	// 귀속 아이템 패시브. append-only - 옛 슬롯은 빈 목록(= 아무 패시브도 없음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save")
	FVBPassiveSaveData Passives;
};
