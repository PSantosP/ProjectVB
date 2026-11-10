#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/WorldSettings.h"
#include "Player/VBPlayerController.h"
#include "Player/VBWeaponWheelComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "UI/VBUIManagerSubsystem.h"
#include "AbilitySystem/VBGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponWheelInputTest, "VB.PIE.WeaponWheel.InputLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBWeaponWheelInputTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Cannot set target window - GEngine is not valid."), EAutomationExpectedErrorFlags::Contains, 1);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	AVBPlayerController* Player = World ? Cast<AVBPlayerController>(World->GetFirstPlayerController()) : nullptr;
	AVBCharacter* Character = Player ? Cast<AVBCharacter>(Player->GetPawn()) : nullptr;
	if (!TestNotNull(TEXT("Requires running gameplay PIE"), Character))
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	UVBWeaponWheelComponent* Wheel = Player->GetWeaponWheel();
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Character);
	UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer());
	const UInputAction* Summon = LoadObject<UInputAction>(nullptr, TEXT("/Game/VB/Input/Actions/IA_WeaponSummon.IA_WeaponSummon"));
	if (!TestTrue(TEXT("Configured component and owned weapons"), Wheel && Wheel->WheelAction && Input && Summon && Weapons
		&& !Weapons->GetOwnedWeapons().IsEmpty()))
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	const TArray<FKey> WheelKeys = Input->QueryKeysMappedToAction(Wheel->WheelAction);
	const TArray<FKey> SummonKeys = Input->QueryKeysMappedToAction(Summon);
	if (!TestTrue(TEXT("Real keyboard mappings available"), !WheelKeys.IsEmpty() && !SummonKeys.IsEmpty()))
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	struct FRun
	{
		TWeakObjectPtr<AVBPlayerController> Player;
		FKey WheelKey;
		FKey SummonKey;
		int32 Stage = 0;
		double Next = 0;
		float OriginalDilation = 1.f;
		bool bOriginallySummoned = false;
		FRotator Camera;
		EVBWeaponType Selected = EVBWeaponType::None;
		EVBWeaponType BeforeCancel = EVBWeaponType::None;
	};
	TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->Player = Player;
	Run->WheelKey = WheelKeys[0];
	Run->SummonKey = SummonKeys[0];
	Run->OriginalDilation = World->GetWorldSettings()->TimeDilation;
	Run->bOriginallySummoned = Weapons->IsWeaponSummoned();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		AVBPlayerController* Controller = Run->Player.Get();
		AVBCharacter* Pawn = Controller ? Cast<AVBCharacter>(Controller->GetPawn()) : nullptr;
		if (!Pawn) { AddError(TEXT("PIE pawn disappeared")); return true; }
		UVBWeaponWheelComponent* Component = Controller->GetWeaponWheel();
		UVBWeaponStateComponent* WeaponState = UVBWeaponStateComponent::FindFor(Pawn);
		const double Now = FPlatformTime::Seconds();
		if (Run->Stage == 4 && Now < Run->Next)
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 20.f, 1));
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, 0.f, 1));
		}

		if (Now < Run->Next)
		{
			return false;
		}

		auto Key = [Controller](FKey InputKey, EInputEvent Event)
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InputKey, Event, Event == IE_Pressed ? 1.f : 0.f));
		};
		switch (Run->Stage++)
		{
		case 0:
			Key(Run->SummonKey, IE_Pressed);
			Run->Next = Now + 0.08;
			break;
		case 1:
			Key(Run->SummonKey, IE_Released);
			Run->Next = Now + 0.25;
			break;
		case 2:
			TestFalse(TEXT("Tap never opens wheel"), Component->IsOpen());
			TestTrue(TEXT("Tap changes summon state once"), WeaponState->IsWeaponSummoned() != Run->bOriginallySummoned);
			Key(Run->WheelKey, IE_Pressed);
			Run->Next = Now + 0.5;
			break;
		case 3:
			TestTrue(TEXT("Hold opens wheel"), Component->IsOpen());
			TestEqual(TEXT("New hold starts at center"), Component->GetSelectedSlot(), INDEX_NONE);
			TestTrue(TEXT("Standalone slows time"), Controller->GetWorld()->GetWorldSettings()->TimeDilation < Run->OriginalDilation);
			Run->Camera = Controller->GetControlRotation();
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 40.f, 1));
			Run->Next = Now + 0.25;
			break;
		case 4:
			AddInfo(FString::Printf(TEXT("Wheel cursor=%s selected=%d"), *Component->GetCursor().ToString(), Component->GetSelectedSlot()));
			TestTrue(TEXT("Still held after multiple frames"), Component->IsOpen());
			TestTrue(TEXT("Wheel look does not rotate camera"), Controller->GetControlRotation().Equals(Run->Camera, 0.1f));
			if (Component->GetVisibleEntries().IsValidIndex(Component->GetSelectedSlot()))
			{
				Run->Selected = Component->GetVisibleEntries()[Component->GetSelectedSlot()].Weapon;
			}

			TestTrue(TEXT("Real mouse mapping selects sector"), Run->Selected != EVBWeaponType::None);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/WeaponWheel.png"), true, false);
			Key(Run->WheelKey, IE_Released);
			Run->Next = Now + 0.3;
			break;
		case 5:
			TestFalse(TEXT("Release closes wheel"), Component->IsOpen());
			TestEqual(TEXT("Release equips selected weapon"), WeaponState->GetCurrentWeaponType(), Run->Selected);
			TestEqual(TEXT("Release restores dilation"), Controller->GetWorld()->GetWorldSettings()->TimeDilation, Run->OriginalDilation);
			Run->BeforeCancel = WeaponState->GetCurrentWeaponType();
			Key(Run->WheelKey, IE_Pressed);
			Run->Next = Now + 0.5;
			break;
		case 6:
			TestTrue(TEXT("Second hold opens"), Component->IsOpen());
			Key(Run->WheelKey, IE_Released);
			Run->Next = Now + 0.25;
			break;
		case 7:
			TestEqual(TEXT("Center release preserves weapon"), WeaponState->GetCurrentWeaponType(), Run->BeforeCancel);
			Key(Run->WheelKey, IE_Pressed);
			Run->Next = Now + 0.5;
			break;
		case 8:
			TestTrue(TEXT("Hold before menu"), Component->IsOpen());
			Controller->RequestPauseMenu();
			Run->Next = Now + 0.5;
			break;
		case 9:
			TestFalse(TEXT("Menu cancels wheel"), Component->IsOpen());
			TestEqual(TEXT("Menu restores dilation"), Controller->GetWorld()->GetWorldSettings()->TimeDilation, Run->OriginalDilation);
			Key(Run->WheelKey, IE_Released);
			if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Controller))
			{
				UI->PopScreen(VBGameplayTags::UI_Screen_Pause);
			}

			Component->Close(false);
			return true;
		}

		return false;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
	}));
	return true;
}
#endif
