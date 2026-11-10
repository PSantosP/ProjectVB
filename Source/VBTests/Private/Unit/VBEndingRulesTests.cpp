#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/DataValidation.h"
#include "Narrative/Ending/VBEndingCatalog.h"
#include "Narrative/Ending/VBEndingRules.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 결말 무리 판정의 단위 시험.
namespace
{
	FGameplayTag EndingTag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false); }

	FInstancedStruct FactCondition(FGameplayTag Fact, bool bNegate)
	{
		FVBCondition_Fact Condition;
		Condition.Fact = Fact;
		Condition.bNegate = bNegate;
		return FInstancedStruct::Make(Condition);
	}

	FVBEndingDefinition& AddEnding(UVBEndingCatalog& Catalog, const TCHAR* Name)
	{
		FVBEndingDefinition& Row = Catalog.Endings.AddDefaulted_GetRef();
		Row.Ending = EndingTag(Name);
		Row.ChoiceText = FText::FromString(Name);
		Row.ConsequenceText = FText::FromString(TEXT("consequence"));
		return Row;
	}

	FString Names(const TArray<const FVBEndingDefinition*>& Group)
	{
		TArray<FString> Out;
		for (const FVBEndingDefinition* Row : Group)
		{
			Out.Add(Row ? Row->Ending.ToString() : TEXT("null"));
		}

		return FString::Join(Out, TEXT(","));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEndingGroupTest, "VB.Unit.Ending.Group",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEndingGroupTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Shared = EndingTag(TEXT("Ending.SharedRecovery"));
	const FGameplayTag Alone = EndingTag(TEXT("Ending.ReturnAlone"));
	const FGameplayTag Contain = EndingTag(TEXT("Ending.ContainAndStay"));
	const FGameplayTag Rescued = EndingTag(TEXT("Story.Test.ErrandDone"));
	const FGameplayTag Talked = EndingTag(TEXT("Story.Test.Talked"));
	const FGameplayTag Never = EndingTag(TEXT("Story.Test.Never"));
	if (!TestTrue(TEXT("tags registered"), Shared.IsValid() && Alone.IsValid() && Contain.IsValid() && Rescued.IsValid()
		&& Talked.IsValid() && Never.IsValid()))
	{
		return false;
	}

	// 제안 모양(user 확인 대기): 구했으면 {함께 회복, 남음}, 못 구했으면 {혼자 돌아옴, 남음}. 판정은 이 모양을 모른다 - 조건이 정한다.
	const TStrongObjectPtr<UVBEndingCatalog> Catalog(NewObject<UVBEndingCatalog>(GetTransientPackage()));
	AddEnding(*Catalog, TEXT("Ending.SharedRecovery")).EligibleWhen.Add(FactCondition(Rescued, false));
	Catalog->Endings[0].RememberFacts.AddTag(Talked);
	Catalog->Endings[0].RememberFacts.AddTag(Never);
	AddEnding(*Catalog, TEXT("Ending.ReturnAlone")).EligibleWhen.Add(FactCondition(Rescued, true));
	AddEnding(*Catalog, TEXT("Ending.ContainAndStay"));

	// 소유 액터 없는 사실 주인 = 단위 시험의 서버(쓰기 허용).
	const TStrongObjectPtr<UVBStoryStateComponent> Story(NewObject<UVBStoryStateComponent>(GetTransientPackage()));
	FVBRuleContext Context;
	Context.Story = Story.Get();

	TArray<const FVBEndingDefinition*> Group = VBEndingRules::ResolveGroup(Catalog.Get(), Context);
	TestEqual(TEXT("not rescued: return-alone and contain, in catalog order"), Names(Group),
		FString::Printf(TEXT("%s,%s"), *Alone.ToString(), *Contain.ToString()));
	TestFalse(TEXT("not rescued: shared recovery is not offered"), VBEndingRules::IsInGroup(Group, Shared));
	TestTrue(TEXT("not rescued: contain is offered"), VBEndingRules::IsInGroup(Group, Contain));
	TestFalse(TEXT("empty choice is never in a group"), VBEndingRules::IsInGroup(Group, FGameplayTag()));

	Story->SetFact(Rescued, true);
	Group = VBEndingRules::ResolveGroup(Catalog.Get(), Context);
	TestEqual(TEXT("rescued: shared recovery and contain"), Names(Group),
		FString::Printf(TEXT("%s,%s"), *Shared.ToString(), *Contain.ToString()));
	TestFalse(TEXT("rescued: return-alone is not offered"), VBEndingRules::IsInGroup(Group, Alone));

	// 요약: 기억할 사실 중 지금 켜진 것만, 이름은 부를 때마다 새로.
	Story->SetFact(Talked, true);
	const FVBClearSummary First = VBEndingRules::MakeClearSummary(*Group[0], Story->GetState());
	const FVBClearSummary Second = VBEndingRules::MakeClearSummary(*Group[0], Story->GetState());
	TestTrue(TEXT("summary carries the ending"), First.Ending == Shared && First.IsSet());
	TestTrue(TEXT("summary remembers a listed fact that is on"), First.Facts.HasTagExact(Talked));
	TestFalse(TEXT("summary drops a listed fact that is off"), First.Facts.HasTagExact(Never));
	TestFalse(TEXT("summary drops an unlisted fact even if on"), First.Facts.HasTagExact(Rescued));
	TestNotEqual(TEXT("each clear gets its own id"), First.ClearId, Second.ClearId);
	{
		FVBEndingDefinition Untagged = *Group[0];
		Untagged.Ending = FGameplayTag();
		AddExpectedMessagePlain(TEXT("이름표 없는 결말"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("untagged ending gives an empty summary (no clear)"),
			VBEndingRules::MakeClearSummary(Untagged, Story->GetState()).IsSet());
	}

	// 고를 것이 없으면 빈 무리 - 아무거나 고르지 않는다.
	const TStrongObjectPtr<UVBEndingCatalog> Locked(NewObject<UVBEndingCatalog>(GetTransientPackage()));
	AddEnding(*Locked, TEXT("Ending.SharedRecovery")).EligibleWhen.Add(FactCondition(Never, false));
	AddEnding(*Locked, TEXT("Ending.ReturnAlone")).Ending = FGameplayTag(); // 이름표 없는 줄은 조건이 비어도 무리에 안 든다
	AddExpectedMessagePlain(TEXT("고를 수 있는 결말이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("nothing eligible: empty group"), VBEndingRules::ResolveGroup(Locked.Get(), Context).Num(), 0);
	AddExpectedMessagePlain(TEXT("결말 목록이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("no catalog: empty group"), VBEndingRules::ResolveGroup(nullptr, Context).Num(), 0);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEndingCatalogValidationTest, "VB.Unit.Ending.CatalogValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEndingCatalogValidationTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBEndingCatalog> Good(NewObject<UVBEndingCatalog>(GetTransientPackage()));
	AddEnding(*Good, TEXT("Ending.SharedRecovery"));
	AddEnding(*Good, TEXT("Ending.ReturnAlone"));
	{
		FDataValidationContext Context;
		TestNotEqual(TEXT("well-formed catalog is not invalid"), Good->IsDataValid(Context), EDataValidationResult::Invalid);
		TestEqual(TEXT("well-formed catalog: no errors"), static_cast<int32>(Context.GetNumErrors()), 0);
	}

	{
		const TStrongObjectPtr<UVBEndingCatalog> Empty(NewObject<UVBEndingCatalog>(GetTransientPackage()));
		FDataValidationContext Context;
		TestEqual(TEXT("empty catalog is invalid"), Empty->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	{
		// 같은 이름표 + 빈 결과 설명 + 빈 조건 칸 = 오류 셋.
		const TStrongObjectPtr<UVBEndingCatalog> Broken(NewObject<UVBEndingCatalog>(GetTransientPackage()));
		AddEnding(*Broken, TEXT("Ending.SharedRecovery"));
		AddEnding(*Broken, TEXT("Ending.SharedRecovery")).ConsequenceText = FText::GetEmpty();
		Broken->Endings[0].EligibleWhen.AddDefaulted();
		FDataValidationContext Context;
		TestEqual(TEXT("broken catalog is invalid"), Broken->IsDataValid(Context), EDataValidationResult::Invalid);
		TestEqual(TEXT("three errors reported"), static_cast<int32>(Context.GetNumErrors()), 3);
	}

	return true;
}
#endif
#endif
