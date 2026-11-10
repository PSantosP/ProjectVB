// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBAbilitySet.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAbilitySet"

void FVBAbilitySetGrantedHandles::TakeFromAbilitySystem()
{
	UAbilitySystemComponent* ASC = GrantedTo.Get();
	if (ASC && !ASC->IsOwnerActorAuthoritative())
	{
		VB_LOG(Warning, "[AbilitySet] 권위가 아닌 쪽에서 거두려 했다(%s) - 거두지 않는다", *GetNameSafe(ASC->GetOwner()));
		return;
	}

	if (ASC)
	{
		for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
		{
			ASC->ClearAbility(Handle);
		}

		for (const FActiveGameplayEffectHandle& Handle : EffectHandles)
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	// ASC 가 이미 사라졌으면 준 것도 함께 사라졌다 - 비우기만 한다.
	GrantedTo.Reset();
	AbilitySpecHandles.Reset();
	EffectHandles.Reset();
	InstantEffectsApplied = 0;
}

void UVBAbilitySet::GiveToAbilitySystem(UAbilitySystemComponent* ASC, FVBAbilitySetGrantedHandles& OutHandles, UObject* SourceObject) const
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		VB_LOG(Warning, "[AbilitySet] %s: ASC 가 없거나(%s) 권위가 아니다 - 주지 않는다", *GetName(), *GetNameSafe(ASC));
		return;
	}

	if (!ensureMsgf(OutHandles.IsEmpty(), TEXT("[AbilitySet] %s: 이미 무언가 든 영수증에 다시 준다 - 한 번 거둘 때 앞의 것이 남는다"), *GetName()))
	{
		// ensure 는 프로세스에 한 번만 울린다 - 두 번째부터도 거절이 보이게.
		VB_LOG(Error, "[AbilitySet] %s: 이미 무언가 든 영수증이라 주지 않는다(%s)", *GetName(), *GetNameSafe(ASC->GetOwner()));
		return;
	}

	OutHandles.GrantedTo = ASC;

	for (const FVBAbilitySetAbility& Entry : GrantedAbilities)
	{
		if (!Entry.Ability)
		{
			// IsDataValid 가 막는 자리다. 저장 검증을 건너뛴 자산이면 소리를 낸다.
			VB_LOG(Error, "[AbilitySet] %s: 빈 능력 항목 - 건너뛴다", *GetName());
			continue;
		}

		FGameplayAbilitySpec Spec(Entry.Ability, Entry.Level, INDEX_NONE, SourceObject);
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
		if (Handle.IsValid())
		{
			OutHandles.AbilitySpecHandles.Add(Handle);
		}

		VB_LOG(Verbose, "[AbilitySet] %s -> %s: 능력 %s", *GetName(), *GetNameSafe(ASC->GetOwner()), *Entry.Ability->GetName());
	}

	for (const FVBAbilitySetEffect& Entry : GrantedEffects)
	{
		if (!Entry.Effect)
		{
			VB_LOG(Error, "[AbilitySet] %s: 빈 효과 항목 - 건너뛴다", *GetName());
			continue;
		}

		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Entry.Effect, Entry.Level, ASC->MakeEffectContext());
		if (!Spec.IsValid())
		{
			VB_LOG(Error, "[AbilitySet] %s: 효과 %s 의 스펙을 만들지 못했다", *GetName(), *Entry.Effect->GetName());
			continue;
		}

		const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		if (Handle.IsValid())
		{
			OutHandles.EffectHandles.Add(Handle);
		}
		else if (Handle.WasSuccessfullyApplied())
		{
			// 순간 효과: 실행은 됐고 거둘 활성 효과는 없다.
			++OutHandles.InstantEffectsApplied;
		}
		else
		{
			VB_LOG(Error, "[AbilitySet] %s: 효과 %s 가 걸리지 않았다", *GetName(), *Entry.Effect->GetName());
		}
	}

	VB_LOG(Log, "[AbilitySet] %s -> %s: 능력 %d · 효과 %d(순간 %d)", *GetName(), *GetNameSafe(ASC->GetOwner()),
		OutHandles.AbilitySpecHandles.Num(), OutHandles.EffectHandles.Num() + OutHandles.InstantEffectsApplied,
		OutHandles.InstantEffectsApplied);
}

TArray<FText> UVBAbilitySet::FindProblems(const TArray<FVBAbilitySetAbility>& Abilities, const TArray<FVBAbilitySetEffect>& Effects)
{
	TArray<FText> Problems;
	TSet<const UClass*> Seen;
	for (int32 Index = 0; Index < Abilities.Num(); ++Index)
	{
		const FVBAbilitySetAbility& Entry = Abilities[Index];
		if (!Entry.Ability)
		{
			Problems.Add(FText::Format(LOCTEXT("NullAbility", "능력 {0}번이 비었다"), Index));
			continue;
		}

		if (Entry.Level < 1)
		{
			Problems.Add(FText::Format(LOCTEXT("AbilityLevel", "능력 {0}번({1})의 레벨이 1 미만이다"), Index,
				FText::FromString(Entry.Ability->GetName())));
		}

		bool bAlreadySeen = false;
		Seen.Add(Entry.Ability.Get(), &bAlreadySeen);
		if (bAlreadySeen)
		{
			// 같은 클래스를 두 번 주면 스펙이 둘 생기고 입력, 활성이 어느 쪽으로 갈지 정해지지 않는다.
			Problems.Add(FText::Format(LOCTEXT("DuplicateAbility", "능력 {0}번({1})이 앞에서 이미 나왔다"), Index,
				FText::FromString(Entry.Ability->GetName())));
		}
	}

	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		const FVBAbilitySetEffect& Entry = Effects[Index];
		if (!Entry.Effect)
		{
			Problems.Add(FText::Format(LOCTEXT("NullEffect", "효과 {0}번이 비었다"), Index));
		}
		else if (Entry.Level < 1.0f)
		{
			Problems.Add(FText::Format(LOCTEXT("EffectLevel", "효과 {0}번({1})의 레벨이 1 미만이다"), Index,
				FText::FromString(Entry.Effect->GetName())));
		}
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBAbilitySet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FText& Problem : FindProblems(GrantedAbilities, GrantedEffects))
	{
		Context.AddError(Problem);
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
