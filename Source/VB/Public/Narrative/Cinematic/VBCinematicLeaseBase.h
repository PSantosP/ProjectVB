#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Cinematic/VBCinematicSkipHold.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveHold.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class APawn;
class APlayerController;
class UVBSaveGameSubsystem;

// 장면 하나의 임대가 공통으로 관리하는 것: 컨트롤러, 폰 문맥과 저장, 입력, 준비 막기, 시야 복귀 확인.
// 막기를 언제 얻고 어떤 순서로 푸는지는 하위 클래스가 정한다.
class VB_API FVBCinematicLeaseBase : public FVBCinematicGameLease
{
public:
	UE_NONCOPYABLE(FVBCinematicLeaseBase);
	virtual ~FVBCinematicLeaseBase() override;

	// 임대를 받을 때의 컨트롤러, 폰 문맥이 지금도 맞으면 true.
	virtual bool IsCurrent() const;
	bool IsFor(const APlayerController* PC) const;
	bool IsFinished() const { return bFinished; }

	virtual bool IsSaveHeld() const override;
	virtual bool BindSkipInput(const UInputAction* Action) override;
	virtual bool ConsumeSkipRequest(float HoldSeconds) override;
	virtual void RestoreGameplay() override { bRestoreRequested = true; }
	virtual bool IsGameplayRestored() const override;

protected:
	FVBCinematicLeaseBase(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave);

	// 컨트롤러 입력 스택 맨 위에 모든 입력을 막는 컴포넌트를 올린다.
	void PushInputGuard();
	void RemoveInputGuard();
	// 복귀 요청이 왔고 UI 입력이 열렸고 시야가 게임 카메라로 돌아와 준비 막기를 풀 수 있으면 true.
	bool IsReadyToRestore() const;

	TWeakObjectPtr<UObject> Owner;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<AActor> GameplayViewTarget;
	TWeakObjectPtr<UVBPlayerReadinessComponent> Readiness;
	TWeakObjectPtr<UVBSaveGameSubsystem> Save;
	TStrongObjectPtr<UEnhancedInputComponent> InputGuard;
	TSharedPtr<FVBCinematicSkipHold> SkipHold;
	TWeakObjectPtr<const UInputAction> SkipAction;
	FVBSaveHoldToken SaveHold;
	FVBPlayerPreparationHoldToken PreparationHold;
	bool bRestoreRequested = false;
	bool bRestored = false;
	bool bFinished = false;
};
