// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBCombatRuleTable.generated.h"

// 캐릭터가 이 상태인 동안의 규칙. 「캐릭터가 이 상태라서」 생기는 규칙은 여기 쓴다.
USTRUCT(BlueprintType)
struct FVBStateRule
{
	GENERATED_BODY()

	// 상태 태그. 자식 상태에도 적용된다(State.Combat.Attacking 줄은 State.Combat.Attacking.Light 에도 걸린다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="State"))
	FGameplayTag State;

	// 이 상태인 동안 시작할 수 없는 능력. 부모를 적으면 그 아래 전부다(Ability.Attack = 모든 공격, Ability = 전부).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Ability"))
	FGameplayTagContainer BlocksStart;

	// 이 상태가 생기는 순간 서버에서 취소하는, 이미 실행 중인 능력.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Ability"))
	FGameplayTagContainer CancelsOnEntry;

	// 이 규칙이 있는 이유.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(MultiLine=true))
	FString Why;
};

// 이 능력이 실행 중인 동안의 규칙. 「상태와 상관없이 이 능력이라서」 생기는 규칙은 여기 쓴다.
USTRUCT(BlueprintType)
struct FVBAbilityRule
{
	GENERATED_BODY()

	// 능력의 이름표(능력 클래스 생성자의 AssetTags). 자식 이름표를 단 능력에도 적용된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Ability"))
	FGameplayTag Ability;

	// 이 능력이 실행 중인 동안 시작할 수 없는 능력.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Ability"))
	FGameplayTagContainer BlocksWhileRunning;

	// 이 능력이 시작할 때 취소하는, 이미 실행 중인 능력.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Ability"))
	FGameplayTagContainer CancelsOnStart;

	// 이 능력을 시작하려면 캐릭터가 가지고 있어야 하는 상태, 얻은 패시브(Passive.*).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="State,Passive"))
	FGameplayTagContainer RequiresState;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(MultiLine=true))
	FString Why;
};

/**
 * 전투 규칙 표 - 무엇이 무엇을 막고 취소하는지 적는 유일한 곳. Lyra ULyraAbilityTagRelationshipMapping 과 같은 역할이다.
 * 능력 클래스는 자기 이름표와 자기가 붙이는 상태만 정하고, 관계는 기획자가 이 자산에서 고친다.
 * UVBAbilitySystemComponent 가 프로젝트 설정(UVBCombatSettings::CombatRules)에서 읽어 판정한다.
 */
UCLASS(BlueprintType)
class VB_API UVBCombatRuleTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(TitleProperty="State"))
	TArray<FVBStateRule> StateRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(TitleProperty="Ability"))
	TArray<FVBAbilityRule> AbilityRules;

	// 이 이름표의 능력을 막는 상태를 OutBlockedBy 에, 시작에 필요한 상태를 OutRequired 에 더한다.
	void AppendActivationTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlockedBy,
		FGameplayTagContainer& OutRequired) const;

	// 이 이름표의 능력이 실행 중에 막는 능력을 OutBlock 에, 시작할 때 취소하는 능력을 OutCancel 에 더한다.
	void AppendBlockAndCancelTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlock,
		FGameplayTagContainer& OutCancel) const;

	// 상태 EnteredState 가 생길 때 취소할 능력을 OutCancel 에 더한다.
	void AppendCancelsOnEntry(const FGameplayTag& EnteredState, FGameplayTagContainer& OutCancel) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

private:
	// IsDataValid 의 두 단계. Abilities = 능력 클래스가 다는 이름표와 그 부모, Subjects = 이미 나온 주제 태그.
	void ValidateStateRules(class FDataValidationContext& Context, const FGameplayTagContainer& Abilities, TSet<FGameplayTag>& Subjects,
		EDataValidationResult& Result) const;
	void ValidateAbilityRules(class FDataValidationContext& Context, const FGameplayTagContainer& Abilities, TSet<FGameplayTag>& Subjects,
		EDataValidationResult& Result) const;
#endif
};
