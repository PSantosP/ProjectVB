// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "VBIsolatedGameWorld.h"
#include "GameFramework/GameStateBase.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "Narrative/Story/VBStoryStateComponent.h"

namespace
{
	UVBStoryStateComponent* MakeStory(UWorld& World)
	{
		AGameStateBase* GameState = World.SpawnActor<AGameStateBase>();
		if (!GameState)
		{
			return nullptr;
		}

		UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>(GameState);
		GameState->AddInstanceComponent(Story);
		Story->RegisterComponent();
		return Story;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStoryBindingLifecycleTest, "VB.Unit.Narrative.StoryBinding.Lifecycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStoryBindingLifecycleTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	FVBStoryStateBinding Binding;
	int32 Calls = 0;
	UVBStoryStateComponent* Seen = nullptr;
	const FSimpleDelegate Callback = FSimpleDelegate::CreateWeakLambda(Owner, [&]()
	{
		++Calls;
		Seen = Binding.GetStory();
	});
	Binding.Refresh(Owner, Fixture.World, Callback);
	TestEqual(TEXT("initial pending snapshot is reported"), Calls, 1);
	TestNull(TEXT("no provider is not a false snapshot"), Seen);
	UVBStoryStateComponent* First = MakeStory(*Fixture.World);
	UVBStoryStateComponent* Second = MakeStory(*Fixture.World);
	if (!TestNotNull(TEXT("first provider"), First) || !TestNotNull(TEXT("second provider"), Second))
	{
		return false;
	}

	Fixture.World->SetGameState(CastChecked<AGameStateBase>(First->GetOwner()));
	TestTrue(TEXT("late GameState binds current component"), Seen == First);
	Binding.Refresh(Owner, Fixture.World, Callback);
	const int32 BeforeEvent = Calls;
	First->OnStoryStateChanged.Broadcast();
	TestEqual(TEXT("refresh does not duplicate subscription"), Calls, BeforeEvent + 1);
	Fixture.World->SetGameState(CastChecked<AGameStateBase>(Second->GetOwner()));
	TestTrue(TEXT("replacement selects new component"), Seen == Second);
	const int32 AfterReplacement = Calls;
	First->OnStoryStateChanged.Broadcast();
	TestEqual(TEXT("old source detached"), Calls, AfterReplacement);
	Second->OnStoryStateChanged.Broadcast();
	TestEqual(TEXT("new source observed once"), Calls, AfterReplacement + 1);
	Fixture.World->SetGameState(nullptr);
	TestNull(TEXT("provider removal clears binding"), Seen);
	Binding.Disconnect();
	Binding.Disconnect();
	const int32 AfterDisconnect = Calls;
	Fixture.World->SetGameState(CastChecked<AGameStateBase>(First->GetOwner()));
	First->OnStoryStateChanged.Broadcast();
	TestEqual(TEXT("disconnect removes world and story listeners"), Calls, AfterDisconnect);
	TestNull(TEXT("disconnect exposes no provider"), Binding.GetStory());
	int32 SelfDisconnectCalls = 0;
	FVBStoryStateBinding SelfDisconnect;
	SelfDisconnect.Refresh(Owner, Fixture.World, FSimpleDelegate::CreateWeakLambda(Owner, [&]()
	{
		++SelfDisconnectCalls;
		SelfDisconnect.Disconnect();
	}));
	TestEqual(TEXT("a callback may disconnect its own binding"), SelfDisconnectCalls, 1);
	TestNull(TEXT("self-disconnect leaves no provider"), SelfDisconnect.GetStory());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStoryBindingOwnershipTest, "VB.Unit.Narrative.StoryBinding.Ownership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStoryBindingOwnershipTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld FirstWorld;
	FVBIsolatedGameWorld SecondWorld;
	if (!FirstWorld.Init(*this) || !SecondWorld.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* First = MakeStory(*FirstWorld.World);
	UVBStoryStateComponent* Second = MakeStory(*SecondWorld.World);
	if (!TestNotNull(TEXT("first provider"), First) || !TestNotNull(TEXT("second provider"), Second))
	{
		return false;
	}

	FirstWorld.World->SetGameState(CastChecked<AGameStateBase>(First->GetOwner()));
	SecondWorld.World->SetGameState(CastChecked<AGameStateBase>(Second->GetOwner()));
	AActor* Owner = FirstWorld.World->SpawnActor<AActor>();
	int32 Calls = 0;
	const FSimpleDelegate Callback = FSimpleDelegate::CreateWeakLambda(Owner, [&]() { ++Calls; });
	{
		FVBStoryStateBinding Binding;
		Binding.Refresh(Owner, FirstWorld.World, Callback);
		Binding.Refresh(Owner, SecondWorld.World, Callback);
		TestTrue(TEXT("world replacement resolves the second world"), Binding.GetStory() == Second);
		const int32 Baseline = Calls;
		First->OnStoryStateChanged.Broadcast();
		FirstWorld.World->SetGameState(nullptr);
		TestEqual(TEXT("old world and old source detached"), Calls, Baseline);
		Second->OnStoryStateChanged.Broadcast();
		TestEqual(TEXT("current source still observed"), Calls, Baseline + 1);
	}

	const int32 AfterDestruction = Calls;
	Second->OnStoryStateChanged.Broadcast();
	SecondWorld.World->SetGameState(nullptr);
	TestEqual(TEXT("RAII disconnects both handles"), Calls, AfterDestruction);
	{
		FVBStoryStateBinding Binding;
		Binding.Refresh(Owner, FirstWorld.World, Callback);
		const int32 BeforeOwnerGone = Calls;
		Owner->Destroy();
		FirstWorld.World->SetGameState(CastChecked<AGameStateBase>(First->GetOwner()));
		First->OnStoryStateChanged.Broadcast();
		TestEqual(TEXT("dead weak owner cannot receive callbacks"), Calls, BeforeOwnerGone);
		TestNull(TEXT("dead owner cannot expose a provider"), Binding.GetStory());
	}

	return true;
}

#endif
