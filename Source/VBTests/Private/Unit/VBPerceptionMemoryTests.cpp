#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/VBAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 시야에서 잃은 대상을 SightMaxAge 동안 기억한다 - 잠깐 안 보였다고 대상(과 보스 체력바)이 바로 사라지지 않게.
struct FVBPerceptionMemoryTestAccess
{
	static bool IsForgetPending(const AVBAIController& Controller)
	{
		return Controller.GetWorldTimerManager().IsTimerActive(Controller.LostTargetTimerHandle);
	}

	static float ForgetDelay(const AVBAIController& Controller)
	{
		return Controller.GetWorldTimerManager().GetTimerRate(Controller.LostTargetTimerHandle);
	}

	static void FireForget(AVBAIController& Controller)
	{
		Controller.GetWorldTimerManager().ClearTimer(Controller.LostTargetTimerHandle);
		Controller.ForgetLostTarget();
	}

	static void SetMaxAge(AVBAIController& Controller, float MaxAge) { Controller.SightConfig->SetMaxAge(MaxAge); }
};

namespace
{
	FActorPerceptionUpdateInfo SightUpdate(AActor* Target, bool bSensed)
	{
		const FAIStimulus Stimulus(*GetDefault<UAISense_Sight>(), 1.0f, Target->GetActorLocation(), FVector::ZeroVector,
			bSensed ? FAIStimulus::SensingSucceeded : FAIStimulus::SensingFailed);
		// 값 생성자는 AIModule 이 내보내지 않는다(링크 실패) - 기본 생성 뒤 칸을 채운다.
		FActorPerceptionUpdateInfo Info;
		Info.TargetId = GetTypeHash(Target);
		Info.Target = Target;
		Info.Stimulus = Stimulus;
		return Info;
	}

	FActorPerceptionUpdateInfo DamageUpdate(AActor* Attacker, bool bSensed)
	{
		FActorPerceptionUpdateInfo Info;
		Info.TargetId = GetTypeHash(Attacker);
		Info.Target = Attacker;
		Info.Stimulus = FAIStimulus(*GetDefault<UAISense_Damage>(), 10.0f, Attacker->GetActorLocation(), FVector::ZeroVector,
			bSensed ? FAIStimulus::SensingSucceeded : FAIStimulus::SensingFailed);
		return Info;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPerceptionMemoryTest, "VB.Unit.EnemyAI.SightMemory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPerceptionMemoryTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(true); // 칠판 초기화가 AI 시스템을 본다(VBCombatCoordinatorAuditTests 와 같은 준비)
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	// 마지막 단계가 다음 프레임까지 가므로 세계의 수명은 이 묶음이 쥔다 - 지연 명령이 끝나며 놓거나, 그 전에 돌아가면 여기서 놓는다.
	struct FWorldHold
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<UBlackboardData> Data;
		~FWorldHold() { World->DestroyWorld(false); Package->SetDirtyFlag(false); }
	};
	const TSharedRef<FWorldHold> Hold = MakeShared<FWorldHold>();
	Hold->World = World;
	Hold->Package.Reset(World->GetPackage());

	AVBAIController* Controller = World->SpawnActor<AVBAIController>();
	AActor* Player = World->SpawnActor<AActor>();
	APawn* Body = World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("target"), Player) || !TestNotNull(TEXT("pawn"), Body))
	{
		return false;
	}
	// 빙의를 먼저 한다 - OnPossess 가 적 설정으로 시야 수치를 다시 쓰므로, 시험 값은 그 뒤에 넣는다.
	Controller->Possess(Body);

	// 실제 BB 자산과 같은 이름의 대상 칸 하나.
	Hold->Data.Reset(NewObject<UBlackboardData>(Controller));
	UBlackboardData* Data = Hold->Data.Get();
	FBlackboardEntry& Entry = Data->Keys.AddDefaulted_GetRef();
	Entry.EntryName = TEXT("TargetActor");
	Entry.KeyType = NewObject<UBlackboardKeyType_Object>(Data);
	UBlackboardComponent* BB = nullptr;
	if (!TestTrue(TEXT("blackboard initialised"), Controller->UseBlackboard(Data, BB) && BB))
	{
		return false;
	}

	auto Target = [BB]() { return BB->GetValueAsObject(TEXT("TargetActor")); };

	const float MaxAge = 5.0f;
	FVBPerceptionMemoryTestAccess::SetMaxAge(*Controller, MaxAge);

	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, true));
	TestTrue(TEXT("sensed: target set"), Target() == Player);

	// 잃음 -> 바로 비우지 않는다. 기억 시간 뒤 잊기가 걸린다.
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, false));
	TestTrue(TEXT("lost sight: target kept"), Target() == Player);
	TestTrue(TEXT("lost sight: forget is pending"), FVBPerceptionMemoryTestAccess::IsForgetPending(*Controller));
	TestEqual(TEXT("forget delay = SightMaxAge"), FVBPerceptionMemoryTestAccess::ForgetDelay(*Controller), MaxAge);

	// 다시 보이면 잊기를 거둔다.
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, true));
	TestFalse(TEXT("seen again: forget cancelled"), FVBPerceptionMemoryTestAccess::IsForgetPending(*Controller));
	TestTrue(TEXT("seen again: target kept"), Target() == Player);

	// 그 사이 다른 대상으로 바뀌었으면 잊기가 새 대상을 지우지 않는다.
	AActor* Other = World->SpawnActor<AActor>();
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, true));
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, false));
	BB->SetValueAsObject(TEXT("TargetActor"), Other);
	FVBPerceptionMemoryTestAccess::FireForget(*Controller);
	TestTrue(TEXT("forget does not clear a different target"), Target() == Other);

	// 기억 시간 0 = 엔진의 잊지 않음(UAISenseConfig::GetMaxAge) - 대상을 지키고 잊기를 걸지 않는다.
	FVBPerceptionMemoryTestAccess::SetMaxAge(*Controller, 0.0f);
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Other, false));
	TestTrue(TEXT("max age 0: never forgotten"), Target() == Other);
	TestFalse(TEXT("max age 0: no forget pending"), FVBPerceptionMemoryTestAccess::IsForgetPending(*Controller));

	// 다시 잃고 기억 시간이 지나면 실제 타이머가 비운다(지각 기록이 없어 지금 안 보인다 갈래만 지난다).
	//   새 타이머는 Tick 끝에서야 활성이 되고 같은 프레임 두 번째 Tick 은 막혀서(FTimerManager::Tick) 프레임을 넘겨 두 번 돌린다.
	//   지연 명령은 같은 프레임에 이어 돌므로 한 명령 안에서 단계를 나눈다.
	FVBPerceptionMemoryTestAccess::SetMaxAge(*Controller, MaxAge);
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, true));
	Controller->OnTargetPerceptionInfoUpdated(SightUpdate(Player, false));
	TWeakObjectPtr<AVBAIController> WeakController = Controller;
	TWeakObjectPtr<AActor> WeakPlayer = Player;
	TWeakObjectPtr<UBlackboardComponent> WeakBB = BB;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Hold, WeakController, WeakPlayer, WeakBB, MaxAge,
		bActivated = false]() mutable
	{
		AVBAIController* LiveController = WeakController.Get();
		UBlackboardComponent* LiveBB = WeakBB.Get();
		if (!LiveController || !LiveBB || !WeakPlayer.IsValid())
		{
			AddError(TEXT("test world objects disappeared before the forget timer step"));
			return true;
		}

		FTimerManager& Timers = Hold->World->GetTimerManager();
		if (!bActivated)
		{
			Timers.Tick(0.0f);
			bActivated = true;
			TestTrue(TEXT("timer activated: target still kept"), LiveBB->GetValueAsObject(TEXT("TargetActor")) == WeakPlayer.Get());
			return false;
		}

		if (Timers.HasBeenTickedThisFrame())
		{
			return false;
		}

		Timers.Tick(MaxAge + 0.1f);
		TestTrue(TEXT("max age passed unseen: target cleared by the timer"), LiveBB->GetValueAsObject(TEXT("TargetActor")) == nullptr);

		// 빙의가 풀리면 걸린 잊기를 거둔다(다음 폰의 칠판을 지우지 않게).
		LiveController->OnTargetPerceptionInfoUpdated(SightUpdate(WeakPlayer.Get(), true));
		LiveController->OnTargetPerceptionInfoUpdated(SightUpdate(WeakPlayer.Get(), false));
		LiveController->UnPossess();
		TestFalse(TEXT("unpossess cancels a pending forget"), FVBPerceptionMemoryTestAccess::IsForgetPending(*LiveController));
		return true;
	}));
	return true;
}

// 시야 원뿔 밖에서 맞아도 적대 공격자를 알아챈다 - 뒤에서 맞은 병사가 교전을 안 하던 것을 막는다. 아군, 팀 없는 출처는 무시한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDamageSenseTest, "VB.Unit.EnemyAI.DamageSense",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDamageSenseTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AVBAIController* Controller = World->SpawnActor<AVBAIController>();
	APawn* Body = World->SpawnActor<APawn>();
	// 팀을 아는 대역: AI controller 는 team id 를 답하고, 일반 액터는 team interface 가 없다(Neutral).
	AAIController* Hostile = World->SpawnActor<AAIController>();
	AAIController* Friendly = World->SpawnActor<AAIController>();
	AActor* Neutral = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("pawn"), Body) || !TestNotNull(TEXT("hostile"), Hostile)
		|| !TestNotNull(TEXT("friendly"), Friendly) || !TestNotNull(TEXT("neutral"), Neutral))
	{
		return false;
	}

	Controller->Possess(Body);
	Controller->SetGenericTeamId(FGenericTeamId(1));
	Hostile->SetGenericTeamId(FGenericTeamId(0));
	Friendly->SetGenericTeamId(FGenericTeamId(1));

	TStrongObjectPtr<UBlackboardData> Data(NewObject<UBlackboardData>(Controller));
	FBlackboardEntry& Entry = Data->Keys.AddDefaulted_GetRef();
	Entry.EntryName = TEXT("TargetActor");
	Entry.KeyType = NewObject<UBlackboardKeyType_Object>(Data.Get());
	UBlackboardComponent* BB = nullptr;
	if (!TestTrue(TEXT("blackboard initialised"), Controller->UseBlackboard(Data.Get(), BB) && BB))
	{
		return false;
	}

	auto Target = [BB]() { return BB->GetValueAsObject(TEXT("TargetActor")); };

	// damage sense 도 목격만큼 오래 기억한다.
	if (!TestNotNull(TEXT("damage sense config"), Controller->DamageConfig.Get()))
	{
		return false;
	}

	TestEqual(TEXT("damage max age = SightMaxAge"), Controller->DamageConfig->GetMaxAge(), GetDefault<UVBEnemyConfig>()->SightMaxAge);

	Controller->OnTargetPerceptionInfoUpdated(DamageUpdate(Friendly, true));
	TestNull(TEXT("friendly hit: no target"), Target());
	Controller->OnTargetPerceptionInfoUpdated(DamageUpdate(Neutral, true));
	TestNull(TEXT("team-less hit: no target"), Target());

	Controller->OnTargetPerceptionInfoUpdated(DamageUpdate(Hostile, true));
	TestTrue(TEXT("hostile hit from outside sight: target set"), Target() == Hostile);

	// 다른 감각 없이 피격이 만료되면 바로 지우지 않고 보통의 잃음 기억으로 간다.
	Controller->OnTargetPerceptionInfoUpdated(DamageUpdate(Hostile, false));
	TestTrue(TEXT("hit expired: target kept"), Target() == Hostile);
	TestTrue(TEXT("hit expired: forget is pending"), FVBPerceptionMemoryTestAccess::IsForgetPending(*Controller));
	return true;
}

// 피격은 위 handler 만이 아니라 실제 perception system 을 거쳐 controller 에 닿아야 한다.
//   BP_VBAIController 의 SensesConfig 는 시야뿐이라 listener 가 시야만으로 등록되고 OnPossess 가 damage sense 를 뒤에 더한다 - 갱신이 없으면 피격을 못 듣는다.
//   native controller 에 같은 상태(등록 listener 에서 damage 를 빼고 SensesConfig 에서도 제거)를 만든 뒤 빙의, 피격 보고, perception tick 한 번.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDamageSenseDeliveryTest, "VB.Unit.EnemyAI.DamageSenseDelivery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDamageSenseDeliveryTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AVBAIController* Controller = World->SpawnActor<AVBAIController>();
	APawn* Body = World->SpawnActor<APawn>();
	AActor* Attacker = World->SpawnActor<AActor>();
	UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(World);
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("pawn"), Body) || !TestNotNull(TEXT("attacker"), Attacker)
		|| !TestNotNull(TEXT("perception system"), Perception))
	{
		return false;
	}

	UAIPerceptionComponent* Listener = Controller->GetPerceptionComponent();
	if (!TestNotNull(TEXT("perception component"), Listener))
	{
		return false;
	}

	// BP 상태: damage sense 없이 등록되고 SensesConfig 에도 damage 항목이 없다.
	const FAISenseID DamageId = UAISense::GetSenseID<UAISense_Damage>();
	Listener->UpdatePerceptionAllowList(DamageId, false);
	FArrayProperty* SensesProperty = FindFProperty<FArrayProperty>(UAIPerceptionComponent::StaticClass(), TEXT("SensesConfig"));
	const FObjectPropertyBase* SenseEntry = SensesProperty ? CastField<FObjectPropertyBase>(SensesProperty->Inner) : nullptr;
	if (!TestNotNull(TEXT("SensesConfig property"), SenseEntry))
	{
		return false;
	}
	// 거르기만이 아니라 항목이 없어야 한다 - ConfigureSense 는 처음 보는 config 일 때만 채널을 다시 받는다.
	FScriptArrayHelper Senses(SensesProperty, SensesProperty->ContainerPtrToValuePtr<void>(Listener));
	for (int32 Index = Senses.Num() - 1; Index >= 0; --Index)
	{
		const UObject* Config = SenseEntry->GetObjectPropertyValue(Senses.GetRawPtr(Index));
		if (Config && Config->IsA<UAISenseConfig_Damage>())
		{
			Senses.RemoveValues(Index);
		}
	}

	TestNull(TEXT("setup: no damage config left"), Listener->GetSenseConfig(DamageId));

	Controller->Possess(Body);
	UAISense_Damage::ReportDamageEvent(World, Body, Attacker, 10.0f, Attacker->GetActorLocation(), Body->GetActorLocation());
	Perception->Tick(0.1f);

	FActorPerceptionBlueprintInfo Info;
	const bool bKnown = Listener->GetActorsPerception(Attacker, Info);
	const bool bHeard = bKnown && Info.LastSensedStimuli.ContainsByPredicate([DamageId](const FAIStimulus& Stimulus)
	{
		return Stimulus.Type == DamageId && Stimulus.WasSuccessfullySensed();
	});
	TestTrue(TEXT("a hit reported after possession reaches the listener"), bHeard);
	return true;
}
#endif
