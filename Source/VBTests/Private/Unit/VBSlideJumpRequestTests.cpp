// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBPawnExtensionComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBSlideJumpRequest.h"
#include "Character/VBSlideMovementComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// 값으로서의 slide-jump 요청 - 각 줄이 입력, 기대 동작, 끝 상태를 고정한다.
// 몸도 world 도 없다 - 몸 연결은 따로 시험한다.
namespace VBSlideJumpRequestTest
{
	using FRequest = FVBSlideJumpRequest;

	// Facts - 땅 위, 선 자세, movement 있음, 아무것도 안 누름. 그 자체로는 아무것도 바꾸지 않는 사실들이다.
	FRequest::FTickInput Facts(double Now)
	{
		FRequest::FTickInput In;
		In.Now = Now;
		In.bSceneAllowed = true;
		In.bHasMovement = true;
		In.bMovingOnGround = true;
		return In;
	}

	bool Expect(FAutomationTestBase& Test, const TCHAR* Row, const FRequest::FActions& Actual, bool bClearHeld, bool bStop, bool bJump)
	{
		const bool bA = Test.TestEqual(FString::Printf(TEXT("%s: clear held"), Row), Actual.bClearHeld, bClearHeld);
		const bool bB = Test.TestEqual(FString::Printf(TEXT("%s: stop jumping"), Row), Actual.bStopJumping, bStop);
		const bool bC = Test.TestEqual(FString::Printf(TEXT("%s: jump"), Row), Actual.bJump, bJump);
		return bA && bB && bC;
	}

	// Awaiting - 슬라이드에서 점프를 냈고 movement update 가 그 점프를 쓰기를 기다리는 요청.
	FRequest Awaiting(double Now)
	{
		FRequest R;
		R.PressOutOfSlide(Now, 0.25);
		R.Tick(Facts(Now)); // standing -> jump issued, awaiting movement
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSlideJumpRequestTransitionTest, "VB.Unit.Character.SlideJumpRequest.Transitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSlideJumpRequestTransitionTest::RunTest(const FString& Parameters)
{
	using namespace VBSlideJumpRequestTest;

	// 대기 상태 - 할 일이 없고, 떼기는 몸 자신의 StopJumping 이다.
	{
		FRequest R;
		Expect(*this, TEXT("idle tick"), R.Tick(Facts(1.0)), false, false, false);
		TestFalse(TEXT("idle release is not held"), R.Release());
		TestTrue(TEXT("idle"), R.IsIdle());
	}
	// 3b: 아직 웅크리면 계속 기다린다. 3c: 일어서면 점프를 내고 movement 를 기다리기 시작한다.
	{
		FRequest R;
		R.PressOutOfSlide(1.0, 0.25);
		FRequest::FTickInput Crouched = Facts(1.1);
		Crouched.bCrouched = true;
		Expect(*this, TEXT("3b pending while crouched"), R.Tick(Crouched), false, false, false);
		TestFalse(TEXT("3b still pending"), R.IsIdle());
		Expect(*this, TEXT("3c stood up"), R.Tick(Facts(1.15)), false, false, true);
		TestFalse(TEXT("3c now awaiting movement"), R.IsIdle());
		TestTrue(TEXT("3c awaiting blocks a jump only inside a scene"), R.BlocksJump(false) && !R.BlocksJump(true));
	}
	// 2: 점프가 눌렸지만 아직 안 쓰였으면 기다림을 유지하고, movement update 가 쓰면 끝난다.
	{
		FRequest R = Awaiting(2.0);
		FRequest::FTickInput Pressed = Facts(2.01);
		Pressed.bPressedJump = true;
		Expect(*this, TEXT("2 jump pressed, not yet used"), R.Tick(Pressed), false, false, false);
		TestFalse(TEXT("2 still awaiting"), R.IsIdle());
		Pressed.bWasJumping = true;
		Expect(*this, TEXT("2 movement used the jump"), R.Tick(Pressed), false, false, false);
		TestTrue(TEXT("2 idle after use"), R.IsIdle());
	}
	// 2 + 기다리는 중 떼기: 점프가 쓰이면 StopJumping 한다 - 키를 다시 누르고 있으면 하지 않는다.
	{
		FRequest R = Awaiting(3.0);
		TestTrue(TEXT("release while awaiting is held"), R.Release());
		FRequest::FTickInput Used = Facts(3.01);
		Used.bWasJumping = true;
		Expect(*this, TEXT("2 used, released"), R.Tick(Used), false, true, false);
		FRequest Again = Awaiting(3.0);
		Again.Release();
		Used.bJumpHeld = true;
		Expect(*this, TEXT("2 used, released then held again"), Again.Tick(Used), false, false, false);
	}
	// 3a: 창은 마감 시각에 정확히(>=) 끝나고, 땅을 떠나도 끝난다.
	{
		FRequest R;
		R.PressOutOfSlide(4.0, 0.25);
		R.Release();
		FRequest::FTickInput Crouched = Facts(4.25);
		Crouched.bCrouched = true;
		Expect(*this, TEXT("3a deadline reached exactly"), R.Tick(Crouched), false, true, false);
		TestTrue(TEXT("3a idle"), R.IsIdle());
		FRequest Air;
		Air.PressOutOfSlide(4.0, 0.25);
		FRequest::FTickInput Falling = Facts(4.1);
		Falling.bMovingOnGround = false;
		Expect(*this, TEXT("3a left the ground, no release"), Air.Tick(Falling), false, false, false);
		TestTrue(TEXT("3a idle after leaving the ground"), Air.IsIdle());
	}
	// 새로 누르면 앞선 떼기 요청이 지워진다.
	{
		FRequest R;
		R.PressOutOfSlide(5.0, 0.25);
		R.Release();
		R.OnPress();
		FRequest::FTickInput Crouched = Facts(5.3);
		Crouched.bCrouched = true;
		Expect(*this, TEXT("re-press then expiry"), R.Tick(Crouched), false, false, false);
	}
	// 1: 장면은 pending 이든 awaiting 이든 모두 버리고 누른 키도 지운다.
	{
		FRequest Pending;
		Pending.PressOutOfSlide(6.0, 0.25);
		FRequest::FTickInput Scene = Facts(6.1);
		Scene.bSceneAllowed = false;
		Expect(*this, TEXT("1 scene while pending"), Pending.Tick(Scene), true, true, false);
		TestTrue(TEXT("1 idle after scene (pending)"), Pending.IsIdle());
		FRequest Waiting = Awaiting(6.0);
		Waiting.Release();
		Expect(*this, TEXT("1 scene while awaiting"), Waiting.Tick(Scene), true, true, false);
		TestTrue(TEXT("1 idle after scene (awaiting)"), Waiting.IsIdle());
		TestFalse(TEXT("1 release after scene is the body's own"), Waiting.Release());
		// 1의 끝 상태 - 떼기 요청도 사라져 나중 기다림이 만료돼도 멈추지 않는다.
		// 몸은 늘 OnPress 를 먼저 불러 이를 가리므로 이 줄이 값 자체의 약속을 고정한다.
		Waiting.PressOutOfSlide(6.2, 0.25);
		FRequest::FTickInput Expired = Facts(6.5);
		Expired.bCrouched = true;
		Expect(*this, TEXT("1 scene dropped the release request"), Waiting.Tick(Expired), false, false, false);
	}
	// 한 tick 의 2 와 3c: 옛 기다림이 먼저 소비되고 새 기다림은 이 tick 을 살아남는다 (Jump 전에 StopJumping).
	{
		FRequest R = Awaiting(7.0);
		R.Release();
		R.PressOutOfSlide(7.05, 0.25);
		FRequest::FTickInput Both = Facts(7.1);
		Both.bWasJumping = true;
		Expect(*this, TEXT("2 + 3c same tick"), R.Tick(Both), false, true, true);
		TestFalse(TEXT("the new wait is not consumed in the tick that made it"), R.IsIdle());
	}

	return true;
}
// 몸이 실제 경로로 요청을 적용한다 - 진짜 슬라이드에서 점프를 눌러 pending, 일어서서 awaiting 이 된 뒤
// 장면(처형, 죽음)이 그것을 버린다. 장면 안에선 점프가 없고 장면이 끝나면 평소 점프가 돌아온다.
// 누른 키 지우기는 air mantle 로만 보이므로 여기서 다루지 않는다.
namespace VBSlideJumpBodyTest
{
	constexpr float DeltaTime = 1.0f / 60.0f;

	// TickBody - 공개 AActor::Tick 으로 몸을 한 번 돌린다 (virtual 이라 protected 인 AVBCharacter::Tick 이 돈다).
	void TickBody(AActor& Body) { Body.Tick(DeltaTime); }

	// PendingFromRealSlide - 진짜 슬라이드를 시작하고 점프를 누른다. 그 상태에 못 가면 이유와 함께 false.
	bool PendingFromRealSlide(FAutomationTestBase& Test, AVBCharacter& Body, UVBSlideMovementComponent& Move)
	{
		Move.SetMovementMode(MOVE_Walking);
		Move.Velocity = FVector(Move.SlideStartSpeed + 200.0, 0.0, 0.0);
		Body.Crouch();
		Move.RequestSlide();
		Move.UpdateCharacterStateBeforeMovement(DeltaTime);
		if (!Test.TestTrue(TEXT("fixture: the body is crouched"), Body.bIsCrouched)
			|| !Test.TestTrue(TEXT("fixture: the slide really started"), Move.IsSliding()))
		{
			return false;
		}

		Body.JumpInputPressed();
		return Test.TestFalse(TEXT("fixture: the press cancelled the slide"), Move.IsSliding())
			&& Test.TestTrue(TEXT("fixture: still crouched after the press (pending, no jump yet)"), Body.bIsCrouched
				&& !Body.bPressedJump);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSlideJumpBodySceneTest, "VB.Unit.Character.SlideJumpRequest.BodySceneGuard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSlideJumpBodySceneTest::RunTest(const FString& Parameters)
{
	using namespace VBSlideJumpBodyTest;
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	for (const FGameplayTag Scene : { VBGameplayTags::State_Combat_Finishing.GetTag(), VBGameplayTags::State_Dead.GetTag() })
	{
		for (const bool bAwaiting : { false, true })
		{
			const FString Row = FString::Printf(TEXT("%s, %s"), *Scene.ToString(), bAwaiting ? TEXT("awaiting movement") : TEXT("pending"));
			AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
			UVBSlideMovementComponent* Move = Body ? Cast<UVBSlideMovementComponent>(Body->GetCharacterMovement()) : nullptr;
			if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("slide movement"), Move))
			{
				return false;
			}

			UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Body);
			Body->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			// 몸의 죽음 담당은 health set 없는 ASC 를 거부하므로(ensure) PlayerState ASC 가 늘 가진 set 을 준다.
			ASC->AddAttributeSetSubobject(NewObject<UVBHealthAttributeSet>(Body));
			UVBPawnExtensionComponent::FindFor(Body)->InitializeAbilitySystem(ASC, Body);
			if (!PendingFromRealSlide(*this, *Body, *Move))
			{
				return false;
			}

			if (bAwaiting)
			{
				Move->UpdateCharacterStateBeforeMovement(DeltaTime); // stands up (no slide, no crouch wish)
				TickBody(*Body); // the request issues the jump
				if (!TestTrue(FString::Printf(TEXT("%s: fixture stood up and the jump was issued"), *Row), !Body->bIsCrouched
					&& Body->bPressedJump))
				{
					return false;
				}
			}

			ASC->AddLooseGameplayTag(Scene);
			if (bAwaiting)
			{
				TestFalse(FString::Printf(TEXT("%s: no new jump inside the scene"), *Row), Body->CanJump());
				Body->CheckJumpInput(DeltaTime);
				TestEqual(FString::Printf(TEXT("%s: movement does not jump inside the scene"), *Row),
					Body->GetCharacterMovement()->Velocity.Z, 0.0);
			}

			TickBody(*Body);
			TestFalse(FString::Printf(TEXT("%s: the scene dropped the jump"), *Row), Body->bPressedJump);
			ASC->RemoveLooseGameplayTag(Scene);
			Move->UpdateCharacterStateBeforeMovement(DeltaTime);
			TestTrue(FString::Printf(TEXT("%s: normal jumping is back after the scene"), *Row), Body->CanJump());
			Body->Destroy();
		}
	}

	return true;
}
#endif
