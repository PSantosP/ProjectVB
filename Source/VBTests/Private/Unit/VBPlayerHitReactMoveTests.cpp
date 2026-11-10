#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBHeroComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 플레이어 피격 반응은 몸을 붙잡는다 - State.Combat.HitReacting 동안 이동 입력을 무시한다
// 반대 경우: 태그가 없으면 이동이 막히지 않는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayerHitReactMoveTest, "VB.Unit.Player.HitReactBlocksMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayerHitReactMoveTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("No ability system: nothing to block"), UVBHeroComponent::ShouldBlockMoveForHitReact(nullptr));

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
	Pawn->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Pawn, Pawn);

	TestFalse(TEXT("Not hit-reacting: movement works"), UVBHeroComponent::ShouldBlockMoveForHitReact(ASC));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	TestTrue(TEXT("Hit-reacting: movement is held"), UVBHeroComponent::ShouldBlockMoveForHitReact(ASC));
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	TestFalse(TEXT("Hit react over: movement works again"), UVBHeroComponent::ShouldBlockMoveForHitReact(ASC));
	return true;
}
#endif
