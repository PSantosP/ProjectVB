// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Interaction/VBInteractableConfig.h"
#include "PlayBot/VBPlayBotScenario.h"

#if WITH_DEV_AUTOMATION_TESTS

// run override 복원은 무엇을 되돌려 썼는지 말해야 한다 - 살아 있는 객체 값을 바꾸고 못 되돌린 run 은 조용한 통과가 아니라 깨진 run 이다.
//   사라진 객체는 실패가 아니라 unavailable 로 보고한다.
class FVBPlayBotOverridesTestAccess
{
public:
	// SetOriginal - 기억한 원래 값을 깨 복원이 실패하게 한다. 밖에서 실패 갈래에 닿는 유일한 길이다.
	static void SetOriginal(FVBPlayBotOverrides& Overrides, FName Property, const FString& Text)
	{
		Overrides.Originals.Last().Property = Property;
		Overrides.Originals.Last().Text = Text;
	}
};

namespace VBPlayBotOverridesTest
{
	FVBPlayBotVariant RangeVariant(const UObject& Object, const TCHAR* Value)
	{
		FVBPlayBotVariant Variant;
		Variant.Name = TEXT("Range");
		Variant.ObjectPath = Object.GetPathName();
		Variant.Property = TEXT("InteractRange");
		Variant.Value = Value;
		return Variant;
	}

	UVBInteractableConfig* NewConfig()
	{
		UVBInteractableConfig* Config = NewObject<UVBInteractableConfig>(GetTransientPackage(), NAME_None, RF_Transient);
		Config->InteractRange = 200.0f;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotOverridesRestoreTest, "VB.Unit.PlayBot.Overrides.Restore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotOverridesRestoreTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotOverridesTest;
	FString Readback, Error;
	{
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		Config->InteractRange = 0.0f;
		Readback = TEXT("stale_");
		Error = TEXT("stale_error");
		TestTrue(TEXT("zero float can be overridden"), Overrides.Apply(RangeVariant(*Config, TEXT("123")), Readback, Error));
		TestFalse(TEXT("readback does not append an earlier result"), Readback.StartsWith(TEXT("stale_")));
		TestTrue(TEXT("successful apply clears earlier errors"), Error.IsEmpty());
		Overrides.RestoreAll();
		TestEqual(TEXT("zero float is restored"), Config->InteractRange, 0.0f);
		TestTrue(TEXT("zero float has a complete snapshot"), Overrides.GetRestoreReport().Failures.IsEmpty());
	}
	{
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		Config->bBlocksPassage = false;
		FVBPlayBotVariant Variant = RangeVariant(*Config, TEXT("True"));
		Variant.Property = TEXT("bBlocksPassage");
		TestTrue(TEXT("false bool can be overridden"), Overrides.Apply(Variant, Readback, Error));
		Overrides.RestoreAll();
		TestFalse(TEXT("false bool is restored"), Config->bBlocksPassage);
		TestTrue(TEXT("false bool has a complete snapshot"), Overrides.GetRestoreReport().Failures.IsEmpty());
	}
	{
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		TestTrue(TEXT("apply"), Overrides.Apply(RangeVariant(*Config, TEXT("123")), Readback, Error));
		TestEqual(TEXT("applied value is live"), Config->InteractRange, 123.0f);
		TestFalse(TEXT("nothing to report before a restore"), Overrides.GetRestoreReport().HasEntries());
		Overrides.RestoreAll();
		const FVBPlayBotRestoreReport& Report = Overrides.GetRestoreReport();
		TestEqual(TEXT("original value is back"), Config->InteractRange, 200.0f);
		TestEqual(TEXT("one restored"), Report.Restored, 1);
		TestTrue(TEXT("no failures"), Report.Failures.IsEmpty());
		TestTrue(TEXT("report names its entries"), Report.HasEntries());
	}

	{
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		TestTrue(TEXT("apply before the object goes away"), Overrides.Apply(RangeVariant(*Config, TEXT("50")), Readback, Error));
		Config->MarkAsGarbage();
		Overrides.RestoreAll();
		const FVBPlayBotRestoreReport& Report = Overrides.GetRestoreReport();
		TestEqual(TEXT("a gone object is unavailable"), Report.Unavailable.Num(), 1);
		TestTrue(TEXT("a gone object is not a failure"), Report.Failures.IsEmpty());
	}

	{
		// 음성: property 를 더 찾을 수 없다 - 복원이 일어났을 수 없다.
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		TestTrue(TEXT("apply"), Overrides.Apply(RangeVariant(*Config, TEXT("77")), Readback, Error));
		FVBPlayBotOverridesTestAccess::SetOriginal(Overrides, TEXT("NoSuchProperty"), TEXT("200.000000"));
		Overrides.RestoreAll();
		TestEqual(TEXT("missing property is a failure"), Overrides.GetRestoreReport().Failures.Num(), 1);
		TestEqual(TEXT("and the swapped value is still there"), Config->InteractRange, 77.0f);
	}

	{
		// 음성: 값은 들어가지만 기억한 값으로 읽히지 않는다 - 복원이 다른 것을 썼다.
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* Config = NewConfig();
		TestTrue(TEXT("apply"), Overrides.Apply(RangeVariant(*Config, TEXT("77")), Readback, Error));
		FVBPlayBotOverridesTestAccess::SetOriginal(Overrides, TEXT("InteractRange"), TEXT("200.0"));
		Overrides.RestoreAll();
		TestEqual(TEXT("readback that differs is a failure"), Overrides.GetRestoreReport().Failures.Num(), 1);
	}

	{
		// 보고는 run 의 모든 복원을 덮는다 - 시나리오가 중간에 복원해도 subsystem 은 끝에 읽는다.
		FVBPlayBotOverrides Overrides;
		UVBInteractableConfig* First = NewConfig();
		UVBInteractableConfig* Second = NewConfig();
		TestTrue(TEXT("apply first"), Overrides.Apply(RangeVariant(*First, TEXT("10")), Readback, Error));
		Overrides.RestoreAll();
		TestTrue(TEXT("apply second"), Overrides.Apply(RangeVariant(*Second, TEXT("20")), Readback, Error));
		Overrides.RestoreAll();
		TestEqual(TEXT("both restores counted"), Overrides.GetRestoreReport().Restored, 2);
	}

	return true;
}
#endif
