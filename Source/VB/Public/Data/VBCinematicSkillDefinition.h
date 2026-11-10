// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBCinematicSkillDefinition.generated.h"

class UAnimMontage;
class UVBAttackConfig;

// 연출 스킬이 붙잡을 대상 조건. 서버에서 한 번 정한다. 몽타주는 여기 없고 스킬 GA 의 AttackConfig 잎이 든다.
UCLASS(BlueprintType, Const)
class VB_API UVBCinematicSkillDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 쓸 수 있는 무기 자세.
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill")
	EVBWeaponType RequiredWeapon = EVBWeaponType::Fighter;

	// 대상 명단: 첫 연출 틱에 시전자로부터 이 수평 거리 안의 적.
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill", meta=(ClampMin="0", Units="cm"))
	float RosterRadiusCm = 800.0f;

	// 시전자 정면 기준 명단 반각. 180 = 전방위.
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill", meta=(ClampMin="0", ClampMax="180", Units="Degrees"))
	float RosterHalfAngleDeg = 180.0f;

	// 가까운 순으로 최대 이만큼.
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill", meta=(ClampMin="0"))
	int32 MaxTargets = 8;

	// 식별 태그(IVBTargetable::GetTargetIdentityTags)가 이 중 하나라도 맞으면 뺀다(예: Enemy.Boss).
	UPROPERTY(EditDefaultsOnly, Category="VB|CinematicSkill", meta=(Categories="Enemy"))
	FGameplayTagContainer ExcludedTargetTags;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

// 연출 틱에 서버가 본 후보 하나. 순수 사실이라 월드 없이 시험한다.
struct FVBCinematicRosterCandidate
{
	float DistanceCm = 0.0f;
	float AngleFromFacingDeg = 0.0f; // 절대값, 0..180
	bool bAlive = true;
	bool bGrounded = true;
	bool bExcludedByTag = false; // 식별 태그가 ExcludedTargetTags 에 걸림
	bool bBodyBusy = false; // 피격, 경직, 다운, 래그돌, 처형 또는 다른 루트모션 몽타주 중
};

namespace VBCinematicSkillRules
{
	// 명단에 드는 후보 인덱스, 가까운 순, 최대 MaxTargets.
	VB_API TArray<int32> SelectRoster(TConstArrayView<FVBCinematicRosterCandidate> Candidates, float RadiusCm,
		float HalfAngleDeg, int32 MaxTargets);

	// 마지막 판정 창 하나를 찾는다. 몽타주 자체에 VBANS_AttackTraceWindow 가 정확히 하나, 세그먼트 시퀀스에는 없어야 한다. 아니면 false.
	VB_API bool FindFinalWindow(const UAnimMontage& Montage, float& OutStart, float& OutEnd);

	// 이 잎으로 스킬을 돌릴 수 있나(몽타주 로드 + 마지막 창 하나). OutWhy 에 실패 이유.
	VB_API bool IsReady(const UVBAttackConfig* Leaf, FString* OutWhy = nullptr);

	// 마지막 국면에 들어섰나: 몽타주 위치가 마지막 창 시작 이상(한 프레임 여유). 하한만 둔다 - 창의 마지막 스윕은 끝을 지나서도 돈다.
	VB_API bool IsFinalPhase(float MontagePosition, float FinalWindowStart);

	// 마지막 타격 관문: 창 안, 명단 안, 아직 소비 안 된 대상만 통과시키고 Consumed 에 더한다.
	VB_API TArray<int32> AllowFinalHits(TConstArrayView<const AActor*> HitActors, bool bInFinalWindow,
		const TSet<TWeakObjectPtr<AActor>>& Roster, TSet<TWeakObjectPtr<AActor>>& Consumed);
}
