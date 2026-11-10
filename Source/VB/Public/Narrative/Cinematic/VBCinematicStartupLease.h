#pragma once

#include "CoreMinimal.h"
#include "Narrative/Cinematic/VBCinematicLeaseBase.h"

class APlayerState;
class UVBCinematicDefinition;

namespace VBCinematicStartup
{
	// 정의를 재생할 수 있으면 true. 거절 이유를 전부 오류 로그로 남긴다.
	VB_API bool ValidateDefinition(const UVBCinematicDefinition* Definition);
}

// 새 게임 첫 장면의 임대: 시작 시도 하나에 묶이고, 실패하면 저장 막기를 남긴다. 저장 막기는 복귀를 확인한 성공만 푼다.
class VB_API FVBStartupLease final : public FVBCinematicLeaseBase
{
public:
	FVBStartupLease(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave, FGuid InAttempt);

	bool Initialize(const UVBCinematicDefinition* Definition);
	bool Matches(APlayerController* PC, FGuid Id) const;
	bool PrepareToPlay();
	// 정책이 장면 동안 매 틱 부른다.
	void TickRestore();
	void CompleteInitialization();
	APlayerState* GetInstigator() const;

	virtual bool IsCurrent() const override;
	virtual void Finish(bool bSucceeded) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBStartupLeaseTestAccess;
#endif
	FGuid Attempt;
};
