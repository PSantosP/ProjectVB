// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Messages/VBGameplayMessageSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/Stack.h"
#include "VB/VB.h"

namespace VBGameplayMessage
{
	// 켜면 보낼 때마다 채널과 내용 구조체를 글로 찍는다(Lyra GameplayMessageSubsystem.LogMessages).
	static int32 LogMessages = 0;
	static FAutoConsoleVariableRef CVarLogMessages(TEXT("vb.Messages.Log"), LogMessages,
		TEXT("1 = log every gameplay message broadcast with its channel and payload."));
}

void FVBMessageListenerHandle::Unregister()
{
	if (UVBGameplayMessageSubsystem* StrongSubsystem = Subsystem.Get())
	{
		StrongSubsystem->UnregisterListener(*this);
	}

	Subsystem.Reset();
	Channel = FGameplayTag();
	Id = 0;
}

bool FVBMessageListenerHandle::IsValid() const
{
	const UVBGameplayMessageSubsystem* StrongSubsystem = Subsystem.Get();
	return Id != 0 && StrongSubsystem && !StrongSubsystem->IsDeinitialized();
}

UVBGameplayMessageSubsystem* UVBGameplayMessageSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UVBGameplayMessageSubsystem>() : nullptr;
}

void UVBGameplayMessageSubsystem::Deinitialize()
{
	// 먼저 해체 표시를 세우고 모든 듣기에 지움 표시를 남긴 뒤 비운다 - 이미 복사된 배달도 남은 콜백을 부르지 않는다(11.3).
	bDeinitialized = true;
	for (TPair<FGameplayTag, TArray<TSharedRef<FListener>>>& Pair : ListenerMap)
	{
		for (const TSharedRef<FListener>& Listener : Pair.Value)
		{
			Listener->bRemoved = true;
		}
	}

	ListenerMap.Reset();
	Super::Deinitialize();
}

void UVBGameplayMessageSubsystem::BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Gameplay messages are game-thread only (%s)"), *Channel.ToString()))
	{
		return;
	}

	if (bDeinitialized)
	{
		VB_LOG(Warning, "[메시지] 해체된 버스로 보냄 %s - 버린다", *Channel.ToString());
		return;
	}

	if (!StructType || !MessageBytes)
	{
		VB_LOG(Error, "[메시지] %s: 내용 구조체가 없다 - 버린다", *Channel.ToString());
		return;
	}

	if (BroadcastDepth >= MaxBroadcastDepth)
	{
		VB_LOG(Error, "[메시지] %s: 보내기 안의 다시 보내기가 %d 겹을 넘었다 - 이 보내기를 버린다(듣기끼리 서로 보내는 고리?)",
			*Channel.ToString(), MaxBroadcastDepth);
		return;
	}

	++BroadcastDepth;
	ON_SCOPE_EXIT { --BroadcastDepth; };

	if (VBGameplayMessage::LogMessages != 0)
	{
		FString Text;
		StructType->ExportText(Text, MessageBytes, /*Defaults=*/nullptr, /*OwnerObject=*/nullptr, PPF_None, /*ExportRootScope=*/nullptr);
		VB_LOG(Log, "[메시지] %s %s %s", *GetPathNameSafe(GetGameInstance()), *Channel.ToString(), *Text);
	}

	// 보낸 채널에서 부모로 올라간다. 보낸 채널에서는 두 맞춤 다, 부모에서는 부분 맞춤만 받는다. 콜백은 언제나 **보낸 채널**을 받는다.
	bool bOnInitialTag = true;
	for (FGameplayTag Tag = Channel; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const TArray<TSharedRef<FListener>>* Listeners = ListenerMap.Find(Tag))
		{
			// 이 채널에 도착한 때의 복사본(Lyra 와 같은 범위): 이 채널에 새로 등록한 듣기는 이번에 안 불리고,
			//   아직 안 간 부모 채널에 새로 등록한 듣기는 불린다(11.2). 지운 듣기는 표시로 건너뛴다(F5).
			const TArray<TSharedRef<FListener>, TInlineAllocator<8>> Snapshot(*Listeners);
			for (const TSharedRef<FListener>& Listener : Snapshot)
			{
				if (Listener->bRemoved || (!bOnInitialTag && Listener->MatchType != EVBMessageMatch::PartialMatch))
				{
					continue;
				}

				if (Listener->bHadValidType && !Listener->StructType.IsValid())
				{
					// 지금 도는 태그로 지운다 - Lyra 는 보낸 채널로 지워 부모 듣기는 남고 남의 듣기가 지워질 수 있다(F3).
					VB_LOG(Warning, "[메시지] %s 의 듣기 형이 사라졌다 - 목록에서 뺀다", *Tag.ToString());
					UnregisterListenerInternal(Tag, Listener->Id);
					continue;
				}

				if (Listener->bHadOwner && !Listener->Owner.IsValid())
				{
					UnregisterListenerInternal(Tag, Listener->Id); // F4: 소유자를 잃은 멤버 리스너
					continue;
				}

				if (Listener->bHadValidType && !StructType->IsChildOf(Listener->StructType.Get()))
				{
					VB_LOG(Error, "[메시지] 형 어긋남 %s: 보낸 형 %s, %s 의 듣기는 %s 를 기대한다", *Channel.ToString(),
						*StructType->GetPathName(), *Tag.ToString(), *Listener->StructType->GetPathName());
					continue;
				}

				Listener->Callback(Channel, StructType, MessageBytes);
				if (bDeinitialized)
				{
					return; // 콜백이 버스를 내렸다 - 남은 듣기를 깨우지 않는다
				}
			}
		}

		bOnInitialTag = false;
	}
}

FVBMessageListenerHandle UVBGameplayMessageSubsystem::RegisterListenerInternal(FGameplayTag Channel, FErasedCallback&& Callback,
	const UScriptStruct* StructType, EVBMessageMatch MatchType,
	TWeakObjectPtr<const UObject> Owner)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Gameplay message listeners are game-thread only (%s)"), *Channel.ToString()))
	{
		return FVBMessageListenerHandle();
	}

	if (bDeinitialized || !Callback)
	{
		VB_LOG(Warning, "[메시지] %s 듣기 등록 거절 - %s", *Channel.ToString(), bDeinitialized ? TEXT("해체된 버스") : TEXT("콜백 없음"));
		return FVBMessageListenerHandle();
	}

	if (NextListenerId == MAX_int64)
	{
		VB_LOG(Error, "[메시지] 듣기 번호를 다 썼다 - 등록 거절(번호를 0 이나 재사용으로 되돌리지 않는다)");
		return FVBMessageListenerHandle();
	}

	TSharedRef<FListener> Listener = MakeShared<FListener>();
	Listener->Callback = MoveTemp(Callback);
	Listener->Id = ++NextListenerId;
	Listener->MatchType = MatchType;
	Listener->StructType = StructType;
	Listener->bHadValidType = StructType != nullptr;
	Listener->Owner = Owner;
	Listener->bHadOwner = !Owner.IsExplicitlyNull();
	ListenerMap.FindOrAdd(Channel).Add(Listener);
	return FVBMessageListenerHandle(this, Channel, Listener->Id);
}

void UVBGameplayMessageSubsystem::UnregisterListener(const FVBMessageListenerHandle& Handle)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Gameplay message listeners are game-thread only (%s)"), *Handle.Channel.ToString()))
	{
		return;
	}

	if (Handle.Id == 0 || Handle.Subsystem.Get() != this)
	{
		return;
	}

	UnregisterListenerInternal(Handle.Channel, Handle.Id);
}

void UVBGameplayMessageSubsystem::UnregisterListenerInternal(FGameplayTag Channel, int64 Id)
{
	TArray<TSharedRef<FListener>>* Listeners = ListenerMap.Find(Channel);
	if (!Listeners)
	{
		return;
	}

	const int32 Index = Listeners->IndexOfByPredicate([Id](const TSharedRef<FListener>& Listener) { return Listener->Id == Id; });
	if (Index != INDEX_NONE)
	{
		(*Listeners)[Index]->bRemoved = true;
		Listeners->RemoveAtSwap(Index);
	}

	if (Listeners->IsEmpty())
	{
		ListenerMap.Remove(Channel);
	}
}

