// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBContextEffectsLibrary.h"

#include "Camera/CameraShakeBase.h"

bool FVBContextEffectRow::HasAnyOutput() const
{
	return Sounds.Num() > 0 || Niagara || DecalMaterial || CameraShake;
}

const FVBContextEffectRow* UVBContextEffectsLibrary::FindRow(const FGameplayTag& EffectTag, const FGameplayTagContainer& Contexts) const
{
	// 맞는 행 중 상황 태그가 가장 많은 행 하나, 같으면 먼저 적힌 행. 전용 행이 없으면 기본 행으로 떨어진다(Lyra 와 다름).
	const FVBContextEffectRow* Best = nullptr;
	int32 BestSpecificity = -1;
	for (const FVBContextEffectRow& Row : Rows)
	{
		if (!Row.EffectTag.MatchesTagExact(EffectTag) || !Contexts.HasAllExact(Row.Context))
		{
			continue;
		}

		const int32 Specificity = Row.Context.Num();
		if (Specificity > BestSpecificity)
		{
			Best = &Row;
			BestSpecificity = Specificity;
		}
	}

	return Best;
}

#if WITH_EDITOR
#include "Misc/DataValidation.h" // FDataValidationContext::AddError

#define LOCTEXT_NAMESPACE "VBContextEffectsLibrary"

EDataValidationResult UVBContextEffectsLibrary::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FVBContextEffectRow& Row = Rows[Index];
		if (!Row.EffectTag.IsValid())
		{
			Context.AddError(FText::Format(LOCTEXT("NoTag", "{0}번 행에 EffectTag 가 없다 - 어떤 노티파이도 이 행을 부를 수 없다."), Index));
			Result = EDataValidationResult::Invalid;
		}

		if (!Row.HasAnyOutput())
		{
			// 빈 행은 경고 없이 아무것도 안 튼다.
			Context.AddError(FText::Format(LOCTEXT("NoOutput", "{0}번 행({1})에 소리·파티클·데칼·흔들림이 하나도 없다."),
				Index, FText::FromName(Row.EffectTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}

		for (int32 Other = Index + 1; Other < Rows.Num(); ++Other)
		{
			if (Rows[Other].EffectTag == Row.EffectTag && Rows[Other].Context == Row.Context)
			{
				// 같은 (태그, 상황) 두 줄이면 뒤 줄은 절대 안 불린다(FindRow 는 같은 구체성에서 앞 줄).
				Context.AddError(FText::Format(LOCTEXT("Duplicate", "{0}번과 {1}번 행이 같은 태그·상황이다 - 뒤 행은 불리지 않는다."), Index, Other));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
