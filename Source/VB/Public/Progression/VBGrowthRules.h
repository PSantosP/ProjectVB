// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBGrowthRules.generated.h"

class UVBSkillTreeDefinition;

/** 칸을 열 수 있나의 답. */
UENUM(BlueprintType)
enum class EVBSkillUnlockResult : uint8
{
	Unlocked,
	UnknownNode,
	AlreadyUnlocked,
	LevelTooLow,
	MissingPrerequisite,
	NotEnoughPoints,
	// 판정 밖의 거절(서버 아님, 트리 없음). 순수 판정은 이 값을 내지 않는다.
	Unavailable,
};

/**
 * 성장의 순수 판정. 레벨, 포인트는 저장하지 않고 누적 경험치와 연 칸 목록에서 센다.
 * 서버 요청과 불러오기가 같은 CanUnlock 을 탄다.
 */
namespace VBGrowth
{
	// 누적 경험치의 레벨. XPToNextLevel[i] = 레벨 i+1 -> i+2 에 필요한 양. 음수 경험치는 0 으로 본다.
	VB_API int32 LevelForXP(TConstArrayView<int32> XPToNextLevel, int32 TotalXP);

	// 그 레벨이 시작되는 누적 경험치(레벨 1 = 0). 최대 레벨 너머는 최대 레벨의 값.
	VB_API int32 XPAtLevelStart(TConstArrayView<int32> XPToNextLevel, int32 Level);

	VB_API int32 PointsEarned(int32 Level, int32 PointsPerLevel);

	// 연 칸 비용의 합. 트리에 없는 칸은 0 으로 센다.
	VB_API int32 PointsSpent(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked);

	VB_API EVBSkillUnlockResult CanUnlock(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked,
		int32 Level, int32 PointsAvailable, const FGameplayTag& NodeId);

	VB_API const TCHAR* LexToString(EVBSkillUnlockResult Result);

	// HUD 레벨업, 해금 알림 기준. 직전에 본 것과 비교해 새로 생긴 것만 알린다.
	struct FNoticeBaseline
	{
		int32 LastLevel = 0;
		TArray<FGameplayTag> Unlocked;
		int32 SaveReadSerial = 0;
		bool bSeen = false;
	};
	struct FNotices
	{
		int32 LevelUpTo = 0; // 0 = 레벨업 알림 없음. 여러 레벨이 한 번에 오르면 최신 레벨 하나
		TArray<FGameplayTag> NewlyUnlocked; // 연 순서대로
	};
	// 처음 본 갱신과 세이브를 읽은 갱신은 기준만 잡고 알리지 않는다 - 불러온 레벨을 오름으로 읽지 않게.
	VB_API FNotices StepNoticeBaseline(FNoticeBaseline& Baseline, int32 Level, TConstArrayView<FGameplayTag> Unlocked,
		int32 SaveReadSerial);
}

/**
 * 부른 플레이어에게 경험치를 준다. 적 처치 효과와 퀘스트 보상이 쓴다.
 * 주는 양을 주는 쪽 데이터 한 곳에 두려고 처치 사건을 따로 듣지 않는다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="경험치 주기"))
struct VB_API FVBEffect_GiveXP : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Growth", meta=(ClampMin="1"))
	int32 Amount = 0;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
