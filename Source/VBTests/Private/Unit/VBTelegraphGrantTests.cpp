// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBTelegraphGrant.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 예고는 붙인 ASC 에서 자기 몫만 뗀다. 대조: Release 의 granted 검사를 빼면 release without a grant 줄이,
//   기억한 ASC 대신 다른 ASC 를 쓰면 stays on the ASC it was granted to 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTelegraphGrantTest, "VB.Unit.Combat.TelegraphGrant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTelegraphGrantTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	const auto GiveASC = [World]()
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Owner, Owner);
		// 큐 내용은 시험 대상이 아니다. 예고 연출 자산 없이 도는 경고를 막는다.
		ASC->bSuppressGameplayCues = true;
		return ASC;
	};

	UAbilitySystemComponent* ASC = GiveASC();
	UAbilitySystemComponent* OtherASC = GiveASC();
	const FGameplayTag Tag = VBGameplayTags::State_Combat_Telegraphing;
	const FGameplayTag Cue = VBGameplayTags::GameplayCue_Combat_Telegraph;

	FVBTelegraphGrant Telegraph;
	Telegraph.Release();
	TestEqual(TEXT("release without a grant takes nothing"), ASC->GetTagCount(Tag), 0);

	Telegraph.Grant(*ASC, ASC->GetOwner(), nullptr, 1.5f, FGameplayTag());
	TestTrue(TEXT("granted"), Telegraph.IsGranted());
	TestEqual(TEXT("grant adds one tag"), ASC->GetTagCount(Tag), 1);
	TestTrue(TEXT("grant raises the cue"), ASC->IsGameplayCueActive(Cue));
	TestEqual(TEXT("stays on the ASC it was granted to"), OtherASC->GetTagCount(Tag), 0);

	Telegraph.Release();
	TestFalse(TEXT("released"), Telegraph.IsGranted());
	TestEqual(TEXT("release takes the tag"), ASC->GetTagCount(Tag), 0);
	TestFalse(TEXT("release drops the cue"), ASC->IsGameplayCueActive(Cue));
	Telegraph.Release();
	TestEqual(TEXT("second release takes nothing"), ASC->GetTagCount(Tag), 0);

	ASC->AddLooseGameplayTag(Tag);
	Telegraph.Grant(*ASC, ASC->GetOwner(), nullptr, 1.5f, FGameplayTag());
	Telegraph.Release();
	TestEqual(TEXT("someone else's tag stays"), ASC->GetTagCount(Tag), 1);
	ASC->RemoveLooseGameplayTag(Tag);
	return true;
}
#endif
