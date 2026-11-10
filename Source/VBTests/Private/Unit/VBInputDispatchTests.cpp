#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "VBInputDispatchFixtureAbilities.h"

// ASC 의 입력 분배 - 반격이 LMB 에 닿는 길(실패하면 다음 후보)과 켜진 능력이 입력을 받는 규칙을 고정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputDispatchTest, "VB.Unit.Input.AbilityDispatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	template <typename T>
	T* InstanceOf(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
	}

	bool IsActive(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
}

bool FVBInputDispatchTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
		.ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("isolated world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AActor* Avatar = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("avatar"), Avatar))
	{
		return false;
	}

	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Avatar);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Avatar, Avatar);

	const FGameplayAbilitySpecHandle Light = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureLight::StaticClass()));
	const FGameplayAbilitySpecHandle Riposte = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureRiposte::StaticClass()));
	const FGameplayAbilitySpecHandle Hold = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureHold::StaticClass()));

	// 1) 막는 태그가 없으면 첫 후보(경공격)가 켜지고 뒤 후보는 시도되지 않는다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	TestTrue(TEXT("Light activates when nothing blocks it"), IsActive(*ASC, Light));
	TestFalse(TEXT("Riposte stays off without its window"), IsActive(*ASC, Riposte));

	// 2) 켜진 능력이 다시 누른 입력의 주인이다 - 다른 후보를 켜지 않고 그 능력에 넘긴다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	UVBInputFixtureLight* LightInstance = InstanceOf<UVBInputFixtureLight>(*ASC, Light);
	TestEqual(TEXT("Active light receives the second press"), LightInstance ? LightInstance->PressedWhileActive : -1, 1);
	TestFalse(TEXT("Second press does not start riposte"), IsActive(*ASC, Riposte));
	ASC->CancelAbilityHandle(Light);

	// 3) 경공격이 태그에 막히면 같은 입력의 다음 후보(반격)로 내려간다.
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	TestFalse(TEXT("Light is blocked by the riposte window"), IsActive(*ASC, Light));
	TestTrue(TEXT("Riposte is reached through the failed light"), IsActive(*ASC, Riposte));
	ASC->CancelAbilityHandle(Riposte);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);

	// 4) 놓으면 끝나는 홀드 능력은 놓음에 취소된다 - 취소 전에 능력이 놓음 통지를 받고 Spec 의 누르는 중 표시가 내려간다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Combat_Guard);
	TestTrue(TEXT("Hold ability activates on press"), IsActive(*ASC, Hold));
	const FGameplayAbilitySpec* PressedSpec = ASC->FindAbilitySpecFromHandle(Hold);
	TestTrue(TEXT("Activating press marks the spec pressed"), PressedSpec && PressedSpec->InputPressed);
	UVBInputFixtureHold* HoldProbe = InstanceOf<UVBInputFixtureHold>(*ASC, Hold);
	const int32 ReleasesBefore = HoldProbe ? HoldProbe->ReleasedWhileActive : -1;
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Combat_Guard);
	TestFalse(TEXT("Hold ability cancels on release"), IsActive(*ASC, Hold));
	TestEqual(TEXT("Active ability gets exactly one release notice"), HoldProbe ? HoldProbe->ReleasedWhileActive - ReleasesBefore : -1, 1);
	const FGameplayAbilitySpec* HoldSpec = ASC->FindAbilitySpecFromHandle(Hold);
	TestTrue(TEXT("Spec input-pressed bit is cleared on release"), HoldSpec && !HoldSpec->InputPressed);

	// 5) 놓아도 안 끝나는 능력(상호작용 탭과 같은 정책)은 놓음에 남는다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Combat_Guard);
	if (UVBInputFixtureHold* HoldInstance = InstanceOf<UVBInputFixtureHold>(*ASC, Hold))
	{
		HoldInstance->bCancelOnInputRelease = false;
	}

	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Combat_Guard);
	TestTrue(TEXT("Ability that keeps running on release stays active"), IsActive(*ASC, Hold));
	ASC->CancelAbilityHandle(Hold);

	// 6) 놓음은 그 입력을 받은 켜진 능력에게 간다 - 꺼진 앞 후보(경공격)에서 멈추지 않는다(2단계에서 고침).
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	TestTrue(TEXT("Riposte is active before release"), IsActive(*ASC, Riposte));
	// 이 행은 놓음이 누구에게 가는지 본다 - 놓으면 끝나는 정책을 여기서 명시한다(제품의 반격 정책은 바꾸지 않는다).
	if (UVBInputFixtureRiposte* RiposteInstance = InstanceOf<UVBInputFixtureRiposte>(*ASC, Riposte))
	{
		RiposteInstance->bCancelOnInputRelease = true;
	}

	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Attack_Light);
	TestFalse(TEXT("Release reaches the active riposte past the inactive light"), IsActive(*ASC, Riposte));
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);

	// 7) 놓아도 계속되는 정책(공격 = bCancelOnInputRelease false)은 놓음에 끝나지 않는다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	if (UVBInputFixtureLight* Instance = InstanceOf<UVBInputFixtureLight>(*ASC, Light))
	{
		Instance->bCancelOnInputRelease = false;
	}

	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Attack_Light);
	TestTrue(TEXT("Attack keeps running after release"), IsActive(*ASC, Light));
	const FGameplayAbilitySpec* LightSpec = ASC->FindAbilitySpecFromHandle(Light);
	TestTrue(TEXT("Released attack spec is not marked pressed"), LightSpec && !LightSpec->InputPressed);
	ASC->CancelAbilityHandle(Light);

	// 7b) 버튼보다 먼저 끝난 능력도 놓음에 누르는 중 표시가 내려간다(Lyra - 놓음은 같은 입력의 모든 능력).
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Attack_Light);
	ASC->CancelAbilityHandle(Light);
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Attack_Light);
	LightSpec = ASC->FindAbilitySpecFromHandle(Light);
	TestTrue(TEXT("Ended ability is not left marked pressed after release"), LightSpec && !LightSpec->InputPressed);

	// 7c) 놓음 정책을 적지 않은 능력(기본값 false)은 놓아도 계속된다 - 회피, 정화, 처형이 키를 떼는 순간 끝나지 않는다
	// 놓음 통지는 받고 누르는 중 표시는 내려간다
	const FGameplayAbilitySpecHandle DefaultRelease = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureDefaultRelease::StaticClass()));
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Dodge);
	TestTrue(TEXT("Default-policy ability activates on press"), IsActive(*ASC, DefaultRelease));
	UVBInputFixtureDefaultRelease* DefaultProbe = InstanceOf<UVBInputFixtureDefaultRelease>(*ASC, DefaultRelease);
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Dodge);
	TestTrue(TEXT("Default-policy ability keeps running after release"), IsActive(*ASC, DefaultRelease));
	TestEqual(TEXT("It still gets the release notice"), DefaultProbe ? DefaultProbe->ReleasedWhileActive : -1, 1);
	const FGameplayAbilitySpec* DefaultSpec = ASC->FindAbilitySpecFromHandle(DefaultRelease);
	TestTrue(TEXT("Its spec is no longer marked pressed"), DefaultSpec && !DefaultSpec->InputPressed);
	ASC->CancelAbilityHandle(DefaultRelease);

	// 8) 능력이 토글이면 다시 누를 때 꺼지고, 놓음은 아무것도 하지 않는다(놓음 통지도 없다).
	const FGameplayAbilitySpecHandle Toggle = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureToggle::StaticClass()));
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Crouch);
	TestTrue(TEXT("Toggle turns on with the first press"), IsActive(*ASC, Toggle));
	UVBInputFixtureToggle* ToggleProbe = InstanceOf<UVBInputFixtureToggle>(*ASC, Toggle);
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Crouch);
	TestTrue(TEXT("Toggle ignores release"), IsActive(*ASC, Toggle));
	TestEqual(TEXT("Toggle gets no release notice"), ToggleProbe ? ToggleProbe->ReleasedWhileActive : -1, 0);
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Crouch);
	TestFalse(TEXT("Second press turns the toggle off"), IsActive(*ASC, Toggle));

	// 9) 활성화 전 준비 - 능력이 준비한 이벤트 데이터로 켜진다 - 유효한 이벤트 태그와 그 누름 번호가 본문까지 온다.
	const FGameplayAbilitySpecHandle Prepared = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixturePrepared::StaticClass()));
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Purify, 42);
	UVBInputFixturePrepared* PreparedProbe = InstanceOf<UVBInputFixturePrepared>(*ASC, Prepared);
	TestTrue(TEXT("Prepared ability activates"), IsActive(*ASC, Prepared));
	TestTrue(TEXT("Activation carries event data"), PreparedProbe && PreparedProbe->bReceivedPayload);
	TestTrue(TEXT("Event data has the input-activate tag"),
		PreparedProbe && PreparedProbe->ReceivedEventTag == VBGameplayTags::Event_Ability_InputActivate);
	TestEqual(TEXT("Press serial reaches the ability"), PreparedProbe ? PreparedProbe->ReceivedPress : -1, 42);
	TestEqual(TEXT("Prepared variant reaches the ability"), PreparedProbe ? PreparedProbe->ReceivedVariant : -1, 3);
	ASC->CancelAbilityHandle(Prepared);
	return true;
}

// 누름 기록 - 번호, 누르는 중, 중복 무시, 보존 수, 지움.
// 켜진 능력이 가져간 입력(ClaimedInputTags): 그 능력이 받고 다른 능력은 안 켜진다. 꺼져 있으면 종전 분배 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputClaimTest, "VB.Unit.Input.ClaimedInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBInputClaimTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
		.ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("isolated world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	AActor* Avatar = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("avatar"), Avatar))
	{
		return false;
	}

	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Avatar);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Avatar, Avatar);

	const FGameplayAbilitySpecHandle Dodge = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureDefaultRelease::StaticClass()));
	const FGameplayAbilitySpecHandle Claimer = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureClaimer::StaticClass()));

	// 가져가는 능력이 꺼져 있으면 회피 누름은 종전대로 회피를 켠다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Dodge);
	TestTrue(TEXT("inactive claimer steals nothing"), IsActive(*ASC, Dodge));
	ASC->CancelAbilityHandle(Dodge);

	// 켜져 있으면 그 능력이 누름, 놓음을 받고 회피는 안 켜진다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Interact);
	UVBInputFixtureClaimer* Probe = InstanceOf<UVBInputFixtureClaimer>(*ASC, Claimer);
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Dodge);
	TestFalse(TEXT("active claimer keeps the dodge off"), IsActive(*ASC, Dodge));
	TestEqual(TEXT("active claimer gets the press"), Probe ? Probe->PressedWhileActive : -1, 1);
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Dodge);
	TestEqual(TEXT("active claimer gets the release"), Probe ? Probe->ReleasedWhileActive : -1, 1);
	const FGameplayAbilitySpec* ClaimerSpec = ASC->FindAbilitySpecFromHandle(Claimer);
	TestTrue(TEXT("claimer spec is not left pressed"), ClaimerSpec && !ClaimerSpec->InputPressed);

	// 가져가지 않은 입력은 켜진 그 능력이 있어도 종전대로 간다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Crouch);
	TestEqual(TEXT("unclaimed input does not reach the claimer"), Probe ? Probe->PressedWhileActive : -1, 1);

	// 원격 클라가 보낸 누름, 놓음(ServerSetInputPressed/Released)이 서버의 켜진 능력에 닿는다 - 빠져나오기 판정은 서버가 한다.
	ASC->ServerSetInputPressed(Claimer);
	TestEqual(TEXT("server-side claimer gets the forwarded press"), Probe ? Probe->PressedWhileActive : -1, 2);
	ASC->ServerSetInputReleased(Claimer);
	TestEqual(TEXT("server-side claimer gets the forwarded release"), Probe ? Probe->ReleasedWhileActive : -1, 2);
	ASC->CancelAbilityHandle(Claimer);

	// 보낼지는 원격 소유 클라에서, 부른 쪽이 서버로 가는 길이 없을 때만(로컬 분배).
	TestTrue(TEXT("remote client forwards a locally dispatched claim"), UVBAbilitySystemComponent::ShouldForwardClaimedInput(false, true));
	TestFalse(TEXT("the server never forwards"), UVBAbilitySystemComponent::ShouldForwardClaimedInput(true, true));
	TestFalse(TEXT("an input with its own server entry is not sent twice"),
		UVBAbilitySystemComponent::ShouldForwardClaimedInput(false, false));

	// 가져간 능력이 누름과 놓음 사이에 끝나도 놓음이 그 스펙의 누르는 중 표시를 내린다.
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Interact);
	ASC->AbilityInputTagPressed(VBGameplayTags::Input_Dodge);
	ASC->CancelAbilityHandle(Claimer);
	ASC->AbilityInputTagReleased(VBGameplayTags::Input_Dodge);
	const FGameplayAbilitySpec* EndedClaimer = ASC->FindAbilitySpecFromHandle(Claimer);
	TestTrue(TEXT("a claimer that ended before the release is not left pressed"), EndedClaimer && !EndedClaimer->InputPressed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputPressRecordTest, "VB.Unit.Input.PressRecord",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBInputPressRecordTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
		.ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("isolated world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	AActor* Avatar = World->SpawnActor<AActor>();
	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Avatar);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Avatar, Avatar);
	const FGameplayTag Tag = VBGameplayTags::Input_Attack_Light;

	const int32 First = ASC->BeginLocalPress(Tag);
	const FVBInputPress* Press = ASC->FindPress(Tag, First);
	TestTrue(TEXT("Local press is recorded and held"), Press && Press->IsHeld());
	TestEqual(TEXT("Release returns the held press"), ASC->EndLocalPress(Tag), First);
	Press = ASC->FindPress(Tag, First);
	TestTrue(TEXT("Released press is no longer held"), Press && !Press->IsHeld());
	TestEqual(TEXT("Nothing held to release twice"), ASC->EndLocalPress(Tag), INDEX_NONE);

	// 서버 쪽: 이미 아는 번호(리슨 호스트의 로컬 기록, 중복 RPC)는 다시 기록하지 않는다 - 놓은 상태가 누르는 중으로 되살지 않는다.
	ASC->RecordPress(Tag, First);
	Press = ASC->FindPress(Tag, First);
	TestTrue(TEXT("Duplicate press RPC does not revive a released press"), Press && !Press->IsHeld());
	ASC->RecordRelease(Tag, First + 50);
	TestNull(TEXT("Release of an unknown press is dropped"), ASC->FindPress(Tag, First + 50));

	// 보존 수를 넘으면 오래된 것부터 밀려난다.
	int32 Latest = First;
	for (int32 Index = 0; Index < UVBAbilitySystemComponent::MaxPressesPerTag + 2; ++Index)
	{
		Latest = ASC->BeginLocalPress(Tag);
		ASC->EndLocalPress(Tag);
	}

	TestNull(TEXT("Oldest press is evicted past the cap"), ASC->FindPress(Tag, First));
	TestNotNull(TEXT("Latest press is kept"), ASC->FindPress(Tag, Latest));

	// 지운 뒤 늦게 온 옛 번호는 새 누름으로 기록되지 않는다.
	ASC->ClearInputPresses();
	TestNull(TEXT("Clear drops presses"), ASC->FindPress(Tag, Latest));
	ASC->RecordPress(Tag, Latest);
	TestNull(TEXT("A stale serial after clear is not recorded"), ASC->FindPress(Tag, Latest));
	TestEqual(TEXT("Numbering continues after clear"), ASC->BeginLocalPress(Tag), Latest + 1);

	// 서버 입구 - 물리 기록과 요청 수락은 따로이고, 받아들인 요청만 동작으로 이어진다
	// 리슨 호스트 정상 흐름 = 로컬 Begin(기록) -> 같은 번호의 서버 요청(처음이니 받아들임).
	const int32 HostPress = Latest + 1;
	TestTrue(TEXT("Host's first server request for a locally recorded press is accepted"), ASC->AcceptPressRequest(Tag, HostPress));
	TestFalse(TEXT("The same press request again is not accepted"), ASC->AcceptPressRequest(Tag, HostPress));
	TestFalse(TEXT("An older press request is not accepted"), ASC->AcceptPressRequest(Tag, HostPress - 1));
	TestFalse(TEXT("An invalid serial is not accepted"), ASC->AcceptPressRequest(Tag, INDEX_NONE));
	ASC->EndLocalPress(Tag);
	TestTrue(TEXT("Host's release request after the local release is accepted once"), ASC->AcceptReleaseRequest(Tag, HostPress));
	TestFalse(TEXT("The same release request again is not accepted"), ASC->AcceptReleaseRequest(Tag, HostPress));
	TestFalse(TEXT("A release request for an unknown press is not accepted"), ASC->AcceptReleaseRequest(Tag, HostPress + 30));

	// 원격 클라 흐름 = 서버에 기록이 없는 번호의 요청 - 받아들이며 기록한다.
	const int32 RemotePress = HostPress + 1;
	TestTrue(TEXT("Remote press request is accepted and recorded"), ASC->AcceptPressRequest(Tag, RemotePress));
	const FVBInputPress* Remote = ASC->FindPress(Tag, RemotePress);
	TestTrue(TEXT("Remote press is held until its release request"), Remote && Remote->IsHeld());
	TestTrue(TEXT("Remote release request is accepted"), ASC->AcceptReleaseRequest(Tag, RemotePress));
	Remote = ASC->FindPress(Tag, RemotePress);
	TestTrue(TEXT("Remote press is released by its accepted request"), Remote && !Remote->IsHeld());

	// 지운 뒤: 이미 받은 번호는 다시 못 들어온다.
	ASC->ClearInputPresses();
	TestFalse(TEXT("A press request already accepted before clear is not accepted again"), ASC->AcceptPressRequest(Tag, RemotePress));

	// 한 번에 너무 멀리 뛴 번호는 받지 않는다 - 받으면 기준이 영구히 올라 그 뒤 정상 입력이 다 막힌다.
	TestFalse(TEXT("A serial far past the last accepted is refused"),
		ASC->AcceptPressRequest(Tag, RemotePress + UVBAbilitySystemComponent::MaxPressSerialJump + 1));
	TestTrue(TEXT("The next normal serial is still accepted"), ASC->AcceptPressRequest(Tag, RemotePress + 1));
	return true;
}
