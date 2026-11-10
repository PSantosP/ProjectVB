// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/CancellableAsyncAction.h"
#include "GameplayTagContainer.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "Messages/VBGameplayMessageTypes.h"
#include "VBAsyncAction_ListenForGameplayMessage.generated.h"

class UScriptStruct;
class UWorld;
struct FFrame;

// ProxyObject 는 GetPayload 를 부를 대상. ActualChannel = 실제로 받은 채널(부분 맞춤이면 더 깊을 수 있다).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBAsyncGameplayMessageDelegate, UVBAsyncAction_ListenForGameplayMessage*, ProxyObject,
	FGameplayTag, ActualChannel);

/**
 * BP 에서 메시지를 듣는 비동기 액션. 게임 인스턴스가 붙잡으므로 쓰는 위젯은 끝날 때 Cancel 을 불러야 한다.
 * 전용 노드가 없어 일반 비동기 노드 + GetPayload 로 쓴다.
 */
UCLASS(BlueprintType)
class VB_API UVBAsyncAction_ListenForGameplayMessage : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	// PayloadType 이 비면 채널만 알리는 듣기(GetPayload 는 언제나 거짓).
	UFUNCTION(BlueprintCallable, Category="VB|Messages", meta=(WorldContext="WorldContextObject", BlueprintInternalUseOnly="true"))
	static UVBAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(UObject* WorldContextObject, FGameplayTag Channel,
		UScriptStruct* PayloadType,
		EVBMessageMatch MatchType = EVBMessageMatch::ExactMatch);

	// 받은 내용을 출력 구조체에 복사한다(출력 형 = 설정 형). OnMessageReceived 안에서만 참이다.
	UFUNCTION(BlueprintCallable, CustomThunk, Category="VB|Messages", meta=(CustomStructureParam="OutPayload"))
	bool GetPayload(UPARAM(ref) int32& OutPayload);
	DECLARE_FUNCTION(execGetPayload);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

	UPROPERTY(BlueprintAssignable, Category="VB|Messages")
	FVBAsyncGameplayMessageDelegate OnMessageReceived;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBGameplayMessageTestAccess;
#endif
	void HandleMessageReceived(FGameplayTag Channel, const UScriptStruct* StructType, const void* Payload);

	// 배달 중에만 유효한, 임시로 가져온 포인터. 배달이 겹치면 안쪽이 끝날 때 바깥 값을 되살린다.
	const void* ReceivedMessagePayloadPtr = nullptr;
	TWeakObjectPtr<UWorld> WorldPtr;
	FGameplayTag ChannelToRegister;
	TWeakObjectPtr<UScriptStruct> MessageStructType;
	EVBMessageMatch MessageMatchType = EVBMessageMatch::ExactMatch;
	FVBMessageListenerHandle ListenerHandle;
	// 두 번째 Activate 는 듣기를 또 만들지 않는다.
	bool bActivated = false;
};
