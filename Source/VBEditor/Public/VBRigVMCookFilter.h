#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UObject;
class UPackage;

/**
 * 쿡 저장에서 RigVM 런타임 에셋(UE 5.8 새 형식 컨트롤 릭)의 에디터 전용 하위 객체를 빼는 필터 (FIND-160).
 * 엔진은 그 데이터를 `<에셋>_EditorOnly` public 하위 객체로 두고, 클래스가 UncookedOnly 모듈이라 PKG_EditorOnly 가 아니어서
 * 쿡에 실린다. 게임에는 그 클래스가 없어 로드 때 CreateExport 오류가 난다(릭 셋에서 141 / 13,050 / 13,050줄).
 * 동작: 쿡 커맨드렛 안의 쿡 저장 pre-save 에서 RF_Transient 를 더하기만 하고, 되돌리기는 커맨드렛이 끝날 때 한 번(RestoreAll).
 * 저장 도중에 되돌리지 않는 이유 = 객체 PreSave 가 엔진의 재귀 저장 차단보다 먼저 돌아 같은 패키지 쿡 저장이 안에서 끝날 수 있고,
 * 엔진은 실패 신호를 주지 않는다. 설계 .sw/rig-runtime-format/design.md B (리뷰 P1 0, rev 4).
 */
class VBEDITOR_API FVBRigVMCookFilter
{
public:
	/** IsActive = 이 프로세스에서 켤지(모듈은 IsRunningCookCommandlet 을 준다, 시험은 true). */
	explicit FVBRigVMCookFilter(TFunction<bool()> InIsActive);
	~FVBRigVMCookFilter();

	void Register();
	void Unregister();

	/** 패키지 pre-save 한 번. 쿡이 아니거나 꺼져 있으면 상태를 바꾸지 않는다. */
	void HandlePreSave(UPackage* Package, bool bCooking);

	/** 이 필터가 더한 RF_Transient 만 걷는다. 원래 transient 였던 객체는 그대로. */
	void RestoreAll();

	int32 NumFlaggedObjects() const { return Flagged.Num(); }
	bool IsFlaggedByFilter(const UObject* Object) const;

private:
	TFunction<bool()> IsActive;
	TSet<TWeakObjectPtr<UObject>> Flagged;
	TSet<TWeakObjectPtr<UObject>> SeenRigs;
	TSet<FName> SeenPackages;
	FDelegateHandle PreSaveHandle;
};
