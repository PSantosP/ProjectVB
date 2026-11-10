// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Messages/VBAsyncAction_ListenForGameplayMessage.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/Stack.h"

UVBAsyncAction_ListenForGameplayMessage* UVBAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
	UObject* WorldContextObject, FGameplayTag Channel, UScriptStruct* PayloadType, EVBMessageMatch MatchType)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	UVBAsyncAction_ListenForGameplayMessage* Action = NewObject<UVBAsyncAction_ListenForGameplayMessage>();
	Action->WorldPtr = World;
	Action->ChannelToRegister = Channel;
	Action->MessageStructType = PayloadType;
	Action->MessageMatchType = MatchType;
	Action->RegisterWithGameInstance(World);
	return Action;
}

void UVBAsyncAction_ListenForGameplayMessage::Activate()
{
	if (bActivated)
	{
		return;
	}

	bActivated = true;
	if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(WorldPtr.Get()))
	{
		TWeakObjectPtr<UVBAsyncAction_ListenForGameplayMessage> WeakThis(this);
		// 소유자 = 이 액션: 액션이 사라지면 버스가 다음 배달 때 이 리스너를 뺀다(F4).
		ListenerHandle = Bus->RegisterListenerInternal(ChannelToRegister,
			[WeakThis](FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload)
			{
				if (UVBAsyncAction_ListenForGameplayMessage* StrongThis = WeakThis.Get())
				{
					StrongThis->HandleMessageReceived(Channel, StructType, Payload);
				}
			},
			MessageStructType.Get(), MessageMatchType, TWeakObjectPtr<const UObject>(this));
		if (ListenerHandle.IsValid())
		{
			return;
		}
	}

	SetReadyToDestroy();
}

void UVBAsyncAction_ListenForGameplayMessage::SetReadyToDestroy()
{
	ListenerHandle.Unregister();
	Super::SetReadyToDestroy();
}

bool UVBAsyncAction_ListenForGameplayMessage::GetPayload(int32& OutPayload)
{
	// 불리지 않는다 - BP 는 아래 썽크로 온다.
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UVBAsyncAction_ListenForGameplayMessage::execGetPayload)
{
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* OutPtr = Stack.MostRecentPropertyAddress;
	const FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH;
	bool bSuccess = false;
	// 출력 형 = 설정 형(정확히), 복사도 설정 형으로 - 받은 메시지가 자식 형이어도 출력 저장소 크기를 넘지 않는다(11.4).
	if (StructProp && StructProp->Struct && OutPtr && StructProp->Struct == P_THIS->MessageStructType.Get()
		&& P_THIS->ReceivedMessagePayloadPtr)
	{
		StructProp->Struct->CopyScriptStruct(OutPtr, P_THIS->ReceivedMessagePayloadPtr);
		bSuccess = true;
	}

	*static_cast<bool*>(RESULT_PARAM) = bSuccess;
}

void UVBAsyncAction_ListenForGameplayMessage::HandleMessageReceived(FGameplayTag Channel, const UScriptStruct* StructType,
	const void* Payload)
{
	// 취소된 액션은 이미 복사된 배달이 와도 내보내지 않는다(F8).
	if (!IsActive())
	{
		return;
	}
	// 받음 판정은 버스와 같게 '보낸 형이 설정 형의 자식이면' (Lyra 는 여기서만 정확한 형을 요구해 버스와 어긋난다).
	const UScriptStruct* Expected = MessageStructType.Get();
	if (!Expected || (StructType && StructType->IsChildOf(Expected)))
	{
		const void* OuterPayload = ReceivedMessagePayloadPtr;
		ReceivedMessagePayloadPtr = Payload;
		ON_SCOPE_EXIT { ReceivedMessagePayloadPtr = OuterPayload; };
		OnMessageReceived.Broadcast(this, Channel);
	}

	if (!OnMessageReceived.IsBound())
	{
		// 이 액션을 만든 BP 객체가 사라지면 묶음이 풀린다 - 그때 스스로 끝낸다(Lyra 와 같다. 위젯은 Cancel 을 부르는 것이 계약이다).
		SetReadyToDestroy();
	}
}
