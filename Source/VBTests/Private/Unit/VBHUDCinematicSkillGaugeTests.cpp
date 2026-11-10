#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "UI/Combat/VBCinematicSkillGaugeWidget.h"
#include "VBUITestFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHUDCinematicSkillGaugeBindingTest, "VB.Unit.HUD.CinematicSkillGauge.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHUDCinematicSkillGaugeBindingTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	ON_SCOPE_EXIT { F.Shutdown(); };
	AVBPlayerState* A = F.SpawnPlayerState(*this);
	AVBPlayerState* B = F.SpawnPlayerState(*this);
	if (!A || !B)
	{
		return false;
	}

	UAbilitySystemComponent* ASC_A = A->GetAbilitySystemComponent();
	UAbilitySystemComponent* ASC_B = B->GetAbilitySystemComponent();
	const FGameplayAttribute Current = UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute();
	const FGameplayAttribute Max = UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute();
	ASC_A->SetNumericAttributeBase(Max, 100);
	ASC_A->SetNumericAttributeBase(Current, 50);
	ASC_B->SetNumericAttributeBase(Max, 120);
	ASC_B->SetNumericAttributeBase(Current, 30);
	UVBCinematicSkillGaugeWidget* Widget = F.MakeRow<UVBCinematicSkillGaugeWidget>(*this, nullptr);
	if (!Widget)
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);
	TestEqual(TEXT("late PlayerState starts empty"), Widget->GetGaugePercent(), 0.0f);
	F.SetPlayerState(A);
	TestEqual(TEXT("late binding immediately reads the attributes"), Widget->GetGaugePercent(), 0.5f);
	TestTrue(TEXT("current subscription exists"), ASC_A->GetGameplayAttributeValueChangeDelegate(Current).IsBoundToObject(Widget));
	Widget->InitializeWithASC(ASC_A);
	ASC_A->SetNumericAttributeBase(Current, 100);
	TestTrue(TEXT("full is not a cached initialization value"), Widget->IsGaugeFull());
	F.SetPlayerState(B);
	TestEqual(TEXT("replaced PlayerState snapshot"), Widget->GetGaugePercent(), 0.25f);
	TestFalse(TEXT("old source detached"), ASC_A->GetGameplayAttributeValueChangeDelegate(Current).IsBoundToObject(Widget));
	ASC_A->SetNumericAttributeBase(Current, 0);
	TestEqual(TEXT("old source cannot update view"), Widget->GetGaugePercent(), 0.25f);
	ASC_B->SetNumericAttributeBase(Max, 60);
	TestEqual(TEXT("max subscription updates view"), Widget->GetGaugePercent(), 0.5f);
	Life.Destruct(*Widget);
	TestFalse(TEXT("destruct removes current"), ASC_B->GetGameplayAttributeValueChangeDelegate(Current).IsBoundToObject(Widget));
	TestFalse(TEXT("destruct removes max"), ASC_B->GetGameplayAttributeValueChangeDelegate(Max).IsBoundToObject(Widget));
	ASC_B->SetNumericAttributeBase(Current, 60);
	TestEqual(TEXT("destructed view remains cleared"), Widget->GetGaugePercent(), 0.0f);
	Life.Construct(*Widget);
	TestEqual(TEXT("reconstruction binds existing PlayerState"), Widget->GetGaugePercent(), 1.0f);
	TestTrue(TEXT("reconstructed full"), Widget->IsGaugeFull());
	Widget->InitializeWithASC(nullptr);
	TestEqual(TEXT("explicit unbind clears ratio"), Widget->GetGaugePercent(), 0.0f);
	TestFalse(TEXT("0/0 view is not full"), Widget->IsGaugeFull());
	Life.Destruct(*Widget);
	return true;
}
#endif
