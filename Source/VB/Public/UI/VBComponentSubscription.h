// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"

/** 컴포넌트 신호 구독 하나. 구독한 컴포넌트를 기억했다가 바로 그 컴포넌트에서 해제한다. */
template <typename ComponentType>
class TVBComponentSubscription
{
public:
	ComponentType* Get() const { return Component.Get(); }

	// 앞 구독을 풀고 NewComponent 의 Signal 에 Method 를 건다. NewComponent 가 nullptr 이면 풀기만 한다.
	template <typename SignalType, typename ListenerType, typename MethodType>
	void Bind(ComponentType* NewComponent, SignalType ComponentType::* Signal, ListenerType* Listener, MethodType Method)
	{
		Reset();
		Component = NewComponent;
		if (NewComponent)
		{
			FDelegateHandle Handle = (NewComponent->*Signal).AddUObject(Listener, Method);
			Release = [Signal, Handle](ComponentType& Bound) { (Bound.*Signal).Remove(Handle); };
		}
	}

	void Reset()
	{
		if (ComponentType* Bound = Component.Get(); Bound && Release)
		{
			Release(*Bound);
		}

		Release.Reset();
		Component.Reset();
	}

private:
	TWeakObjectPtr<ComponentType> Component;
	TFunction<void(ComponentType&)> Release;
};
