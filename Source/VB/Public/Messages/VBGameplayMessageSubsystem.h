// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Messages/VBGameplayMessageTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "VBGameplayMessageSubsystem.generated.h"

class UVBAsyncAction_ListenForGameplayMessage;
class UVBGameplayMessageSubsystem;
struct FFrame;

/**
 * 듣기를 지울 때 쓰는 손잡이. 번호는 버스 전체에서 늘기만 한다(낡은 사본이 새 듣기를 지우지 않게).
 * 소멸할 때 스스로 지우지 않는다 - 듣는 쪽이 해체에서 지운다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBMessageListenerHandle
{
	GENERATED_BODY()

	FVBMessageListenerHandle() = default;

	// 버스가 없어도 이 손잡이는 비운다.
	void Unregister();
	bool IsValid() const;

private:
	friend UVBGameplayMessageSubsystem;

	FVBMessageListenerHandle(UVBGameplayMessageSubsystem* InSubsystem, FGameplayTag InChannel, int64 InId)
		: Subsystem(InSubsystem), Channel(InChannel), Id(InId)
	{
	}

	UPROPERTY(Transient)
	TWeakObjectPtr<UVBGameplayMessageSubsystem> Subsystem;

	UPROPERTY(Transient)
	FGameplayTag Channel;

	UPROPERTY(Transient)
	int64 Id = 0;
};

/**
 * 게임 메시지 버스(게임 인스턴스마다 하나). 보내는 쪽과 듣는 쪽은 "채널 태그 + USTRUCT" 만 맞춘다.
 * 동기로 나눠 주고 보관하지 않으며(늦은 듣기는 지난 메시지를 못 받는다) 네트워크로 보내지 않는다. 듣기 순서는 보장하지 않는다.
 */
UCLASS()
class VB_API UVBGameplayMessageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	friend UVBAsyncAction_ListenForGameplayMessage;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBGameplayMessageTestAccess;
#endif

public:
	// 월드나 게임 인스턴스가 없으면 null. 부르는 쪽이 처리한다.
	static UVBGameplayMessageSubsystem* Get(const UObject* WorldContextObject);

	virtual void Deinitialize() override;

	bool IsDeinitialized() const { return bDeinitialized; }

	// 채널로 보낸다. 듣는 형이 보낸 형과 같거나 부모가 아니면 Error 를 남기고 그 듣기만 건너뛴다.
	template <typename FMessageStructType>
	void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message)
	{
		BroadcastMessageInternal(Channel, TBaseStructure<FMessageStructType>::Get(), &Message);
	}

	// 람다로 듣는다. 지울 책임은 손잡이를 받은 쪽에 있다.
	template <typename FMessageStructType>
	FVBMessageListenerHandle RegisterListener(FGameplayTag Channel, TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback,
		EVBMessageMatch MatchType = EVBMessageMatch::ExactMatch)
	{
		return RegisterListenerInternal(Channel, MakeThunk<FMessageStructType>(MoveTemp(Callback)),
			TBaseStructure<FMessageStructType>::Get(), MatchType, TWeakObjectPtr<const UObject>());
	}

	// 객체의 멤버 함수로 듣는다. 객체가 사라지면 다음 배달 때 목록에서 뺀다.
	template <typename FMessageStructType, typename TOwner = UObject>
	FVBMessageListenerHandle RegisterListener(FGameplayTag Channel, TOwner* Object,
		void (TOwner::*Function)(FGameplayTag, const FMessageStructType&))
	{
		FVBMessageListenerParams<FMessageStructType> Params;
		Params.SetMessageReceivedCallback(Object, Function);
		return RegisterListener<FMessageStructType>(Channel, Params);
	}

	// 묶음으로 듣는다. 콜백이 비면 등록하지 않고 무효 손잡이를 돌려준다. 묶음은 복사해 쓰고 바꾸지 않는다.
	template <typename FMessageStructType>
	FVBMessageListenerHandle RegisterListener(FGameplayTag Channel, const FVBMessageListenerParams<FMessageStructType>& Params)
	{
		if (!Params.OnMessageReceivedCallback)
		{
			return FVBMessageListenerHandle();
		}

		TFunction<void(FGameplayTag, const FMessageStructType&)> Callback = Params.OnMessageReceivedCallback;
		return RegisterListenerInternal(Channel, MakeThunk<FMessageStructType>(MoveTemp(Callback)),
			TBaseStructure<FMessageStructType>::Get(), Params.MatchType, Params.Owner);
	}

	// 손잡이로 지운다. 이미 지웠거나 무효면 아무 일 없다.
	void UnregisterListener(const FVBMessageListenerHandle& Handle);

	// 재귀 보내기가 이 깊이를 넘으면 버린다(서로 보내는 두 듣기가 스택을 터뜨리지 않게).
	static constexpr int32 MaxBroadcastDepth = 16;

private:
	using FErasedCallback = TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>;

	template <typename FMessageStructType>
	static FErasedCallback MakeThunk(TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback)
	{
		// 형 검사(BroadcastMessageInternal)를 지난 뒤에만 불린다 - 보낸 형은 이 형이거나 그 자식이다.
		return [Inner = MoveTemp(Callback)](FGameplayTag ActualChannel, const UScriptStruct*, const void* Payload)
		{
			Inner(ActualChannel, *static_cast<const FMessageStructType*>(Payload));
		};
	}

	// 듣기 하나. 배달은 채널 목록의 복사본을 돌고, 지운 듣기는 bRemoved 로 건너뛴다.
	struct FListener
	{
		FErasedCallback Callback;
		int64 Id = 0;
		EVBMessageMatch MatchType = EVBMessageMatch::ExactMatch;
		TWeakObjectPtr<const UScriptStruct> StructType;
		bool bHadValidType = false;
		TWeakObjectPtr<const UObject> Owner;
		bool bHadOwner = false;
		bool bRemoved = false;
	};

	void BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes);
	// Owner 는 약한 포인터 그대로 받는다 - 이미 사라진 소유자의 리스너는 첫 배달에서 제거된다.
	FVBMessageListenerHandle RegisterListenerInternal(FGameplayTag Channel, FErasedCallback&& Callback, const UScriptStruct* StructType,
		EVBMessageMatch MatchType, TWeakObjectPtr<const UObject> Owner);
	void UnregisterListenerInternal(FGameplayTag Channel, int64 Id);

	TMap<FGameplayTag, TArray<TSharedRef<FListener>>> ListenerMap;
	int64 NextListenerId = 0;
	int32 BroadcastDepth = 0;
	// 해체 첫 줄에 세운다. 이후 손잡이는 무효, 등록, 보내기는 거절.
	bool bDeinitialized = false;
};
