#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GameFramework/GameStateBase.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCampaignClearTest, "VB.Unit.Campaign.RecordClear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCampaignClearTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Alone = FGameplayTag::RequestGameplayTag(TEXT("Ending.ReturnAlone"));
	const FGameplayTag Shared = FGameplayTag::RequestGameplayTag(TEXT("Ending.SharedRecovery"));
	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>());
	Save->Campaign.CampaignId = FGuid::NewGuid();
	Save->Campaign.RunId = FGuid::NewGuid();
	Save->Campaign.RunIndex = 2;
	Save->Campaign.Parent.ClearId = FGuid::NewGuid();
	Save->Campaign.Parent.Ending = Alone;
	const TStrongObjectPtr<UVBCampaignStateComponent> Campaign(NewObject<UVBCampaignStateComponent>());
	Campaign->ReadSave(*Save.Get(), FVBSaveContext());
	AddExpectedMessagePlain(TEXT("clear id or ending tag is unset"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("unset clear rejected"), Campaign->RecordClear(FVBClearSummary()));
	FVBClearSummary MissingTag;
	MissingTag.ClearId = FGuid::NewGuid();
	TestFalse(TEXT("unset ending rejected"), Campaign->RecordClear(MissingTag));
	TestFalse(TEXT("invalid records leave LastClear empty"), Campaign->GetState().LastClear.IsSet());
	FVBClearSummary First;
	First.ClearId = FGuid::NewGuid();
	First.Ending = Shared;
	TestTrue(TEXT("valid clear accepted"), Campaign->RecordClear(First));
	TestEqual(TEXT("parent id unchanged"), Campaign->GetPrevious().ClearId, Save->Campaign.Parent.ClearId);
	TestEqual(TEXT("parent ending unchanged"), Campaign->GetPrevious().Ending, Alone);
	TestEqual(TEXT("campaign identity unchanged"), Campaign->GetState().CampaignId, Save->Campaign.CampaignId);
	TestEqual(TEXT("run identity unchanged"), Campaign->GetState().RunId, Save->Campaign.RunId);
	TestEqual(TEXT("run index unchanged"), Campaign->GetState().RunIndex, 2);
	TestEqual(TEXT("LastClear is the new clear"), Campaign->GetState().LastClear.ClearId, First.ClearId);
	TestTrue(TEXT("seen contains first ending"), Campaign->GetState().SeenEndings.HasTagExact(Shared));
	FVBClearSummary Second;
	Second.ClearId = FGuid::NewGuid();
	Second.Ending = Alone;
	TestTrue(TEXT("later clear accepted"), Campaign->RecordClear(Second));
	TestEqual(TEXT("last clear is replaced"), Campaign->GetState().LastClear.ClearId, Second.ClearId);
	TestTrue(TEXT("seen retains both endings"), Campaign->GetState().SeenEndings.HasTagExact(Shared)
		&& Campaign->GetState().SeenEndings.HasTagExact(Alone));
	Campaign->WriteSave(*Save.Get(), FVBSaveContext());
	const TStrongObjectPtr<UVBCampaignStateComponent> Reloaded(NewObject<UVBCampaignStateComponent>());
	Reloaded->ReadSave(*Save.Get(), FVBSaveContext());
	TestEqual(TEXT("save round trip keeps latest clear"), Reloaded->GetState().LastClear.ClearId, Second.ClearId);
	TestTrue(TEXT("save round trip keeps seen endings"), Reloaded->GetState().SeenEndings.HasTagExact(Shared));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCampaignClearAuthorityTest, "VB.Unit.Campaign.RecordClearAuthority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCampaignClearAuthorityTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Scope;
	if (!Scope.Init(*this))
	{
		return false;
	}

	AGameStateBase* Client = Scope.World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("client owner fixture"), Client))
	{
		return false;
	}

	UVBCampaignStateComponent* Remote = NewObject<UVBCampaignStateComponent>(Client);
	Client->SetRole(ROLE_SimulatedProxy);
	FVBClearSummary Clear;
	Clear.ClearId = FGuid::NewGuid();
	Clear.Ending = FGameplayTag::RequestGameplayTag(TEXT("Ending.ReturnAlone"));
	AddExpectedMessagePlain(TEXT("RecordClear"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("non-authority cannot record clear"), Remote->RecordClear(Clear));
	TestFalse(TEXT("client state stays empty"), Remote->GetState().LastClear.IsSet());
	return true;
}
#endif
