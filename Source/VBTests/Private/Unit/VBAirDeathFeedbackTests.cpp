#include "Unit/VBAirDeathFeedbackTestProbe.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyDeathPresentationComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Player/VBPlayerState.h"
#include "TimerManager.h"
#include "UObject/Package.h"

AVBAirDeathFeedbackTestEnemy::AVBAirDeathFeedbackTestEnemy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	// 시험 월드에는 물리 메시가 없다 - 캡슐 따라가기를 끄고 시체 전환의 나머지만 실제로 돌린다.
	UVBEnemyConfig* TestConfig = CreateDefaultSubobject<UVBEnemyConfig>(TEXT("TestEnemyConfig"));
	TestConfig->bUseDrivenRagdoll = false;
	EnemyConfig = TestConfig;
}

void AVBAirDeathFeedbackTestEnemy::Die(const FVBDeathFacts& Facts)
{
	DeathPresentationComponent->MarkBodyReady();
	DeathPresentationComponent->CaptureAirDeath(Facts);
	DeathPresentationComponent->Present(Facts);
}

void AVBAirDeathFeedbackTestEnemy::HandleGameplayCue(UObject* Self, FGameplayTag Tag,
	EGameplayCueEvent::Type Event, const FGameplayCueParameters& Parameters)
{
	if (Tag == VBGameplayTags::GameplayCue_Combat_AirSlam && Event == EGameplayCueEvent::Executed)
	{
		++LandingCues;
		LastLanding = Parameters;
	}
}

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirDeathFeedbackTest, "VB.Unit.Health.Enemy.AirLandingRecipient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBAirDeathFeedbackTest::RunTest(const FString& Parameters)
{
	// 이 fixture 에는 공중 반응 GA 가 없어 공중 사망마다 제품이 Error 를 한 번 남긴다 - 아래 공중 사망 여덟 번과 정확히 같아야 한다.
	AddExpectedMessagePlain(TEXT("Airborne lethal finish requires the victim's air-slam reaction"), ELogVerbosity::Error,
		EAutomationExpectedMessageFlags::Contains, 8);

	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	UWorld* Foreign = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	ON_SCOPE_EXIT
	{
		if (Foreign) { Foreign->DestroyWorld(false); }
		if (World) { World->DestroyWorld(false); }
	};
	if (!TestNotNull(TEXT("landing world"), World) || !TestNotNull(TEXT("foreign world"), Foreign)) { return false; }
	World->GetOutermost()->SetFlags(RF_Transient);
	Foreign->GetOutermost()->SetFlags(RF_Transient);
	AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
	APawn* Original = World->SpawnActor<APawn>();
	APawn* Replacement = World->SpawnActor<APawn>();
	APawn* OtherWorldPawn = Foreign->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("state"), State) || !TestNotNull(TEXT("original pawn"), Original)
		|| !TestNotNull(TEXT("replacement pawn"), Replacement) || !TestNotNull(TEXT("foreign pawn"), OtherWorldPawn)) { return false; }
	UAbilitySystemComponent* AttackerASC = State->GetAbilitySystemComponent();
	AttackerASC->InitAbilityActorInfo(State, Original);

	// 죽음은 한 몸에 한 번이다 - 경우마다 새 피해자를 쓴다.
	auto NewVictim = [this, World]() -> AVBAirDeathFeedbackTestEnemy*
	{
		AVBAirDeathFeedbackTestEnemy* Victim = World->SpawnActor<AVBAirDeathFeedbackTestEnemy>();
		if (TestNotNull(TEXT("victim"), Victim))
		{
			Victim->GetAbilitySystemComponent()->InitAbilityActorInfo(Victim, Victim);
		}

		return Victim;
	};

	FVBDeathFacts Facts;
	Facts.Instigator = State;
	Facts.bAirborneAtDeath = true;
	Facts.Motion.InitialVelocity = FVector(0.0, 0.0, -1000.0);
	Facts.Motion.MaxAirSeconds = 2.0f;
	Facts.Motion.GroundImpactCue = VBGameplayTags::GameplayCue_Combat_AirSlam;
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.ImpactPoint = FVector(110.0, 210.0, 10.0);
	Hit.ImpactNormal = FVector::UpVector;

	AVBAirDeathFeedbackTestEnemy* Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	AttackerASC->InitAbilityActorInfo(State, Replacement);
	Victim->Land(Hit);
	TestEqual(TEXT("actual landing emits one cue"), Victim->LandingCues, 1);
	TestEqual(TEXT("cue keeps original pawn after avatar changes"), Victim->LastLanding.GetInstigator(), static_cast<AActor*>(Original));
	TestEqual(TEXT("context agrees with explicit recipient"), Victim->LastLanding.EffectContext.GetInstigator(), static_cast<AActor*>(Original));
	TestEqual(TEXT("context preserves victim effect causer"), Victim->LastLanding.EffectContext.GetEffectCauser(), static_cast<AActor*>(Victim));
	TestEqual(TEXT("consumer receives original attacker"), UVBGC_CombatFeedback::ResolveScreenRecipient(Victim,
		Victim->LastLanding, EVBFeedbackRecipient::Instigator), Original);
	const FHitResult* Contact = Victim->LastLanding.EffectContext.GetHitResult();
	TestTrue(TEXT("real ground contact retained"), Contact && Contact->ImpactPoint.Equals(Hit.ImpactPoint));
	Victim->Land(Hit);
	TestEqual(TEXT("duplicate landing cannot replay"), Victim->LandingCues, 1);

	Facts.Instigator = Original;
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	Victim->Land(Hit);
	TestEqual(TEXT("direct pawn retained"), Victim->LastLanding.GetInstigator(), static_cast<AActor*>(Original));

	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	Original->Destroy();
	Victim->Land(Hit);
	TestEqual(TEXT("destroyed attacker still lands once"), Victim->LandingCues, 1);
	TestNull(TEXT("destroyed attacker does not fall back to victim context"), Victim->LastLanding.GetInstigator());
	TestNull(TEXT("destroyed attacker has no screen recipient"), UVBGC_CombatFeedback::ResolveScreenRecipient(Victim,
		Victim->LastLanding, EVBFeedbackRecipient::Instigator));

	Facts.Instigator = nullptr;
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	Victim->Land(Hit);
	TestEqual(TEXT("missing attacker still lands once"), Victim->LandingCues, 1);
	TestNull(TEXT("missing attacker keeps cue instigator empty"), Victim->LastLanding.GetInstigator());

	Facts.Instigator = OtherWorldPawn;
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	Victim->Land(Hit);
	TestEqual(TEXT("foreign-world attacker still lands once"), Victim->LandingCues, 1);
	TestNull(TEXT("foreign-world attacker rejected at capture"), Victim->LastLanding.GetInstigator());

	Facts.Instigator = Replacement;
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	FHitResult Miss;
	Victim->Land(Miss);
	Victim->Land(Hit);
	TestEqual(TEXT("nonblocking callback consumes without delayed replay"), Victim->LandingCues, 0);

	// 공중으로 기록한 뒤 땅 사망이 오면 이전 공격자와 착지 대기가 남지 않는다.
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	UVBEnemyDeathPresentationComponent* const Presentation = UVBEnemyDeathPresentationComponent::FindFor(Victim);
	if (!TestNotNull(TEXT("death presentation"), Presentation)) { return false; }
	Presentation->CaptureAirDeath(Facts);
	FVBDeathFacts Ground = Facts;
	Ground.bAirborneAtDeath = false;
	Victim->Die(Ground);
	Victim->Land(Hit);
	TestEqual(TEXT("ground death waits for no landing"), Victim->LandingCues, 0);

	// 착지 없이 공중 상한이 지나면 시체가 되고, 늦은 착지는 큐를 내지 않는다.
	Victim = NewVictim();
	if (!Victim) { return false; }
	Facts.DeadActor = Victim;
	Victim->Die(Facts);
	FTimerManager& Timers = World->GetTimerManager();
	++GFrameCounter;
	Timers.Tick(0.0f);
	++GFrameCounter;
	Timers.Tick(Facts.Motion.MaxAirSeconds + 0.5f);
	Victim->Land(Hit);
	TestEqual(TEXT("air timeout prevents late cue"), Victim->LandingCues, 0);
	return true;
}
#endif
