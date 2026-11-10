// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBCombatRuleTable.h"

#if WITH_EDITOR
#include "Abilities/GameplayAbility.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#endif

#define LOCTEXT_NAMESPACE "VBCombatRuleTable"

void UVBCombatRuleTable::AppendActivationTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlockedBy,
	FGameplayTagContainer& OutRequired) const
{
	for (const FVBStateRule& Rule : StateRules)
	{
		if (Identity.HasAny(Rule.BlocksStart))
		{
			OutBlockedBy.AddTag(Rule.State);
		}
	}

	for (const FVBAbilityRule& Rule : AbilityRules)
	{
		if (Identity.HasTag(Rule.Ability))
		{
			OutRequired.AppendTags(Rule.RequiresState);
		}
	}
}

void UVBCombatRuleTable::AppendBlockAndCancelTags(const FGameplayTagContainer& Identity, FGameplayTagContainer& OutBlock,
	FGameplayTagContainer& OutCancel) const
{
	for (const FVBAbilityRule& Rule : AbilityRules)
	{
		if (Identity.HasTag(Rule.Ability))
		{
			OutBlock.AppendTags(Rule.BlocksWhileRunning);
			OutCancel.AppendTags(Rule.CancelsOnStart);
		}
	}
}

void UVBCombatRuleTable::AppendCancelsOnEntry(const FGameplayTag& EnteredState, FGameplayTagContainer& OutCancel) const
{
	for (const FVBStateRule& Rule : StateRules)
	{
		if (EnteredState.MatchesTag(Rule.State))
		{
			OutCancel.AppendTags(Rule.CancelsOnEntry);
		}
	}
}

#if WITH_EDITOR
namespace
{
	// 네이티브 능력 클래스(추상 포함)가 다는 이름표와 그 부모. BP 능력은 네이티브 생성자의 이름표를 물려받는다.
	FGameplayTagContainer CollectAbilityIdentities()
	{
		FGameplayTagContainer Result;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UGameplayAbility::StaticClass()) && !It->HasAnyClassFlags(CLASS_NewerVersionExists)
				&& It->GetPackage()->GetName() == TEXT("/Script/VB"))
			{
				const FGameplayTagContainer& Tags = It->GetDefaultObject<UGameplayAbility>()->GetAssetTags();
				Result.AppendTags(Tags);
				Result.AppendTags(Tags.GetGameplayTagParents());
			}
		}

		return Result;
	}

	void CheckCells(FDataValidationContext& Context, const FText& Row, const TCHAR* Column, const FGameplayTagContainer& Cells,
		const FGameplayTagContainer* KnownAbilities, EDataValidationResult& Result)
	{
		// 등록되지 않은 태그는 로드할 때 이미 빠진다. 그 손실은 표의 텍스트 사본 비교로 드러난다.
		for (const FGameplayTag& Tag : Cells)
		{
			if (KnownAbilities && !KnownAbilities->HasTagExact(Tag))
			{
				Context.AddError(FText::Format(LOCTEXT("UnknownAbility", "{0} {1}: {2} 를 다는 능력 클래스가 없다 - 이 칸은 아무것도 막지 않는다."),
					Row, FText::FromString(Column), FText::FromName(Tag.GetTagName())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}


	void Refuse(FDataValidationContext& Context, const FText& Message, EDataValidationResult& Result)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	}

	// 주제 태그가 있고 한 번만 나오는지, Why 가 있는지 검사한다.
	void CheckSubjectAndWhy(FDataValidationContext& Context, const FText& Row, const FGameplayTag& Subject, const FString& Why,
		TSet<FGameplayTag>& Subjects, EDataValidationResult& Result)
	{
		if (!Subject.IsValid())
		{
			Refuse(Context, FText::Format(LOCTEXT("SubjectNone", "{0}: 주제 태그가 비었다."), Row), Result);
		}
		else if (Subjects.Contains(Subject))
		{
			Refuse(Context, FText::Format(LOCTEXT("SubjectTwice", "{0} 이 두 번 적혔다 - 한 주제의 규칙은 한 줄에 둔다."), Row), Result);
		}

		Subjects.Add(Subject);
		if (Why.TrimStartAndEnd().IsEmpty())
		{
			Refuse(Context, FText::Format(LOCTEXT("WhyEmpty", "{0}: Why 가 비었다 - 규칙이 왜 있는지 적는다."), Row), Result);
		}
	}
}

void UVBCombatRuleTable::ValidateStateRules(FDataValidationContext& Context, const FGameplayTagContainer& Abilities,
	TSet<FGameplayTag>& Subjects, EDataValidationResult& Result) const
{
	for (const FVBStateRule& Rule : StateRules)
	{
		const FText Row = FText::Format(LOCTEXT("StateRow", "상태 줄 {0}"), FText::FromName(Rule.State.GetTagName()));
		CheckSubjectAndWhy(Context, Row, Rule.State, Rule.Why, Subjects, Result);
		CheckCells(Context, Row, TEXT("BlocksStart"), Rule.BlocksStart, &Abilities, Result);
		CheckCells(Context, Row, TEXT("CancelsOnEntry"), Rule.CancelsOnEntry, &Abilities, Result);
	}
}

void UVBCombatRuleTable::ValidateAbilityRules(FDataValidationContext& Context, const FGameplayTagContainer& Abilities,
	TSet<FGameplayTag>& Subjects, EDataValidationResult& Result) const
{
	for (const FVBAbilityRule& Rule : AbilityRules)
	{
		const FText Row = FText::Format(LOCTEXT("AbilityRow", "능력 줄 {0}"), FText::FromName(Rule.Ability.GetTagName()));
		CheckSubjectAndWhy(Context, Row, Rule.Ability, Rule.Why, Subjects, Result);
		if (Rule.Ability.IsValid() && !Abilities.HasTagExact(Rule.Ability))
		{
			Refuse(Context, FText::Format(LOCTEXT("AbilityUnknown", "{0}: 이 이름표를 다는 능력 클래스가 없다."), Row), Result);
		}

		CheckCells(Context, Row, TEXT("BlocksWhileRunning"), Rule.BlocksWhileRunning, &Abilities, Result);
		CheckCells(Context, Row, TEXT("CancelsOnStart"), Rule.CancelsOnStart, &Abilities, Result);
		CheckCells(Context, Row, TEXT("RequiresState"), Rule.RequiresState, nullptr, Result);
		// 자기 이름을 그대로 적은 칸만 오류다. 부모 태그로 자신까지 덮는 것은 엔진이 시작하는 능력 자신을 취소하지 않으므로 괜찮다.
		if (Rule.BlocksWhileRunning.HasTagExact(Rule.Ability) || Rule.CancelsOnStart.HasTagExact(Rule.Ability))
		{
			Refuse(Context, FText::Format(LOCTEXT("AbilitySelf", "{0}: 자기 자신을 막거나 끊는다."), Row), Result);
		}
		// 필요한 상태가 같은 능력을 막는 상태이기도 하면 그 능력은 시작할 수 없다.
		FGameplayTagContainer BlockedBy;
		FGameplayTagContainer Unused;
		AppendActivationTags(FGameplayTagContainer(Rule.Ability), BlockedBy, Unused);
		if (Rule.RequiresState.HasAny(BlockedBy))
		{
			Refuse(Context, FText::Format(LOCTEXT("RequiredAndBlocked", "{0}: 필요한 상태가 상태 줄에서 이 능력을 막는다 - 영영 켜지지 않는다."), Row), Result);
		}
	}
}

EDataValidationResult UVBCombatRuleTable::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	const FGameplayTagContainer Abilities = CollectAbilityIdentities();
	TSet<FGameplayTag> Subjects;
	ValidateStateRules(Context, Abilities, Subjects, Result);
	ValidateAbilityRules(Context, Abilities, Subjects, Result);
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
