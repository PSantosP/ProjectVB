// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Input/VBInputConfig.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "InputAction.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBInputConfig"

namespace
{
	const UInputAction* FindForTag(const TArray<FVBInputAction>& Actions, const FGameplayTag& InputTag)
	{
		if (!InputTag.IsValid())
		{
			return nullptr;
		}

		const FVBInputAction* Found = Actions.FindByPredicate([&InputTag](const FVBInputAction& Action)
		{
			return Action.InputAction && Action.InputTag.MatchesTagExact(InputTag);
		});
		return Found ? Found->InputAction.Get() : nullptr;
	}

	const FVBNativeInputContract* FindContract(const FGameplayTag& InputTag)
	{
		for (const FVBNativeInputContract& Contract : UVBInputConfig::GetNativeInputContract())
		{
			if (Contract.InputTag.MatchesTagExact(InputTag))
			{
				return &Contract;
			}
		}

		return nullptr;
	}

	FText ValueTypeName(EInputActionValueType Type)
	{
		return StaticEnum<EInputActionValueType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
	}
}

const UInputAction* UVBInputConfig::FindNativeInputActionForTag(const FGameplayTag& InputTag) const
{
	return FindForTag(NativeInputActions, InputTag);
}

const UInputAction* UVBInputConfig::FindAbilityInputActionForTag(const FGameplayTag& InputTag) const
{
	return FindForTag(AbilityInputActions, InputTag);
}

TConstArrayView<FVBNativeInputContract> UVBInputConfig::GetNativeInputContract()
{
	// 함수 안 정적 - 네이티브 태그는 모듈 시작 때 등록되므로 첫 호출(에디터 검사, 시험, 영웅 묶기) 때는 이미 유효하다.
	using EType = EInputActionValueType;
	static const FVBNativeInputContract Contract[] =
	{
		{ VBGameplayTags::Input_Move, EType::Axis2D },
		{ VBGameplayTags::Input_Look, EType::Axis2D },
		{ VBGameplayTags::Input_Jump, EType::Boolean },
		{ VBGameplayTags::Input_Attack_Light, EType::Boolean },
		{ VBGameplayTags::Input_Crouch, EType::Boolean },
		{ VBGameplayTags::Input_WalkToggle, EType::Boolean },
		{ VBGameplayTags::Input_LockOn, EType::Boolean },
		{ VBGameplayTags::Input_SwitchTarget, EType::Axis1D },
		{ VBGameplayTags::Input_WeaponSlot_1, EType::Boolean },
		{ VBGameplayTags::Input_WeaponSlot_2, EType::Boolean },
		{ VBGameplayTags::Input_WeaponSlot_3, EType::Boolean },
		{ VBGameplayTags::Input_Interact, EType::Boolean },
		{ VBGameplayTags::Input_Ragdoll, EType::Boolean },
		{ VBGameplayTags::Input_Aim, EType::Boolean },
		{ VBGameplayTags::Input_WeaponSummon, EType::Boolean, true },
	};
	return Contract;
}

TArray<FText> UVBInputConfig::FindProblems() const
{
	TArray<FText> Problems;
	// Input.* 네이티브 태그가 만든 부모 노드. 없으면 모든 줄이 Input 밖으로 실패한다 - 조용히 통과하지 않는다.
	const FGameplayTag InputRoot = FGameplayTag::RequestGameplayTag(TEXT("Input"), false);
	TSet<FGameplayTag> SeenTags;
	TSet<const UInputAction*> SeenActions;
	auto CheckList = [&](const TArray<FVBInputAction>& Rows, bool bNative)
	{
		const FText ListName = bNative ? LOCTEXT("NativeList", "Native") : LOCTEXT("AbilityList", "Ability");
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const FVBInputAction& Row = Rows[Index];
			const FText Tag = FText::FromString(Row.InputTag.ToString());
			if (!Row.InputAction)
			{
				Problems.Add(FText::Format(LOCTEXT("NullAction", "{0} {1}번({2})의 입력 행동이 비었다"), ListName, Index, Tag));
			}
			else
			{
				// 태그가 틀린 줄도 행동 중복은 센다 - 아래의 태그 검사가 줄을 건너뛰기 전에.
				bool bActionSeen = false;
				SeenActions.Add(Row.InputAction.Get(), &bActionSeen);
				if (bActionSeen)
				{
					// 한 행동이 두 처리에 묶이면 한 번 누름이 두 일을 한다.
					Problems.Add(FText::Format(LOCTEXT("DuplicateAction", "{0} {1}번의 입력 행동({2})이 앞에서 이미 나왔다"),
						ListName, Index, FText::FromString(Row.InputAction->GetName())));
				}
			}
			// 맨 Input 태그 자체는 열쇠가 아니다 - MatchesTag 는 자기 자신도 맞다고 하므로 따로 거른다.
			if (!Row.InputTag.IsValid() || !Row.InputTag.MatchesTag(InputRoot) || Row.InputTag.MatchesTagExact(InputRoot))
			{
				Problems.Add(FText::Format(LOCTEXT("BadTag", "{0} {1}번의 태그({2})가 Input.* 이 아니다"), ListName, Index, Tag));
				continue;
			}

			bool bTagSeen = false;
			SeenTags.Add(Row.InputTag, &bTagSeen);
			if (bTagSeen)
			{
				// 같은 태그가 두 줄이면 찾기는 첫 줄만 돌려주고 둘째 줄은 조용히 묶이지 않는다.
				Problems.Add(FText::Format(LOCTEXT("DuplicateTag", "{0} {1}번의 태그({2})가 앞에서 이미 나왔다"), ListName, Index, Tag));
			}

			const FVBNativeInputContract* Contract = FindContract(Row.InputTag);
			if (bNative && !Contract)
			{
				Problems.Add(FText::Format(LOCTEXT("UnknownNative", "Native {0}번의 태그({1})를 영웅이 처리하지 않는다 - 능력 입력이면 Ability 목록에 둔다"), Index,
					Tag));
			}

			if (!bNative && Contract && !Contract->bMayBeAbilityInput)
			{
				Problems.Add(FText::Format(LOCTEXT("NativeInAbility", "Ability {0}번의 태그({1})는 영웅이 직접 처리하는 입력이다 - Native 목록에 둔다"), Index,
					Tag));
			}

			const EInputActionValueType Expected = bNative && Contract ? Contract->ValueType : EInputActionValueType::Boolean;
			if (Row.InputAction && Row.InputAction->ValueType != Expected)
			{
				Problems.Add(FText::Format(LOCTEXT("ValueType", "{0} {1}번({2})의 값 형식이 {3} 이다 - {4} 여야 한다"),
					ListName, Index, Tag, ValueTypeName(Row.InputAction->ValueType), ValueTypeName(Expected)));
			}
		}
	};
	CheckList(NativeInputActions, true);
	CheckList(AbilityInputActions, false);
	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBInputConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
