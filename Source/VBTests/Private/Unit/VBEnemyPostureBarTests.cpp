// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Misc/ScopeExit.h"
#include "Player/VBPlayerState.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "Unit/VBUITestFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyPostureBarLifecycleTest, "VB.Unit.EnemyBar.Posture.Lifecycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyPostureBarLifecycleTest::RunTest(const FString& Parameters)
{
	constexpr float MeterTolerance = 1.e-4f;
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBPlayerState* FirstOwner = Fixture.SpawnPlayerState(*this);
	AVBPlayerState* SecondOwner = Fixture.SpawnPlayerState(*this);
	if (!FirstOwner || !SecondOwner)
	{
		return false;
	}

	UAbilitySystemComponent* First = FirstOwner->GetAbilitySystemComponent();
	UAbilitySystemComponent* Second = SecondOwner->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("first ASC"), First) || !TestNotNull(TEXT("second ASC"), Second))
	{
		return false;
	}

	for (UAbilitySystemComponent* ASC : {First, Second})
	{
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), 100.0f);
		ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), 100.0f);
	}

	UVBEnemyHealthBarWidget* Widget = Fixture.MakeRow<UVBEnemyHealthBarWidget>(*this,
		TEXT("/Game/VB/UI/WBP_EnemyHealthBar.WBP_EnemyHealthBar_C"));
	if (!Widget)
	{
		return false;
	}

	UProgressBar* Meter = Cast<UProgressBar>(Widget->WidgetTree->FindWidget(TEXT("PostureBar")));
	if (!TestNotNull(TEXT("authored enemy bar contains PostureBar"), Meter))
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);
	ON_SCOPE_EXIT { Life.Destruct(*Widget); };
	Widget->InitializeWithASC(First);
	TestTrue(TEXT("live target displays posture"), Meter->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("undamaged posture is empty"), Meter->GetPercent(), 0.0f);
	First->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), 60.0f);
	TestTrue(TEXT("poise damage fills the actual widget"), FMath::IsNearlyEqual(Meter->GetPercent(), 0.4f, MeterTolerance));
	First->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), 200.0f);
	TestTrue(TEXT("maximum changes refresh the widget"), FMath::IsNearlyEqual(Meter->GetPercent(), 0.7f, MeterTolerance));

	// 엔진 큐가 갱신하는 태그 맵을 통해 창의 시작과 끝을 전달한다.
	const FGameplayTag WindowCue = VBGameplayTags::GameplayCue_Combat_FinisherWindow;
	First->AddLooseGameplayTag(WindowCue);
	First->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), 200.0f);
	TestEqual(TEXT("visible window stays full after numeric refill"), Meter->GetPercent(), 1.0f);
	First->RemoveLooseGameplayTag(WindowCue);
	TestEqual(TEXT("window expiry displays current posture"), Meter->GetPercent(), 0.0f);
	First->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), 0.0f);
	TestEqual(TEXT("disabled posture hides the meter"), Meter->GetVisibility(), ESlateVisibility::Collapsed);

	Widget->InitializeWithASC(Second);
	TestFalse(TEXT("old poise listener detached"), First->GetGameplayAttributeValueChangeDelegate(
		UVBCombatAttributeSet::GetPoiseAttribute()).IsBoundToObject(Widget));
	TestFalse(TEXT("old maximum listener detached"), First->GetGameplayAttributeValueChangeDelegate(
		UVBCombatAttributeSet::GetMaxPoiseAttribute()).IsBoundToObject(Widget));
	TestFalse(TEXT("old cue listener detached"), First->RegisterGameplayTagEvent(WindowCue).IsBoundToObject(Widget));
	First->AddLooseGameplayTag(WindowCue);
	TestEqual(TEXT("old source cannot fill the new target meter"), Meter->GetPercent(), 0.0f);
	First->RemoveLooseGameplayTag(WindowCue);
	Second->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), 25.0f);
	TestTrue(TEXT("new source updates the meter"), FMath::IsNearlyEqual(Meter->GetPercent(), 0.75f, MeterTolerance));
	Second->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	TestEqual(TEXT("death hides the meter"), Meter->GetVisibility(), ESlateVisibility::Collapsed);

	Life.Destruct(*Widget);
	TestFalse(TEXT("Slate teardown detaches poise"), Second->GetGameplayAttributeValueChangeDelegate(
		UVBCombatAttributeSet::GetPoiseAttribute()).IsBoundToObject(Widget));
	TestFalse(TEXT("Slate teardown detaches the cue"), Second->RegisterGameplayTagEvent(WindowCue).IsBoundToObject(Widget));
	Second->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	Life.Construct(*Widget);
	Widget->InitializeWithASC(Second);
	TestTrue(TEXT("reconstruction reads the latest value"), FMath::IsNearlyEqual(Meter->GetPercent(), 0.75f, MeterTolerance));
	Widget->Clear();
	TestEqual(TEXT("clearing a pooled bar hides posture"), Meter->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("clear detaches poise"), Second->GetGameplayAttributeValueChangeDelegate(
		UVBCombatAttributeSet::GetPoiseAttribute()).IsBoundToObject(Widget));
	return true;
}
#endif
