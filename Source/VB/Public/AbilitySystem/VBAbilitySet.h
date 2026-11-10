// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpecHandle.h"
#include "Templates/SubclassOf.h"
#include "VBAbilitySet.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FVBAbilitySetAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|GAS")
	TSubclassOf<UGameplayAbility> Ability;

	UPROPERTY(EditAnywhere, Category="VB|GAS", meta=(ClampMin="1"))
	int32 Level = 1;
};

USTRUCT(BlueprintType)
struct FVBAbilitySetEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|GAS")
	TSubclassOf<UGameplayEffect> Effect;

	UPROPERTY(EditAnywhere, Category="VB|GAS", meta=(ClampMin="1"))
	float Level = 1.0f;
};

/**
 * 세트가 준 것의 영수증. 준 쪽이 들고 있다가 거둘 때 돌려준다. 준 ASC 를 약참조로 기억한다.
 * 순간 효과는 거둘 것이 없어 성공 수만 센다.
 */
USTRUCT()
struct VB_API FVBAbilitySetGrantedHandles
{
	GENERATED_BODY()

	void TakeFromAbilitySystem();

	bool IsEmpty() const { return AbilitySpecHandles.IsEmpty() && EffectHandles.IsEmpty() && InstantEffectsApplied == 0; }

private:
	friend class UVBAbilitySet;

	UPROPERTY()
	TWeakObjectPtr<UAbilitySystemComponent> GrantedTo;

	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> EffectHandles;

	int32 InstantEffectsApplied = 0;
};

/**
 * 능력, 효과 묶음 - 능력과 효과를 주는 유일한 길. 서버에서 능력 먼저, 효과 뒤에 배열 순서대로 준다.
 * 속성 세트는 넣지 않는다(소유 액터가 하위 객체로 만든다). 칸이 EditAnywhere 인 이유: 파이썬 저작 경로.
 */
UCLASS(BlueprintType)
class VB_API UVBAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 서버. 준 것을 OutHandles 에 적는다(빈 영수증이어야 한다). SourceObject 는 능력 스펙에만 싣는다.
	void GiveToAbilitySystem(UAbilitySystemComponent* ASC, FVBAbilitySetGrantedHandles& OutHandles, UObject* SourceObject) const;

	const TArray<FVBAbilitySetAbility>& GetGrantedAbilities() const { return GrantedAbilities; }
	const TArray<FVBAbilitySetEffect>& GetGrantedEffects() const { return GrantedEffects; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 검증 판정부(자산 없이 시험). 문제 하나에 한 줄.
	static TArray<FText> FindProblems(const TArray<FVBAbilitySetAbility>& Abilities, const TArray<FVBAbilitySetEffect>& Effects);

private:
	UPROPERTY(EditAnywhere, Category="VB|GAS", meta=(TitleProperty="Ability"))
	TArray<FVBAbilitySetAbility> GrantedAbilities;

	UPROPERTY(EditAnywhere, Category="VB|GAS", meta=(TitleProperty="Effect"))
	TArray<FVBAbilitySetEffect> GrantedEffects;
};
