// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "CharacterCommon/VBDeathSteps.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "GameFramework/Actor.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBHealthComponentTestTypes.generated.h"

// UVBHealthComponent 가 필요로 하는 것만 가진 몸 - 실제 health set 을 단 자기 ASC 와 죽음 세 단계.
//   단계마다 본 것을 Steps 에 적어, 시험이 제품 hook 없이 순서와 단계별 상태를 읽는다.
UCLASS(NotBlueprintable)
class AVBHealthTestBody : public AActor, public IAbilitySystemInterface, public IVBDeathSteps
{
	GENERATED_BODY()

public:
	AVBHealthTestBody()
	{
		AbilitySystem = CreateDefaultSubobject<UVBAbilitySystemComponent>(TEXT("AbilitySystem"));
		HealthSet = CreateDefaultSubobject<UVBHealthAttributeSet>(TEXT("HealthSet"));
		Health = CreateDefaultSubobject<UVBHealthComponent>(TEXT("HealthComponent"));
	}

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

	bool HasDeadTag() const { return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(VBGameplayTags::State_Dead); }

	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) override
	{
		Steps.Add(FString::Printf(TEXT("before dead=%d"), HasDeadTag() ? 1 : 0));
		if (OnBefore)
		{
			OnBefore(*this);
		}
	}

	virtual void AfterDeathTag(const FVBDeathFacts& Facts) override
	{
		Steps.Add(FString::Printf(TEXT("after dead=%d"), HasDeadTag() ? 1 : 0));
		if (OnAfter)
		{
			OnAfter(*this);
		}
	}

	virtual void PresentDeath(const FVBDeathFacts& Facts) override
	{
		bPresented = true;
		Steps.Add(TEXT("present"));
	}

	UPROPERTY()
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystem;
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthSet;
	UPROPERTY()
	TObjectPtr<UVBHealthComponent> Health;

	TArray<FString> Steps;
	bool bPresented = false;
	int32 GateDefeats = 0;
	// 재진입 probe - BeforeDeathTag / AfterDeathTag 안에서 돈다(같은 몸에 쓰는 평판, 처치 효과 흉내)
	TFunction<void(AVBHealthTestBody&)> OnBefore;
	TFunction<void(AVBHealthTestBody&)> OnAfter;
};

// 조우 관문이 적용한 순간 그 몸의 모습을 적어 두는 DefeatedEffects 항목
USTRUCT()
struct FVBHealthTestDefeatObserver : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AVBHealthTestBody> Body;

	virtual void Apply(const FVBRuleContext& Context) const override
	{
		if (!Body)
		{
			return;
		}

		++Body->GateDefeats;
		Body->Steps.Add(FString::Printf(TEXT("gate present=%d dead=%d"), Body->bPresented ? 1 : 0, Body->HasDeadTag() ? 1 : 0));
	}
};
