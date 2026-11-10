#include "Enemy/VBEnemySetup.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Data/VBEnemyConfig.h"
#include "VB/VB.h"

void VBEnemySetup::ApplyDevHealthScale(UAbilitySystemComponent* ASC, float Scale)
{
	if (!ASC || !FMath::IsFinite(Scale) || Scale <= 0.0f || FMath::IsNearlyEqual(Scale, 1.0f))
	{
		return;
	}
	// MaxHealth 먼저 - Health 의 PreAttributeChange 가 현재 MaxHealth 로 자른다. 초기화 GE 가 base 값을 쓰므로 base 를 배율한다.
	// 아래 Health 쓰기는 PostAttributeChange 의 비율 보정과 같은 값이지만 그 규칙에 기대지 않으려고 명시한다.
	const float MaxHealth = ASC->GetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute());
	const float Health = ASC->GetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute());
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), MaxHealth * Scale);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), Health * Scale);
	VB_LOG(Log, "%s: 개발용 적 체력 배율 x%.2f - 최대 %.0f -> %.0f", *GetNameSafe(ASC->GetOwner()), Scale, MaxHealth, MaxHealth * Scale);
}

void VBEnemySetup::ApplyAnimTickPolicy(USkeletalMeshComponent* Mesh, const UVBEnemyConfig& Config)
{
	FVBMeshRefresh::SetBasePolicy(Mesh, Config.AnimTickOption, Config.bAnimUpdateRateOptimizations);
}
