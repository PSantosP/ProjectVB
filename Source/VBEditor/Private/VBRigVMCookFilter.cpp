#include "VBRigVMCookFilter.h"

#include "HAL/IConsoleManager.h"
#include "RigVMRuntimeAsset.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다. 쿡 로그에서 이 필터만 골라 보려고 따로 둔다.
DEFINE_LOG_CATEGORY_STATIC(LogVBRigVMCook, Log, All);

// 0 = 끄기. 음성 대조(끄면 개발용 객체가 다시 쿡에 실려야 한다)와, 엔진이 스스로 고쳤을 때 확인용.
static TAutoConsoleVariable<int32> CVarStripRigVMEditorData(
	TEXT("vb.Cook.StripRigVMEditorData"), 1,
	TEXT("1 = cook saves drop the editor-only data subobject of RigVM runtime assets (FIND-160). 0 = engine behavior."),
	ECVF_Default);

FVBRigVMCookFilter::FVBRigVMCookFilter(TFunction<bool()> InIsActive)
	: IsActive(MoveTemp(InIsActive))
{
}

FVBRigVMCookFilter::~FVBRigVMCookFilter()
{
	Unregister();
}

void FVBRigVMCookFilter::Register()
{
	if (!PreSaveHandle.IsValid())
	{
		PreSaveHandle = UPackage::PreSavePackageWithContextEvent.AddLambda(
			[this](UPackage* Package, FObjectPreSaveContext SaveContext) { HandlePreSave(Package, SaveContext.IsCooking()); });
	}
}

void FVBRigVMCookFilter::Unregister()
{
	if (PreSaveHandle.IsValid())
	{
		UPackage::PreSavePackageWithContextEvent.Remove(PreSaveHandle);
		PreSaveHandle.Reset();
	}
}

void FVBRigVMCookFilter::HandlePreSave(UPackage* Package, bool bCooking)
{
	// 조건을 먼저 본다 - 아니면 상태를 하나도 바꾸지 않는다(쿡 저장 도중 끼어든 일반 저장이 아무것도 못 건드리게).
	if (!Package || !bCooking || !IsActive || !IsActive() || CVarStripRigVMEditorData.GetValueOnAnyThread() == 0)
	{
		return;
	}
#if WITH_EDITORONLY_DATA
	TArray<UObject*> PackageObjects;
	GetObjectsWithPackage(Package, PackageObjects, true);
	for (UObject* Object : PackageObjects)
	{
		IRigVMRuntimeAssetInterface* RigAsset = Cast<IRigVMRuntimeAssetInterface>(Object);
		if (!RigAsset)
		{
			continue;
		}

		UObject* EditorData = RigAsset->GetEditorOnlyData();
		if (!EditorData)
		{
			// 엔진 구조가 바뀌었다 - 조용히 넘어가면 개발용 객체가 다시 쿡에 실린다. 쿡 오류로 알린다.
			UE_LOG(LogVBRigVMCook, Error,
				TEXT("%s: RigVM editor-only data not found - engine layout changed? This rig may cook its editor-only objects again (FIND-160)"),
				*Object->GetPathName());
			continue;
		}

		TArray<UObject*> Subtree;
		GetObjectsWithOuter(EditorData, Subtree, true);
		Subtree.Add(EditorData);
		int32 NewlyFlagged = 0;
		for (UObject* Sub : Subtree)
		{
			// 이미 transient 인 것(원래 그랬거나 앞선 저장에서 이 필터가 붙인 것)은 건너뛴다 - 원래 값은 RestoreAll 이 건드리지 않는다.
			if (!Sub || Sub->HasAnyFlags(RF_Transient))
			{
				continue;
			}

			Sub->SetFlags(RF_Transient);
			Flagged.Add(Sub);
			++NewlyFlagged;
		}

		SeenRigs.Add(Object);
		UE_LOG(LogVBRigVMCook, Verbose, TEXT("%s: %d editor-only objects kept out of the cook"), *Object->GetPathName(), NewlyFlagged);
	}

	SeenPackages.Add(Package->GetFName());
#endif
}

void FVBRigVMCookFilter::RestoreAll()
{
	int32 Restored = 0;
	for (const TWeakObjectPtr<UObject>& Weak : Flagged)
	{
		if (UObject* Object = Weak.Get())
		{
			Object->ClearFlags(RF_Transient);
			++Restored;
		}
	}

	if (Flagged.Num() > 0)
	{
		UE_LOG(LogVBRigVMCook, Display,
			TEXT("RigVM cook filter: %d rigs in %d packages, %d editor-only objects kept out of the cook (%d restored now)"),
			SeenRigs.Num(), SeenPackages.Num(), Flagged.Num(), Restored);
	}

	Flagged.Reset();
	SeenRigs.Reset();
	SeenPackages.Reset();
}

bool FVBRigVMCookFilter::IsFlaggedByFilter(const UObject* Object) const
{
	return Object && Flagged.Contains(TWeakObjectPtr<UObject>(const_cast<UObject*>(Object)));
}
