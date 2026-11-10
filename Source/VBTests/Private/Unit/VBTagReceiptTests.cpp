// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBLooseTagHold.h"
#include "AbilitySystemComponent.h"
#include "VBLoadingScreenTestProbe.h" // AVBLoadingScreenTestAbilityPawn - a pawn with a real ASC
#include "VBPlayerBodyTestFixture.h"
#include <type_traits>

// 영수증을 복사할 수 있으면 두 사본이 같은 몫을 각각 뗀다 - 막혀 있어야 한다.
static_assert(!std::is_copy_constructible_v<FVBLooseTagHold> && !std::is_copy_assignable_v<FVBLooseTagHold>,
	"FVBLooseTagHold must not be copyable");

// 태그는 자식 몫이 섞이지 않는 State.Combat.LockedOn 하나(합계 = 직접 몫).
//   음성 대조: Release 가 기억한 ASC 에서 떼는 줄을 빼면 'A 가 0' 이, Hold 의 '이미 쥐었으면 그대로' 를 빼면 '또 쥐면 그대로' 가 실패해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLooseTagHoldTest, "VB.Unit.AbilitySystem.LooseTagHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLooseTagHoldTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	TStrongObjectPtr<UPackage> Package(World->GetPackage());
	World->InitializeActorsForPlay(FURL());
	AVBLoadingScreenTestAbilityPawn* PawnA = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>();
	AVBLoadingScreenTestAbilityPawn* PawnB = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>();
	if (TestNotNull(TEXT("pawn A"), PawnA) && TestNotNull(TEXT("pawn B"), PawnB))
	{
		UAbilitySystemComponent& A = *PawnA->AbilitySystem;
		UAbilitySystemComponent& B = *PawnB->AbilitySystem;
		A.InitAbilityActorInfo(PawnA, PawnA);
		B.InitAbilityActorInfo(PawnB, PawnB);
		const FGameplayTagContainer Tags(VBGameplayTags::State_Combat_LockedOn);

		FVBLooseTagHold Hold;
		TestFalse(TEXT("no ASC: nothing held"), Hold.Hold(nullptr, Tags, EGameplayTagReplicationState::CountToOwner) || Hold.IsHeld());
		TestTrue(TEXT("held on A"), Hold.Hold(&A, Tags, EGameplayTagReplicationState::CountToOwner));
		TestEqual(TEXT("A carries one share"), A.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 1);
		TestFalse(TEXT("holding again does nothing"), Hold.Hold(&A, Tags, EGameplayTagReplicationState::CountToOwner));
		TestEqual(TEXT("A still one share"), A.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 1);
		TestTrue(TEXT("the receipt names A"), Hold.GetASC() == &A);

		// B 에 있는 다른 주인의 몫은 우리 release 뒤에도 남아야 한다 - 영수증은 가져간 곳에서 가져간 만큼만 돌려준다
		B.AddLooseGameplayTag(VBGameplayTags::State_Combat_LockedOn, 1, EGameplayTagReplicationState::CountToOwner);
		Hold.Release();
		TestEqual(TEXT("released: A back to 0"), A.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 0);
		TestEqual(TEXT("released: B's other share untouched"), B.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 1);
		Hold.Release();
		TestEqual(TEXT("releasing twice changes nothing"), A.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 0);
		TestFalse(TEXT("nothing held after release"), Hold.IsHeld());
	}

	World->DestroyWorld(false);
	Package->SetDirtyFlag(false);
	return true;
}

namespace VBTagReceiptTest
{
	// 제품 경로(서버, facing 없는 parry lock)로 몸을 fixture 표적에 잠그고 몫 수를 돌려준다
	int32 Lock(const VBPlayerBodyTest::FFixture& F, AVBCharacter* Body)
	{
		UVBTargetLockComponent::FindFor(Body)->FaceAndLockParryTarget(F.Target, /*bLock=*/true, /*bFace=*/false);
		return F.ASC->GetTagCount(VBGameplayTags::State_Combat_LockedOn);
	}
}

// 보통 교체(회귀)와 순서 견딤 두 줄(설계 3절, 검토 P1-1). 보통 교체는 지금 코드에서도 통과한다 - C1 이 몸을 놓기 전에 떼기 때문이다.
//   순서 견딤 줄은 고치기 전 코드에서 실패해야 한다: 정리보다 먼저 몸이 PlayerState 를 잃으면 옛 떼기가 ASC 를 못 찾는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockOnTagReceiptTest, "VB.Unit.Character.LockOnTagReceipt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLockOnTagReceiptTest::RunTest(const FString& Parameters)
{
	VBPlayerBodyTest::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.A);
	if (!TestEqual(TEXT("normal swap: A locked, one LockedOn share"), VBTagReceiptTest::Lock(F, F.A), 1))
	{
		return false;
	}

	F.PC->Possess(F.B);
	F.A->Destroy();
	TestEqual(TEXT("normal swap: after A is released and destroyed, no share left"),
		F.ASC->GetTagCount(VBGameplayTags::State_Combat_LockedOn), 0);

	AVBCharacter* C = F.World->SpawnActor<AVBCharacter>(FVector(0.0, -200.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("body C"), C))
	{
		return false;
	}

	F.PC->Possess(C);
	if (!TestEqual(TEXT("ordering: C locked, one LockedOn share"), VBTagReceiptTest::Lock(F, C), 1))
	{
		return false;
	}
	// 보통 빙의가 샌다는 주장이 아니다 - 여기서는 정리가 돌기 전에 몸이 PlayerState 를 잃는다
	C->SetPlayerState(nullptr);
	F.PC->Possess(F.B);
	C->Destroy();
	TestEqual(TEXT("ordering: PlayerState lost before cleanup, the share is still given back"),
		F.ASC->GetTagCount(VBGameplayTags::State_Combat_LockedOn), 0);
	return true;
}
#endif
