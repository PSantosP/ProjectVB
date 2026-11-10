// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"

// ASC attribute 구독 하나를 가지고, 구독한 바로 그 source 에서 해제한다
struct FVBAttributeSubscription
{
	FVBAttributeSubscription() = default;
	FVBAttributeSubscription(const FVBAttributeSubscription&) = delete;
	FVBAttributeSubscription& operator=(const FVBAttributeSubscription&) = delete;
	~FVBAttributeSubscription() { Reset(); }

	template <typename UserClass>
	void Bind(UAbilitySystemComponent& ASC, const FGameplayAttribute& InAttribute, UserClass* Listener,
		void (UserClass::*Handler)(const FOnAttributeChangeData&))
	{
		Reset();
		Source = &ASC;
		Attribute = InAttribute;
		Handle = ASC.GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(Listener, Handler);
	}

	void Reset()
	{
		if (UAbilitySystemComponent* ASC = Source.Get())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(Attribute).Remove(Handle);
		}

		Source.Reset();
		Handle.Reset();
	}

private:
	TWeakObjectPtr<UAbilitySystemComponent> Source;
	FGameplayAttribute Attribute;
	FDelegateHandle Handle;
};
