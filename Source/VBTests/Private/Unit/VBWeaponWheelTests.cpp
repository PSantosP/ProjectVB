#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/VBWeaponWheelRules.h"
#include "InputTriggers.h"
#include "Player/VBWeaponWheelComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "VBWeaponDefinitionTestUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponWheelSelectionTest, "VB.Unit.WeaponWheel.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBWeaponWheelSelectionTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Empty"), VBWeaponWheel::PickSlot({0, -100}, 0, 35), INDEX_NONE);
	TestEqual(TEXT("Center cancels"), VBWeaponWheel::PickSlot({0, 0}, 3, 35), INDEX_NONE);
	TestEqual(TEXT("Dead radius inclusive"), VBWeaponWheel::PickSlot({0, -35}, 3, 35), INDEX_NONE);
	TestEqual(TEXT("Top"), VBWeaponWheel::PickSlot({0, -100}, 3, 35), 0);
	TestEqual(TEXT("Clockwise"), VBWeaponWheel::PickSlot({100, 50}, 3, 35), 1);
	TestEqual(TEXT("Counterclockwise"), VBWeaponWheel::PickSlot({-100, 50}, 3, 35), 2);
	TestEqual(TEXT("One weapon"), VBWeaponWheel::PickSlot({-100, 50}, 1, 35), 0);
	TestEqual(TEXT("Four right"), VBWeaponWheel::PickSlot({100, 0}, 4, 35), 1);
	TestEqual(TEXT("Four bottom"), VBWeaponWheel::PickSlot({0, 100}, 4, 35), 2);
	TestEqual(TEXT("Four left"), VBWeaponWheel::PickSlot({-100, 0}, 4, 35), 3);
	TestEqual(TEXT("Clamped cursor"), VBWeaponWheel::AccumulateCursor({100, 0}, {100, 0}, 140), FVector2D(140, 0));
	TestEqual(TEXT("Return to center"), VBWeaponWheel::AccumulateCursor({100, 0}, {-100, 0}, 140), FVector2D::ZeroVector);
	TestEqual(TEXT("Invalid radius"), VBWeaponWheel::AccumulateCursor({100, 0}, {0, 0}, -1), FVector2D::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponWheelTriggerTest, "VB.Unit.WeaponWheel.TapHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBWeaponWheelTriggerTest::RunTest(const FString& Parameters)
{
	UInputTriggerTap* Tap = NewObject<UInputTriggerTap>();
	UInputTriggerHold* Hold = NewObject<UInputTriggerHold>();
	Tap->TapReleaseTimeThreshold = 0.25f;
	Hold->HoldTimeThreshold = 0.25f;
	Hold->bIsOneShot = false;
	auto Step = [](UInputTrigger* Trigger, bool bDown, float Delta)
	{
		const FInputActionValue Value(bDown);
		const ETriggerState State = Trigger->UpdateState(nullptr, Value, Delta);
		Trigger->LastValue = Value;
		return State;
	};
	TestEqual(TEXT("Tap waits"), Step(Tap, true, 0.1f), ETriggerState::Ongoing);
	TestEqual(TEXT("Hold waits"), Step(Hold, true, 0.1f), ETriggerState::Ongoing);
	TestEqual(TEXT("Short release summons"), Step(Tap, false, 0.01f), ETriggerState::Triggered);
	TestEqual(TEXT("Short release never opens"), Step(Hold, false, 0.01f), ETriggerState::None);
	Step(Tap, true, 0.3f);
	TestEqual(TEXT("Long press opens"), Step(Hold, true, 0.3f), ETriggerState::Triggered);
	TestEqual(TEXT("Still held"), Step(Hold, true, 0.1f), ETriggerState::Triggered);
	TestEqual(TEXT("Long release never summons"), Step(Tap, false, 0.01f), ETriggerState::None);
	TestEqual(TEXT("Long release completes"), Step(Hold, false, 0.01f), ETriggerState::None);
	UInputTriggerHold* OneShot = NewObject<UInputTriggerHold>();
	OneShot->HoldTimeThreshold = 0.25f;
	OneShot->bIsOneShot = true;
	Step(OneShot, true, 0.3f);
	TestEqual(TEXT("One-shot negative control ends while held"), Step(OneShot, true, 0.1f), ETriggerState::None);
	return true;
}

// 휠 항목 - 가진 무기만, 정의 순서대로, 정의의 DisplayName 을 이름으로 쓴다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponWheelEntriesTest, "VB.Unit.WeaponWheel.Entries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponWheelEntriesTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponDefinitionTest;
	const TStrongObjectPtr<UVBWeaponStateComponent> Weapons(NewObject<UVBWeaponStateComponent>());
	UVBWeaponDefinition* Sword = Make(Weapons.Get(), EVBWeaponType::OneHandSword);
	UVBWeaponDefinition* Blade = Make(Weapons.Get(), EVBWeaponType::Blade);
	UVBWeaponDefinition* Fighter = Make(Weapons.Get(), EVBWeaponType::Fighter);
	SetField(*Sword, TEXT("DisplayName"), FText::FromString(TEXT("Sword")));
	Equip(*Weapons, {Sword, Blade, Fighter}, EVBWeaponType::None);
	SetField(*Weapons, TEXT("OwnedWeapons"), TArray<EVBWeaponType>{EVBWeaponType::Fighter, EVBWeaponType::OneHandSword});

	TArray<FVBWeaponWheelEntry> Entries = UVBWeaponWheelComponent::CollectEntries(*Weapons);
	if (!TestEqual(TEXT("owned weapons only"), Entries.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("definition order, not ownership order"), Entries[0].Weapon, EVBWeaponType::OneHandSword);
	TestEqual(TEXT("label from the definition"), Entries[0].Label.ToString(), FString(TEXT("Sword")));
	TestEqual(TEXT("second entry"), Entries[1].Weapon, EVBWeaponType::Fighter);

	SetField(*Sword, TEXT("DisplayName"), FText::GetEmpty());
	AddExpectedMessage(TEXT("DisplayName"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Entries = UVBWeaponWheelComponent::CollectEntries(*Weapons);
	TestEqual(TEXT("an unnamed owned weapon still shows"), Entries.Num(), 2);
	TestFalse(TEXT("with a fallback label"), Entries[0].Label.IsEmpty());
	return true;
}
#endif
