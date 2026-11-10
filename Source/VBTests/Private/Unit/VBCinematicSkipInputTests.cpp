// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Narrative/Cinematic/VBCinematicLeaseBase.h"
#include "Narrative/Cinematic/VBCinematicSkipHold.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace VBCinematicSkipTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	class FGuardLease final : public FVBCinematicLeaseBase
	{
	public:
		explicit FGuardLease(APlayerController& PC) : FVBCinematicLeaseBase(&PC, &PC, nullptr)
		{
			PushInputGuard();
		}
		virtual void Finish(bool) override
		{
			RemoveInputGuard();
		}
		UEnhancedInputComponent* Guard() const
		{
			return InputGuard.Get();
		}
		TSharedPtr<FVBCinematicSkipHold> Hold() const
		{
			return SkipHold;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkipHoldTest, "VB.Unit.Cinematic.Skip.Hold",
	VBCinematicSkipTests::Flags)
bool FVBCinematicSkipHoldTest::RunTest(const FString& Parameters)
{
	FVBCinematicSkipHold Hold;
	TestFalse(TEXT("already-held input without a fresh press cannot skip"), Hold.Consume(2.0, 0.5f, true));
	Hold.Press(10.0);
	TestFalse(TEXT("short press does not skip"), Hold.Consume(10.49, 0.5f, true));
	Hold.Release();
	Hold.Press(10.5);
	TestFalse(TEXT("separate short taps do not accumulate"), Hold.Consume(10.8, 0.5f, true));
	Hold.Press(10.9);
	TestTrue(TEXT("repeat press does not restart the monotonic hold"), Hold.Consume(11.0, 0.5f, true));
	TestFalse(TEXT("one continuous hold is consumed once"), Hold.Consume(12.0, 0.5f, true));
	Hold.Release();
	Hold.Press(20.0);
	TestFalse(TEXT("focus flush or zero action cancels the hold"), Hold.Consume(21.0, 0.5f, false));
	TestFalse(TEXT("input recovery does not reuse the canceled press"), Hold.Consume(22.0, 0.5f, true));
	Hold.Press(30.0);
	TestFalse(TEXT("clock reversal cancels the hold"), Hold.Consume(29.0, 0.5f, true));
	Hold.Press(40.0);
	TestFalse(TEXT("invalid hold setting is refused"), Hold.Consume(41.0, -1.f, true));
	Hold.Press(50.0);
	TestFalse(TEXT("nonfinite time is refused"), Hold.Consume(std::numeric_limits<double>::infinity(), 0.5f, true));
	Hold.Press(60.0);
	TestTrue(TEXT("authored zero duration still needs a fresh press"), Hold.Consume(60.0, 0.f, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkipGuardTest, "VB.Unit.Cinematic.Skip.InputGuard",
	VBCinematicSkipTests::Flags)
bool FVBCinematicSkipGuardTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("isolated input world"), World))
	{
		return false;
	}

	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller"), PC))
	{
		World->DestroyWorld(false);
		return false;
	}

	{
		VBCinematicSkipTests::FGuardLease Lease(*PC);
		const TStrongObjectPtr<UInputAction> Action(NewObject<UInputAction>());
		UEnhancedInputComponent* Guard = Lease.Guard();
		TestFalse(TEXT("missing action refuses skip input"), Lease.BindSkipInput(nullptr));
		TestTrue(TEXT("bind existing interaction action"), Lease.BindSkipInput(Action.Get()));
		TestFalse(TEXT("bindings cannot duplicate"), Lease.BindSkipInput(Action.Get()));
		TestTrue(TEXT("all lower gameplay input remains blocked"), Guard->bBlockInput);
		TestEqual(TEXT("only press and two release paths are bound"), Guard->GetActionEventBindings().Num(), 3);
		TestEqual(TEXT("one action value is observed"), Guard->GetActionValueBindings().Num(), 1);
		const TSharedPtr<FVBCinematicSkipHold> Hold = Lease.Hold();
		FInputActionInstance Instance(Action.Get());
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Guard->GetActionEventBindings())
		{
			TestTrue(TEXT("every binding belongs to the interaction action"), Binding->GetAction() == Action.Get());
			if (Binding->GetTriggerEvent() == ETriggerEvent::Started)
			{
				Binding->Execute(Instance);
			}
		}

		TestTrue(TEXT("real guard press binding arms the hold"), Hold->IsPressed());
		TestFalse(TEXT("a zero raw action cannot complete a captured press"), Lease.ConsumeSkipRequest(0.f));
		TestFalse(TEXT("zero action clears the captured press"), Hold->IsPressed());
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Guard->GetActionEventBindings())
		{
			if (Binding->GetTriggerEvent() != ETriggerEvent::Started)
			{
				Hold->Press(10.0);
				Binding->Execute(Instance);
				TestFalse(TEXT("completed and canceled bindings clear elapsed time"), Hold->IsPressed());
			}
		}

		Hold->Press(20.0);
		Lease.Finish(false);
		TestNull(TEXT("abort removes the input guard owner"), Lease.Guard());
		TestFalse(TEXT("abort unregisters the owned component"), Guard->IsRegistered());
		TestFalse(TEXT("detached event state is canceled on removal"), Hold->IsPressed());
		TestFalse(TEXT("after cleanup no request is available"), Lease.ConsumeSkipRequest(0.f));
	}

	World->DestroyWorld(false);
	return true;
}
#endif
