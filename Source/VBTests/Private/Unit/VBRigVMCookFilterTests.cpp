// ProjectVB Copyright (c) 2025. All Rights Reserved.

// FVBRigVMCookFilter - 5.8 runtime-format control rig 의 에디터 전용 데이터를 RF_Transient 로 cook 저장에서 뺀다(RestoreAll 만 되돌림)
// 경우: (a) 필터가 필요한 엔진 전제, (b) 같은 패키지 재진입, (c) 사이의 비 cook 저장, (d) 다른 패키지, (e) 문
// 실제 asset 을 메모리에서만 쓰고 아무것도 저장하지 않으며, ON_SCOPE_EXIT 가 되돌린다

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "RigVMRuntimeAsset.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "VBRigVMCookFilter.h"

namespace VBRigVMCookFilterTest
{
	const TCHAR* RigA = TEXT("/Game/Characters/UEFN_Mannequin/Rigs/CR_UEFN_Mannequin_FullBodyIK.CR_UEFN_Mannequin_FullBodyIK");
	const TCHAR* RigB = TEXT("/Game/VB/Animations/LookAtPOI/CR_VBBossLookAtPOI.CR_VBBossLookAtPOI");

	UObject* EditorDataOf(UObject* Rig)
	{
		IRigVMRuntimeAssetInterface* Asset = Cast<IRigVMRuntimeAssetInterface>(Rig);
		return Asset ? Asset->GetEditorOnlyData() : nullptr;
	}

	int32 CountTransient(UObject* EditorData)
	{
		TArray<UObject*> Subtree;
		GetObjectsWithOuter(EditorData, Subtree, true);
		Subtree.Add(EditorData);
		int32 Count = 0;
		for (const UObject* Object : Subtree)
		{
			Count += Object->HasAnyFlags(RF_Transient) ? 1 : 0;
		}

		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRigVMCookFilterTest, "VB.Unit.Editor.RigVMCookFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVBRigVMCookFilterTest::RunTest(const FString& Parameters)
{
	using namespace VBRigVMCookFilterTest;
	UObject* A = LoadObject<UObject>(nullptr, RigA);
	UObject* B = LoadObject<UObject>(nullptr, RigB);
	if (!TestNotNull(TEXT("rig A loads"), A) || !TestNotNull(TEXT("rig B loads"), B))
	{
		return false;
	}

	UObject* DataA = EditorDataOf(A);
	UObject* DataB = EditorDataOf(B);
	if (!TestNotNull(TEXT("rig A is a RigVM runtime asset with editor data"), DataA) || !TestNotNull(TEXT("rig B too"), DataB))
	{
		return false;
	}

	// (a) 이 필터가 필요한 이유 자체 - 엔진이 고치면 여기가 뒤집히고, 그때 필터를 지운다.
	const UPackage* ClassPackage = DataA->GetClass()->GetOutermost();
	TestTrue(TEXT("editor data class lives in an UncookedOnly script package"), ClassPackage->HasAnyPackageFlags(PKG_UncookedOnly));
	TestFalse(TEXT("... which is not PKG_EditorOnly, so the saver keeps it (the defect)"),
		ClassPackage->HasAnyPackageFlags(PKG_EditorOnly));

	const int32 TransientBeforeA = CountTransient(DataA);
	const int32 TransientBeforeB = CountTransient(DataB);
	bool bGateOpen = true;
	FVBRigVMCookFilter Filter([&bGateOpen] { return bGateOpen; });
	ON_SCOPE_EXIT { Filter.RestoreAll(); };

	// (e) 문이 닫혀 있으면 아무것도 바꾸지 않는다: 커맨드렛 아님 / 쿡 아님 / cvar 0.
	bGateOpen = false;
	Filter.HandlePreSave(A->GetPackage(), true);
	bGateOpen = true;
	Filter.HandlePreSave(A->GetPackage(), false);
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Cook.StripRigVMEditorData"));
	if (!TestNotNull(TEXT("cvar registered"), CVar))
	{
		return false;
	}

	const int32 CVarBefore = CVar->GetInt();
	CVar->Set(0, ECVF_SetByCode);
	Filter.HandlePreSave(A->GetPackage(), true);
	CVar->Set(CVarBefore, ECVF_SetByCode);
	TestEqual(TEXT("gate closed: nothing flagged"), Filter.NumFlaggedObjects(), 0);

	// (b) 바깥 쿡 저장 A -> 안쪽 쿡 저장 A: 표시가 남고 같은 객체를 두 번 세지 않는다.
	Filter.HandlePreSave(A->GetPackage(), true);
	const int32 FlaggedA = Filter.NumFlaggedObjects();
	TestTrue(TEXT("cook pre-save flags the editor data"), FlaggedA > 0 && Filter.IsFlaggedByFilter(DataA)
		&& DataA->HasAnyFlags(RF_Transient));
	Filter.HandlePreSave(A->GetPackage(), true);
	TestEqual(TEXT("nested cook pre-save of A adds nothing twice"), Filter.NumFlaggedObjects(), FlaggedA);

	// (c) 그 사이 쿡이 아닌 저장 A: 바깥 저장의 표시를 건드리지 않는다.
	Filter.HandlePreSave(A->GetPackage(), false);
	TestTrue(TEXT("non-cooking pre-save keeps A flagged"), DataA->HasAnyFlags(RF_Transient) && Filter.NumFlaggedObjects() == FlaggedA);

	// (d) 다른 패키지 B 를 더해도 A 는 그대로, 되돌리기는 RestoreAll 하나뿐.
	Filter.HandlePreSave(B->GetPackage(), true);
	TestTrue(TEXT("B flagged, A kept"), Filter.IsFlaggedByFilter(DataB) && DataA->HasAnyFlags(RF_Transient));
	Filter.RestoreAll();
	TestEqual(TEXT("RestoreAll leaves A's original transient objects only"), CountTransient(DataA), TransientBeforeA);
	TestEqual(TEXT("RestoreAll leaves B's original transient objects only"), CountTransient(DataB), TransientBeforeB);
	TestEqual(TEXT("nothing remembered after RestoreAll"), Filter.NumFlaggedObjects(), 0);
	return true;
}
#endif
