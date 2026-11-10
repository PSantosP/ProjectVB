// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBStoryState.h"
#include "Settings/VBOptionTypes.h"
#include "VBSaveGameTypes.generated.h"

/**
 * 사용자별 설정(오디오, 자막, 접근성, 입력). "VBSettings" 슬롯에 저장. 해상도, 그래픽, 언어는 엔진 설정이 관리한다.
 * 필드는 뒤에 덧붙이기만 한다(옛 슬롯은 기본값으로 읽힌다). 바꾸는 길은 UVBOptionsSubsystem::Commit 하나다.
 */
USTRUCT(BlueprintType)
struct FVBSettingsSaveData
{
	GENERATED_BODY()

	// 볼륨 (0.0 ~ 1.0). UVBOptionsSubsystem 이 사운드 믹스의 클래스 볼륨으로 건다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float MasterVolume = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float MusicVolume = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float SfxVolume = 1.0f;

	// 카메라/조준 감도 배율(1.0 = 기본). 시점 보정기가 매 입력마다 직접 읽어 변경이 바로 반영된다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float LookSensitivity = 1.0f;

	// Y축 반전 여부. 감도와 같은 경로로 시점 입력 보정기가 읽는다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bInvertYAxis = false;

	// 락온한 적을 처치하면 화면 안 다음 적으로 락온을 넘길지(기본 켬). UVBTargetLockComponent 가 처치 순간에 읽는다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bAutoTargetNextOnKill = true;

	// 미니맵을 보일지(기본 켬). UVBMinimapWidget 이 구성될 때와 설정 바뀜 신호 때 읽는다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bShowMinimap = true;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float VoiceVolume = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bSubtitlesEnabled = true;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	EVBSubtitleSize SubtitleSize = EVBSubtitleSize::Medium;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bShowSpeakerName = true;

	// 게임 UI 배율(1.0 = 기본). UUserInterfaceSettings::ApplicationScale 로 건다 - 게임 뷰포트에만 곱해진다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float UIScale = 1.0f;

	// 화면 흔들림, 섬광 배율(0 = 끔, 1 = 저작값 그대로).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float CameraShakeScale = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float ScreenFlashScale = 1.0f;

	// 누르고 있기 대신 토글로 쓰는 입력(Input.* 태그). 고를 수 있는 입력 목록은 UVBOptionsSettings::ToggleableInputs.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	FGameplayTagContainer ToggleInputs;

	// 피 표현(보통/다른 색/끄기). UVBGC_MeleeImpact 가 타격마다 읽는다. 옛 저장은 보통으로 읽힌다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	EVBBloodDisplay BloodDisplay = EVBBloodDisplay::Normal;

	// 패드 진동(기본 켬). UVBOptionsSubsystem 이 PC 의 진동, 햅틱 문에 건다.
	//   전역 끄기가 없는 장치 속성 진동을 쓰는 쪽은 이 값을 직접 본다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	bool bVibrationEnabled = true;

	// 패드 시점 감도. -1 = 아직 안 고름 = 마우스 감도를 따른다. 푸는 곳은 VBInputCalibration::GamepadLookSensitivity 하나다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float GamepadLookSensitivity = -1.0f;

	// 패드 스틱 데드존(원형, 아래 문턱). 기본값은 IMC_Gamepad 의 옛 값이다. 읽는 쪽은 UVBInputModifier_StickDeadZone.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float GamepadLookDeadZone = 0.15f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Settings")
	float GamepadMoveDeadZone = 0.2f;
};

/**
 * 게임 진행도. 메인 메뉴 "Continue" 의 핵심 데이터.
 * 확장형: 스토리 플래그는 켜진 항목만 보관(TSet)하여 필드 추가 없이 늘려간다.
 */
USTRUCT(BlueprintType)
struct FVBProgressSaveData
{
	GENERATED_BODY()

	// 마지막 저장 시점의 레벨(맵) 패키지 이름. 로드 시 OpenLevel 대상.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	FName LastLevelName;

	// 레벨 내 복원 지점 식별자. 체크포인트 액터가 자기 Id 로 위치를 복원한다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	FName LastCheckpointId;

	// 누적 플레이 시간(초). 슬롯 목록 표시 + 통계용.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	double PlayTimeSeconds = 0.0;

	// 쓰는 곳 없음 - 사실은 아래 Story 가 잇는다. 저장 형식이 append-only 라 지우지 않는다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	TSet<FName> StoryFlags;

	// 저장 시점 플레이어 폰의 월드 Transform. 수동 저장(일시정지)이 채운다.
	// 로드 시 WP 스트리밍 준비 후 이 위치로 복원(UVBPlayerRestoreComponent). 체크포인트(LastCheckpointId)와 별개 축.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	FTransform SavedPlayerTransform;

	// 위 Transform 이 유효한지. false 면 위치 복원을 건너뛰고 PlayerStart 스폰을 유지한다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	bool bHasSavedTransform = false;

	// 상호작용 대상의 켜짐 상태(문 열림 등). 키 = UVBInteractableComponent::GetSaveKey. UVBInteractableSubsystem 이 관리한다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	TMap<FName, bool> InteractableStates;

	// 세계 사실, 카운터. 쓰고 읽는 쪽 = UVBStoryStateComponent(저장 참여자). append-only - 옛 슬롯은 비어 있다(= 아무 사실도 없음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	FVBStoryState Story;

	// 발견한 빠른 이동 지점 ID(Map.TravelPoint.*). 쓰고 읽는 쪽 = UVBTravelPointSubsystem(저장 참여자). append-only - 옛 슬롯은 비어 있다(= 발견 없음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Progress")
	TArray<FGameplayTag> DiscoveredTravelPoints;
};

/**
 * 플레이어 빌드/스탯 스냅샷. 타입을 바꿔도 옛 세이브를 잃지 않게 FGameplayAttribute 대신 Attribute 이름을 키로 쓴다.
 * 채우고 읽는 쪽은 AVBPlayerState::SnapshotBuild / ApplyBuild 이고, 대상 목록은 SavedAttributes 가 소유한다.
 */
USTRUCT(BlueprintType)
struct FVBPlayerBuildSaveData
{
	GENERATED_BODY()

	// Attribute 이름 -> 값 스냅샷 (예: "Health" -> 80.0).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Build")
	TMap<FName, float> AttributeValues;

	// 해금/습득한 어빌리티 식별자 (예: "Ability.Combat.HeavyAttack").
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Build")
	TArray<FName> UnlockedAbilities;

	// 현재 장착 무기 식별자.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Build")
	FName EquippedWeaponId;

	// 가진 무기(EVBWeaponType 이름 문자열). bOwnedWeaponsSaved 가 거짓이면 이전 세이브라 맵 시작값을 둔다(빈 목록 = 무기 없음과 구별).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Build")
	TArray<FName> OwnedWeaponIds;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="VB|Save|Build")
	bool bOwnedWeaponsSaved = false;
};

/** 가진 아이템 한 줄. 자산 경로가 아니라 ItemId 태그로 적는다(UVBItemDefinition 주석 - 자산을 옮겨도 옛 슬롯이 비지 않는다). */
USTRUCT(BlueprintType)
struct FVBSavedItem
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Inventory")
	FGameplayTag ItemId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Inventory")
	int32 Count = 0;
};

/** 인벤토리 구역. 쓰고 읽는 쪽 = UVBInventoryComponent(저장 참여자). 옛 슬롯은 비어 있다(= 빈손). */
USTRUCT(BlueprintType)
struct FVBInventorySaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Inventory")
	TArray<FVBSavedItem> Items;

	// 퀵슬롯 칸마다 ItemId(빈 칸 = 빈 태그). 옛 슬롯은 빈 배열 = 모든 칸이 빈다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Inventory")
	TArray<FGameplayTag> QuickSlots;
};

/** 장비 구역. 쓰고 읽는 쪽 = UVBEquipmentComponent. 옛 슬롯은 빈 배열 = 아무것도 안 입음. */
USTRUCT(BlueprintType)
struct FVBEquipmentSaveData
{
	GENERATED_BODY()

	// 칸마다 입은 아이템의 ItemId(칸 순서 = 아래 Layout, 빈 칸 = 빈 태그).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Equipment")
	TArray<FGameplayTag> Slots;

	// 저장할 때의 칸 배치. 읽을 때 같은 종류의 같은 번째 칸으로 옮긴다. 빈 배열 = 옛 저장(LegacySaveLayout).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Equipment")
	TArray<FGameplayTag> Layout;
};

/** 성장 구역. 쓰고 읽는 쪽 = UVBProgressionComponent. 레벨, 남은 포인트는 저장하지 않고 센다. 옛 슬롯 = 0 / 빈 목록. */
USTRUCT(BlueprintType)
struct FVBGrowthSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Growth")
	int32 TotalXP = 0;

	// 연 스킬 칸(NodeId). 연 순서대로.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Growth")
	TArray<FGameplayTag> Unlocked;
};

/** 귀속 아이템으로 얻은 패시브. 쓰고 읽는 쪽 = UVBPassiveComponent. 옛 슬롯 = 빈 목록. */
USTRUCT(BlueprintType)
struct FVBPassiveSaveData
{
	GENERATED_BODY()

	// 얻은 패시브(Passive.*). 얻은 순서대로.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Passive")
	TArray<FGameplayTag> Acquired;
};

/** 아이템 하나의 강화 단계. */
USTRUCT(BlueprintType)
struct FVBSavedItemLevel
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	FGameplayTag ItemId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	int32 Level = 0;
};

/** 무기 하나의 강화 단계. 무기는 enum 이름 문자열(FVBPlayerBuildSaveData::OwnedWeaponIds 와 같은 식별자). */
USTRUCT(BlueprintType)
struct FVBSavedWeaponLevel
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	FName WeaponId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	int32 Level = 0;
};

/** 강화 구역. 쓰고 읽는 쪽 = UVBEnhancementComponent. 옛 슬롯 = 빈 목록(= 모두 +0). */
USTRUCT(BlueprintType)
struct FVBEnhancementSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	TArray<FVBSavedItemLevel> Items;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Enhancement")
	TArray<FVBSavedWeaponLevel> Weapons;
};

/** 일지의 한 줄. 정의는 QuestId 태그로 되찾는다(아이템과 같은 판단 - 자산을 옮겨도 슬롯이 안 빈다). */
USTRUCT(BlueprintType)
struct FVBSavedQuest
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Quest")
	FGameplayTag QuestId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Quest")
	bool bCompleted = false;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Quest")
	int32 StageIndex = 0;

	// 지금 단계의 목표별 진행.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Quest")
	TArray<int32> Progress;
};

/** 퀘스트 구역. 쓰고 읽는 쪽 = UVBQuestLogComponent(저장 참여자). 옛 슬롯은 비어 있다(= 받은 퀘스트 없음). */
USTRUCT(BlueprintType)
struct FVBQuestSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Quest")
	TArray<FVBSavedQuest> Quests;
};

/** 상점 한 곳에서 아이템 하나를 판 수. 상점, 아이템 모두 태그로 적는다(아이템과 같은 판단 - 자산을 옮겨도 슬롯이 안 빈다). */
USTRUCT(BlueprintType)
struct FVBSavedShopSale
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Shop")
	FGameplayTag ShopId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Shop")
	FGameplayTag ItemId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Shop")
	int32 Sold = 0;
};

/** 상점 재고 구역. 쓰고 읽는 쪽 = UVBShopStateSubsystem(저장 참여자). 옛 슬롯은 비어 있다(= 아무것도 안 팔림). */
USTRUCT(BlueprintType)
struct FVBShopSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Shop")
	TArray<FVBSavedShopSale> Sales;
};

/** 서랍장 하나에 남은 것. 빈 Remaining 도 적는다 - 다 털림은 기록 없는 아직 안 굴림과 다르다. */
USTRUCT(BlueprintType)
struct FVBSavedLootContainer
{
	GENERATED_BODY()

	// 서랍장 상호작용 대상의 저장 키(기본 = 액터 이름 - 켜짐 저장과 같은 키).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Loot")
	FName Key;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Loot")
	TArray<FVBSavedItem> Remaining;
};

/** 서랍장 남은 몫 구역. 쓰고 읽는 쪽 = UVBLootContainerSubsystem(저장 참여자). 옛 슬롯은 비어 있다(= 아무 서랍장도 안 굴림). */
USTRUCT(BlueprintType)
struct FVBLootContainerSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Loot")
	TArray<FVBSavedLootContainer> Containers;
};

/**
 * 월드 물체 자리 구역(밀기 물체 등). 쓰고 읽는 쪽 = UVBInteractableSubsystem. 키 = UVBInteractableComponent::GetObjectKey.
 * 처음 자리에서 움직인 물체만 적는다.
 */
USTRUCT(BlueprintType)
struct FVBWorldObjectSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|World")
	TMap<FName, FTransform> Transforms;
};

/**
 * 튜토리얼 하나의 개인 진행. 복제 구조가 바뀌어도 읽히게 런타임 값과 저장 형식을 나눈다.
 * 단계는 재배열에 견디게 순서 번호가 아니라 StepId 로 적는다.
 */
USTRUCT(BlueprintType)
struct FVBSavedTutorial
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	FGameplayTag TutorialId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	FGameplayTag CurrentStepId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	int32 EventCount = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	bool bCompleted = false;

	// 서버가 다시 보기에 노출을 허용한 단계들(실제로 그려졌는지가 아니다).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	TArray<FGameplayTag> UnlockedSteps;
};

/** 튜토리얼 구역. 쓰고 읽는 쪽 = UVBTutorialComponent(PlayerState, 저장 참여자). 옛 슬롯은 비어 있다(= 아무 안내도 안 봄). */
USTRUCT(BlueprintType)
struct FVBTutorialSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Tutorial")
	TArray<FVBSavedTutorial> Entries;
};

/** 난이도 구역. 쓰고 읽는 쪽 = UVBDifficultyStateComponent(GameState). 옛 슬롯은 빈 태그(= 기본 단계). */
USTRUCT(BlueprintType)
struct FVBDifficultySaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Difficulty")
	FGameplayTag Level;
};

/** 끝낸 결말 한 번의 요약. 다음 회차가 지난 회차를 읽는 유일한 자료다. */
USTRUCT(BlueprintType)
struct FVBClearSummary
{
	GENERATED_BODY()

	// 이 결말을 끝낸 한 번의 이름. 비었으면 끝낸 결말 없음.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGuid ClearId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGameplayTag Ending;

	// 결말 목록이 "다음 회차가 기억할 것" 으로 고른 사실들(누구를 구했나 등). 그 회차 사실 전체가 아니다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGameplayTagContainer Facts;

	bool IsSet() const { return ClearId.IsValid(); }
};

/** 회차 구역. 쓰고 읽는 쪽 = UVBCampaignStateComponent(GameState). 옛 슬롯의 빈 값은 "1회차, 끝낸 결말 없음" 이다. */
USTRUCT(BlueprintType)
struct FVBCampaignSaveData
{
	GENERATED_BODY()

	// 1회차부터 이어지는 한 줄기의 이름. 2회차도 같은 값을 물려받는다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGuid CampaignId;

	// 이 회차의 이름. 회차마다 새로 만든다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGuid RunId;

	// 1 = 1회차. 0 = 아직 이름을 안 붙인 옛 슬롯, 빈 세이브.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	int32 RunIndex = 0;

	// 이 회차를 낳은 앞 회차의 결말. 회차를 만들 때 정해지고 그 회차 동안 바뀌지 않는다. 1회차는 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FVBClearSummary Parent;

	// 이번 회차에 마지막으로 끝낸 결말. Parent 는 앞 회차의 불변 기록이다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FVBClearSummary LastClear;

	// 표시 전용. 게임플레이 조건은 지금 이야기 사실이나 Parent 를 읽는다.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Save|Campaign")
	FGameplayTagContainer SeenEndings;
};

/** 슬롯 목록 표시용 경량 메타데이터(조회 결과, 직렬화 대상 아님). 목록 UI 가 전체 데이터를 들지 않게 한다. */
USTRUCT(BlueprintType)
struct FVBSaveSlotMeta
{
	GENERATED_BODY()

	// 슬롯 번호 (0 기반). 빈 슬롯도 이 값은 채워진다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	int32 SlotIndex = 0;

	// 해당 슬롯에 저장 파일이 있는지. false면 "빈 슬롯"으로 표시.
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	bool bExists = false;

	// 파일은 있는데 읽지 못했다. 빈 슬롯이 아니므로 자동 저장이 이 자리를 고르면 안 된다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	bool bUnreadable = false;

	// 슬롯에 표시할 이름.
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	FString DisplayName;

	// 저장 시각 (UTC).
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	FDateTime Timestamp;

	// 누적 플레이 시간(초).
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	double PlayTimeSeconds = 0.0;

	// 마지막 레벨 이름 (썸네일/배경 결정 등에 활용).
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	FName LastLevelName;

	// 그 슬롯의 지금 챕터 제목. 저장하지 않고 Progress.Story 에서 센다. 챕터가 없으면 빈 글.
	UPROPERTY(BlueprintReadOnly, Category="VB|Save|Slot")
	FText ChapterTitle;

	// 덮어도 잃는 것이 없는 자리인가.
	bool IsFree() const { return !bExists && !bUnreadable; }
};
