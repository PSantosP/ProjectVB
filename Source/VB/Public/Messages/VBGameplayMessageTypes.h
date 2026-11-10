// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBGameplayMessageTypes.generated.h"

// 채널 맞춤. Lyra EGameplayMessageMatch 와 같다.
UENUM(BlueprintType)
enum class EVBMessageMatch : uint8
{
	// 그 채널만 받는다 - 'A.B' 로 들으면 A.B 는 받고 A.B.C 는 안 받는다.
	ExactMatch,
	// 그 채널과 자식 채널을 받는다 - 'A.B' 로 들으면 A.B 와 A.B.C 를 받는다.
	PartialMatch
};

// 듣기를 등록할 때의 묶음(Lyra FGameplayMessageListenerParams). 콜백이 비면 등록하지 않는다.
template <typename FMessageStructType>
struct FVBMessageListenerParams
{
	EVBMessageMatch MatchType = EVBMessageMatch::ExactMatch;

	TFunction<void(FGameplayTag, const FMessageStructType&)> OnMessageReceivedCallback;

	// 소유 객체가 있는 콜백. 소유자가 사라지면 버스가 다음 배달 때 이 리스너를 목록에서 뺀다(설계 4절 F4 - Lyra 는 남겨 둔다).
	TWeakObjectPtr<const UObject> Owner;

	template <typename TOwner = UObject>
	void SetMessageReceivedCallback(TOwner* Object, void (TOwner::*Function)(FGameplayTag, const FMessageStructType&))
	{
		if (!Object)
		{
			// 소유자 없는 멤버 콜백은 영영 호출되지 않고 제거할 방법도 없다 - 비워 두면 RegisterListener 가 거절한다.
			Owner = nullptr;
			OnMessageReceivedCallback = nullptr;
			return;
		}

		TWeakObjectPtr<TOwner> WeakObject(Object);
		Owner = Object;
		OnMessageReceivedCallback = [WeakObject, Function](FGameplayTag Channel, const FMessageStructType& Payload)
		{
			if (TOwner* StrongObject = WeakObject.Get())
			{
				(StrongObject->*Function)(Channel, Payload);
			}
		};
	}
};
