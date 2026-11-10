// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Ending/VBEndingCatalog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Narrative/Story/VBRuleTypes.h"
#define LOCTEXT_NAMESPACE "VBEndingCatalog"

EDataValidationResult UVBEndingCatalog::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};
	if (Endings.IsEmpty())
	{
		Fail(LOCTEXT("Empty", "결말이 하나도 없습니다. 마지막 선택이 늘 비어 결말로 갈 수 없습니다."));
	}

	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < Endings.Num(); ++Index)
	{
		const FVBEndingDefinition& Row = Endings[Index];
		// 빈 이름표는 저장(앞 회차 결말)이 결말을 못 가리키고, 같은 이름표 둘은 둘 중 아무거나 가리킨다.
		if (!Row.Ending.IsValid())
		{
			Fail(FText::Format(LOCTEXT("NoTag", "{0} 번 결말의 Ending 이 비었습니다."), Index));
		}
		else if (Seen.Contains(Row.Ending))
		{
			Fail(FText::Format(LOCTEXT("DupTag", "{0} 번 결말의 Ending {1} 이 앞의 결말과 같습니다."), Index, FText::FromString(Row.Ending.ToString())));
		}
		else
		{
			Seen.Add(Row.Ending);
		}

		if (Row.ChoiceText.IsEmpty() || Row.ConsequenceText.IsEmpty())
		{
			Fail(FText::Format(LOCTEXT("NoText", "결말 {0} 의 선택지 말이나 결과 설명이 비었습니다. 플레이어는 고르기 전에 결과를 들어야 합니다."),
				FText::FromString(Row.Ending.ToString())));
		}

		if (!VBRules::ValidateStructArray(Row.EligibleWhen, FVBCondition::StaticStruct(),
			FText::Format(LOCTEXT("EligibleWhere", "결말 {0} 의 고를 수 있는 조건"), FText::FromString(Row.Ending.ToString())), Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
