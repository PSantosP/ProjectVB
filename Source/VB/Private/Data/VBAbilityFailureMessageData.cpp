// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBAbilityFailureMessageData.h"
#include "AbilitySystem/VBGameplayTags.h"

#include "GameplayTagsManager.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	// 조회와 저장 검사가 같은 규칙을 쓴다 - 앞 줄이 쓰지 않은 유효한 태그와, 메시지 또는 Silent 가 있어야 한다.
	bool AcceptFailureMessageRow(const FVBAbilityFailureMessageRow& Row, TSet<FGameplayTag>& Seen)
	{
		bool bAlreadySeen = false;
		if (Row.FailureTag.IsValid())
		{
			Seen.Add(Row.FailureTag, &bAlreadySeen);
		}

		return Row.FailureTag.IsValid() && Row.bSilent == Row.Message.IsEmpty() && !bAlreadySeen;
	}
}

const FVBAbilityFailureMessageRow* UVBAbilityFailureMessageData::FindRow(const FGameplayTagContainer& FailureTags) const
{
	const FVBAbilityFailureMessageRow* Best = nullptr;
	const FVBAbilityFailureMessageRow* Fallback = nullptr;
	TSet<FGameplayTag> Seen;
	for (const FVBAbilityFailureMessageRow& Row : Rows)
	{
		if (!AcceptFailureMessageRow(Row, Seen))
		{
			return nullptr;
		}

		if (Row.FailureTag == VBGameplayTags::Ability_Fail_Unknown)
		{
			Fallback = &Row;
		}

		if (FailureTags.HasTagExact(Row.FailureTag)
			&& (!Best || Row.Priority > Best->Priority
				|| (Row.Priority == Best->Priority && Row.FailureTag.ToString() < Best->FailureTag.ToString())))
		{
			Best = &Row;
		}
	}

	if (!Fallback)
	{
		return nullptr;
	}

	return Best ? Best : Fallback;
}

FGameplayTagContainer UVBAbilityFailureMessageData::RequiredReasons()
{
	const UGameplayTagsManager& Tags = UGameplayTagsManager::Get();
	FGameplayTagContainer Reasons;
	for (const FGameplayTag& Tag :
		Tags.RequestGameplayTagChildren(FGameplayTag(VBGameplayTags::Ability_Fail_Unknown).RequestDirectParent()))
	{
		if (Tags.RequestGameplayTagChildren(Tag).IsEmpty())
		{
			Reasons.AddTag(Tag);
		}
	}

	return Reasons;
}

#if WITH_EDITOR
EDataValidationResult UVBAbilityFailureMessageData::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = Super::IsDataValid(Context) != EDataValidationResult::Invalid;
	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (!AcceptFailureMessageRow(Rows[Index], Seen))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Failure row %d needs a unique valid tag and either a message or Silent (not both)."),
				Index)));
			bValid = false;
		}
	}
	// Ability.Fail 아래 등록된 이유는 Unknown 까지 모두 자기 줄이 있어야 한다.
	//   나중에 더한 이유는 글을 쓰거나 Silent 로 표시할 때까지 여기서 실패한다 - 게임에서 조용히 Unknown 글이 뜨지 않게.
	for (const FGameplayTag& Reason : RequiredReasons())
	{
		if (!Seen.Contains(Reason))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Ability failure messages need a row for %s."), *Reason.ToString())));
			bValid = false;
		}
	}

	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
