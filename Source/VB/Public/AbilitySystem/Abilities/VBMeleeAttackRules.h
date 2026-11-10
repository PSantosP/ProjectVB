// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

class UAnimMontage;
class URootMotionModifier_Warp;
class UVBAttackConfig;
enum class EVBAttackSide : uint8;

// 접근 멈춤 거리 계산의 입력. GA 가 모은 값이고 여기서는 다시 배율 보정하지 않는다.
struct FVBApproachOffsetInputs
{
	// 변형, 콤보 보정과 캐릭터 배율이 이미 곱해진 기본 멈춤 거리(cm).
	float BaseOffset = 0.0f;
	// 캐릭터 배율이 이미 곱해진 사거리(UVBAttackConfig::GetScaledAttackRange).
	float ScaledRange = 0.0f;
	// 배율을 곱하지 않는 먼 시작 멈춤(UVBAttackConfig::FarStartApproachOffset). 0 이면 먼 시작 없음.
	float FarStartApproachOffset = 0.0f;
	// 배율을 곱하지 않는 여유(UVBAttackConfig::MinWarpStandoff).
	float MinWarpStandoff = 0.0f;
	// 공격자-대상 2D 거리(cm).
	float DistToTarget2D = 0.0f;
	// 두 캡슐 반경 합(실제 배율이 걸린 반경). 0 이하면 하한 없음.
	float MinSeparation = 0.0f;
};

// 적 변형 고르기의 재료. 인덱스는 UVBAttackConfig::ResolveMontageVariant 와 같은 규칙이다.
struct FVBEnemyVariantFit
{
	TArray<bool> Allowed;
	TArray<float> Lunges;
	TArray<float> Needed;
	float Ratio = 1.0f;

	// 대역 안 = 전진/허용비 <= 필요 <= 전진 x 허용비. 위쪽 경계가 없으면 제자리 클립이 먼 거리에서 뽑힌다
	bool InBand(int32 Index) const
	{
		return Lunges[Index] / Ratio <= Needed[Index] && Needed[Index] <= Lunges[Index] * Ratio;
	}

	// 대역 밖일 때 덜 틀린 정도 = |ln(전진/필요)|. 누름과 늘임을 같은 자로 잰다. +1 은 0 나눗셈 방지
	float Distortion(int32 Index) const
	{
		return FMath::Abs(FMath::Loge((Lunges[Index] + 1.0f) / (Needed[Index] + 1.0f)));
	}
};

// 토큰 때 계획한 변형을 지금도 쓸 수 있는지의 판정.
enum class EVBPlannedVariant : uint8
{
	Unusable, // 계획이 없거나 범위 밖이거나 좌우 필터에 걸렸다
	InBand,
	OutOfBand, // 후보지만 예고 중 거리가 바뀌어 대역 밖이다
};

// 근접 공격의 상태 없는 계산. 플레이어, 적 GA, 전투 컴포넌트, 시험이 같은 식을 부른다.
namespace VBMeleeAttackRules
{
	// 접근 워프의 멈춤 거리: 사거리 상한 -> 먼 시작 -> 거리-여유 상한 -> 캡슐 하한(마지막이라 가장 우선).
	VB_API float ResolveApproachOffset(const FVBApproachOffsetInputs& In);

	// 몽타주 처음부터 접근 워프 창이 닫힐 때까지 루트가 가는 2D 거리(cm, 월드). 창이 없으면 클립 끝까지. 메시 공간이라 MeshScale 을 곱한다
	VB_API float MeasureMontageLungeCm(const UAnimMontage* Montage, float MeshScale);

	// 몽타주의 접근 워프 창(AttackTarget 노티)과 닫히는 시각. 없으면 null. 반환값은 노티가 소유한 modifier 를 참조한다
	VB_API const URootMotionModifier_Warp* FindApproachWarpModifier(const UAnimMontage* Montage, float* OutWindowEndTime = nullptr);

	// 콤보 타 번호(0 = 첫 타)의 섹션 이름. 설정의 ComboSectionNames, 없으면 "Swing%d"
	VB_API FName ComboSectionName(const UVBAttackConfig* Config, int32 ComboSectionIndex);
	// 그 몽타주에 그 타의 섹션이 있는가. 연타 전환과 회피 재개가 같이 쓴다
	VB_API bool HasComboSection(const UAnimMontage* Montage, const UVBAttackConfig* Config, int32 ComboSectionIndex);

	// 이번 프레임 SkewWarp Facing 이 볼 점. Goal 이 MinLead 이상 앞서면 Goal, 아니면 대상 쪽 MinLead 점(bOutHold=true)
	VB_API FVector ResolveApproachSyncPoint(const FVector& OwnerLocation, const FVector& OwnerForward, const FVector& TargetLocation,
		const FVector& Goal, float MinLead, bool& bOutHold);

	// 이번 프레임의 목표와 겨냥점. 따라가면 지금 대상 기준, 고정이면 세울 때의 점 그대로
	VB_API void ResolveApproachGoal(bool bFollows, const FVector& OwnerLocation, const FVector& LiveTargetLocation,
		float StopDistance,
		const FVector& FixedGoal, const FVector& FixedAim, FVector& OutGoal, FVector& OutAim);

	// 공격 워프를 이 대상으로 이어 가도 되나(살아 있고 타겟 자격이 있다)
	VB_API bool CanKeepAttackWarp(const AActor* SwingTarget);

	// 공격자 로컬 Y 로 표적이 어느 쪽인지 답한다(+Y = 오른쪽)
	VB_API EVBAttackSide ResolveTargetSide(float TargetLocalY);

	// Side 가 있는 변형이 있으면 표적 쪽 변형만 후보다. 맞는 쪽이 없으면 전부 허용하고 bOutNoneOnSide 를 세운다(안 휘두르는 것보다 낫다)
	VB_API TArray<bool> FilterVariantsBySide(TConstArrayView<EVBAttackSide> Sides, EVBAttackSide TargetSide, bool& bOutNoneOnSide);

	VB_API EVBPlannedVariant JudgePlannedVariant(const FVBEnemyVariantFit& Fit, int32 Planned);

	// 대역 안 후보 중 전진이 가장 긴 것, 없으면 덜 틀린 것. 후보가 없으면 INDEX_NONE
	VB_API int32 PickBestEnemyVariant(const FVBEnemyVariantFit& Fit, bool& bOutInBand);
}
