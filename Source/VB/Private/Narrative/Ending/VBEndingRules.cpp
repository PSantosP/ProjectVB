// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Ending/VBEndingRules.h"

#include "Narrative/Ending/VBEndingCatalog.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryState.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VB/VB.h"

TArray<const FVBEndingDefinition*> VBEndingRules::ResolveGroup(const UVBEndingCatalog* Catalog, const FVBRuleContext& Context)
{
	TArray<const FVBEndingDefinition*> Group;
	if (!Catalog)
	{
		VB_LOG(Warning, "결말 판정: 결말 목록이 없다 - 고를 결말이 없다");
		return Group;
	}

	for (const FVBEndingDefinition& Row : Catalog->Endings)
	{
		// 이름표 없는 줄은 골라도 저장이 결말을 못 가리킨다 - 무리에 넣지 않는다(검증 오류이기도 하다).
		if (Row.Ending.IsValid() && VBRules::EvaluateAll(Row.EligibleWhen, Context))
		{
			Group.Add(&Row);
		}
	}

	VB_CLOG(Group.IsEmpty(), Warning, "결말 판정: 목록 %s 에서 고를 수 있는 결말이 없다 - 결말로 가지 않는다(조건을 확인)", *Catalog->GetName());
	return Group;
}

bool VBEndingRules::IsInGroup(const TArray<const FVBEndingDefinition*>& Group, FGameplayTag Ending)
{
	return Ending.IsValid() && Group.ContainsByPredicate([Ending](const FVBEndingDefinition* Row) { return Row && Row->Ending == Ending; });
}

FVBClearSummary VBEndingRules::MakeClearSummary(const FVBEndingDefinition& Ending, const FVBStoryState& Facts)
{
	FVBClearSummary Summary;
	// 이름표 없는 결말로 만든 요약은 끝냈는데 어느 결말인지 모르는 상태가 된다 - 빈 요약(끝낸 결말 없음)을 준다.
	if (!Ending.Ending.IsValid())
	{
		VB_LOG(Warning, "결말 요약: 이름표 없는 결말이다 - 끝낸 결말로 남기지 않는다");
		return Summary;
	}

	Summary.ClearId = FGuid::NewGuid();
	Summary.Ending = Ending.Ending;
	for (const FGameplayTag& Fact : Ending.RememberFacts)
	{
		if (Facts.HasFact(Fact))
		{
			Summary.Facts.AddTagFast(Fact);
		}
	}

	return Summary;
}
