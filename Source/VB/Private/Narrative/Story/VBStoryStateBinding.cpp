// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBStoryStateBinding.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Narrative/Story/VBStoryStateComponent.h"

FVBStoryStateBinding::~FVBStoryStateBinding()
{
	Disconnect();
}

void FVBStoryStateBinding::Refresh(UObject* Owner, UWorld* World, FSimpleDelegate Requery)
{
	if (!IsValid(Owner) || !IsValid(World))
	{
		Disconnect();
		return;
	}

	if (BoundOwner.Get() != Owner || BoundWorld.Get() != World)
	{
		Disconnect();
	}

	BoundOwner = Owner;
	BoundWorld = World;
	RequestRequery = MoveTemp(Requery);
	if (!GameStateHandle.IsValid())
	{
		// 처음 찾은 뒤에도 계속 듣는다 - GameState 가 바뀌면 옛 출처를 떼어야 한다.
		GameStateHandle = World->GameStateSetEvent.AddWeakLambda(Owner, [this](AGameStateBase*) { Rebind(); });
	}

	Rebind();
}

void FVBStoryStateBinding::Rebind()
{
	if (!BoundOwner.IsValid() || !BoundWorld.IsValid())
	{
		Disconnect();
		return;
	}

	UVBStoryStateComponent* Current = UVBStoryStateComponent::Get(BoundWorld.Get());
	if (Current != Story.Get() || (Current && !StoryHandle.IsValid()) || (!Current && StoryHandle.IsValid()))
	{
		if (UVBStoryStateComponent* Previous = Story.Get())
		{
			Previous->OnStoryStateChanged.Remove(StoryHandle);
		}

		StoryHandle.Reset();
		Story = Current;
		if (Current)
		{
			StoryHandle = Current->OnStoryStateChanged.AddWeakLambda(BoundOwner.Get(), [this]()
			{
				ExecuteRequery();
			});
		}
	}
	// 구독한 뒤에 읽어서, 이미 복원된 사실이 다음 이벤트를 기다리지 않게 한다.
	ExecuteRequery();
}

void FVBStoryStateBinding::ExecuteRequery() const
{
	// 지역 복사본 - 콜백이 이 바인딩을 Disconnect 하면 호출 도중 멤버가 풀린다.
	const FSimpleDelegate Requery = RequestRequery;
	Requery.ExecuteIfBound();
}

UVBStoryStateComponent* FVBStoryStateBinding::GetStory() const
{
	return BoundOwner.IsValid() ? Story.Get() : nullptr;
}

void FVBStoryStateBinding::Disconnect()
{
	if (UVBStoryStateComponent* Previous = Story.Get())
	{
		Previous->OnStoryStateChanged.Remove(StoryHandle);
	}

	if (UWorld* World = BoundWorld.Get())
	{
		World->GameStateSetEvent.Remove(GameStateHandle);
	}

	StoryHandle.Reset();
	GameStateHandle.Reset();
	Story.Reset();
	BoundOwner.Reset();
	BoundWorld.Reset();
	RequestRequery.Unbind();
}
