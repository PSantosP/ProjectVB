// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBAttackConfig.h"
#include "Animation/VBMontageTransition.h"

#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBComboSelectionPolicy.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBAttackConfig::UVBAttackConfig()
{
	// 답의 기본은 "막을 수 있다". 네이티브 태그는 헤더 멤버 초기화로 못 넣어 생성자에서 준다(GA 의 InputTag 와 같은 관례).
	// 비워 두면 예고 큐가 답 행을 못 찾아 경고를 찍는다 - 기본값이 그 경고를 없는 자산에서까지 막는다.
	AttackAnswer = VBGameplayTags::Attack_Answer_Parryable;
}

namespace
{
	// AttackMontage(0번) + null 아닌 변형들을 평탄한 목록으로 만든다.
	// null 원소를 빼는 이유: 에디터에서 배열 슬롯만 추가하고 비워 두면
	//   그 순간 공격이 무음으로 사라진다. 슬롯 실수를 동작 결함으로 만들지 않는다.
	void CollectVariants(const UVBAttackConfig& Config, TArray<FVBAttackMontageVariant>& Out)
	{
		if (Config.AttackMontage)
		{
			FVBAttackMontageVariant Base;
			Base.Montage = Config.AttackMontage;
			Base.TargetReach = Config.AttackMontageTargetReach;
			Out.Add(Base);
		}

		for (const FVBAttackMontageVariant& V : Config.MontageVariants)
		{
			if (V.Montage)
			{
				Out.Add(V);
			}
		}
	}
}

int32 UVBAttackConfig::GetMontageVariantCount() const
{
	TArray<FVBAttackMontageVariant> All;
	CollectVariants(*this, All);
	return All.Num();
}

TArray<FVBComboCandidate> UVBAttackConfig::GetComboCandidates() const
{
	TArray<FVBAttackMontageVariant> All;
	CollectVariants(*this, All);
	TArray<FVBComboCandidate> Candidates;
	Candidates.Reserve(All.Num());
	for (const FVBAttackMontageVariant& Variant : All)
	{
		Candidates.Add({Variant.Role, Variant.TargetReach});
	}

	return Candidates;
}

FVBAttackMontageVariant UVBAttackConfig::ResolveMontageVariant(int32 VariantIndex) const
{
	TArray<FVBAttackMontageVariant> All;
	CollectVariants(*this, All);
	if (All.Num() == 0)
	{
		return FVBAttackMontageVariant(); // Montage=null -> 호출부가 즉시-Trace 폴백
	}

	// 음수 커서도 안전하게 감싼다. 호출부가 범위를 지킬 의무를 없앤다.
	const int32 Index = ((VariantIndex % All.Num()) + All.Num()) % All.Num();
	return All[Index];
}

float UVBAttackConfig::ResolveComboApproachOffset(const TArray<float>& VariantOffsets, int32 SwingIndex, float FallbackOffset,
	float BodyScale) const
{
	const auto IsAuthored = [SwingIndex](const TArray<float>& Offsets)
	{
		return Offsets.IsValidIndex(SwingIndex) && FMath::IsFinite(Offsets[SwingIndex]) && Offsets[SwingIndex] >= 0.0f;
	};
	if (IsAuthored(VariantOffsets))
	{
		return VariantOffsets[SwingIndex] * BodyScale;
	}

	return IsAuthored(ComboApproachOffsets) ? ComboApproachOffsets[SwingIndex] * BodyScale : FallbackOffset;
}

float UVBAttackConfig::GetVariantApproachOffset(int32 VariantIndex, float BodyScale) const
{
	// 0번(AttackMontage)은 CollectVariants 가 기본 구조체로 만들므로 override 가 -1 이라 자산 값으로 떨어진다.
	const FVBAttackMontageVariant Variant = ResolveMontageVariant(VariantIndex);
	const float Base = Variant.WarpApproachOffsetOverride >= 0.0f ? Variant.WarpApproachOffsetOverride : WarpApproachOffset;
	return FMath::Clamp(Base * BodyScale, 0.0f, FMath::Max(0.0f, GetScaledAttackRange(BodyScale)));
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "VBAttackConfig"

EDataValidationResult UVBAttackConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!VBMontageTransition::IsValidSectionBlend(ComboSectionBlendSeconds))
	{
		Context.AddError(LOCTEXT("ComboBlend", "ComboSectionBlendSeconds는 유한한 0~0.3초 값이어야 합니다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!FMath::IsFinite(KnockbackZ) || !FMath::IsNearlyZero(KnockbackZ))
	{
		Context.AddError(LOCTEXT("LegacyPop", "KnockbackZ는 폐기된 상방 밀치기 값입니다. 0으로 설정하고 띄우기는 LaunchHeight를 사용하세요."));
		Result = EDataValidationResult::Invalid;
	}

	if (!ComboSelection)
	{
		return Result;
	}
	// 한 줄짜리 띠는 돌 수 없다. Manual 줄 옆에 있으면 대개 저작한 동작을 띠에 넣지 않은 것이다.
	for (const EVBComboRole Band : UVBComboSelectionPolicy::FindLoneBandsBesideManual(GetComboCandidates()))
	{
		Context.AddError(FText::Format(LOCTEXT("VBAttack_LoneBand",
			"{0} 띠의 시작 콤보가 하나뿐인데 Manual 줄이 남아 있습니다 - 같은 거리에서 시작 콤보가 돌지 않습니다. "
			"띠 배치 결정이 이 자산에 아직 안 들어갔을 수 있습니다(MontageVariants 의 Role)."),
			UEnum::GetDisplayValueAsText(Band)));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
