// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration

class UAbilitySystemComponent;
class UUserWidget;

/**
 * HUD 위젯을 소유 PC 의 PlayerState 쪽 대상에 잇는 연결 하나. 구성 때 Follow 로 걸고 해체 때 Stop 으로 푼다.
 * PlayerState 가 이미 있으면 거는 순간 한 번 부른다. 소유 PC 가 AVBPlayerController 가 아니면 걸지 않는다.
 */
struct VB_API FVBPlayerStateBinding
{
	// PlayerState 의 ASC 를 Widget->InitializeWithASC 로 넘긴다. ASC 가 없으면 nullptr 을 넘긴다.
	template <typename WidgetType>
	void FollowASC(WidgetType* Widget)
	{
		Start(*Widget, FVBOnPlayerStateSet::FDelegate::CreateWeakLambda(Widget, [Widget](APlayerState* PlayerState)
		{
			Widget->InitializeWithASC(FindASC(PlayerState));
		}));
	}

	// 소유 PC 에서 ComponentType::FindFor 로 찾아 Current 와 다를 때만 Rebind 를 부른다. Current 는 Widget 의 멤버다.
	template <typename ComponentType, typename WidgetType, typename CurrentType>
	void FollowComponent(WidgetType* Widget, const CurrentType& Current, void (WidgetType::*Rebind)(ComponentType*))
	{
		const CurrentType* Tracked = &Current;
		Start(*Widget, FVBOnPlayerStateSet::FDelegate::CreateWeakLambda(Widget, [Widget, Tracked, Rebind](APlayerState* /*PlayerState*/)
		{
			// 찾는 출발점은 PC 다. 폰의 PlayerState 는 따로 복제돼 늦을 수 있다.
			ComponentType* Found = ComponentType::FindFor(Widget->GetOwningPlayer());
			if (Found != Tracked->Get())
			{
				(Widget->*Rebind)(Found);
			}
		}));
	}

	void Stop() { Registration.Unregister(); }

private:
	void Start(const UUserWidget& Widget, FVBOnPlayerStateSet::FDelegate Delegate);
	static UAbilitySystemComponent* FindASC(APlayerState* PlayerState);

	FVBPlayerStateSetRegistration Registration;
};
