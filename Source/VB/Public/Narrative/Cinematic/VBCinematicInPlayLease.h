#pragma once

#include "CoreMinimal.h"
#include "Narrative/Cinematic/VBCinematicLeaseBase.h"

// 진행 중 장면 하나의 임대: 저장 막기, 준비 막기, 맨 위 입력 막기를 얻고, 시야가 게임 카메라로 돌아오면 입력, 준비 막기를 푼다.
// 플레이어가 조종 중이고 기록 중인 저장이 없을 때만 받는다. 끝나면 성공 여부와 무관하게 셋을 다 푼다.
class VB_API FVBInPlayCinematicLease final : public FVBCinematicLeaseBase
{
public:
	FVBInPlayCinematicLease(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave);
	virtual ~FVBInPlayCinematicLease() override;

	// 플레이어가 지금 조종 중일 때만 세 막기를 얻는다. 하나라도 못 얻으면 얻은 것을 풀고 false.
	bool Acquire();

	// 정책이 장면 동안 매 틱 부른다.
	void TickRestore();

	virtual void Finish(bool bSucceeded) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBInPlayLeaseTestAccess;
#endif
	// 세 막기(저장, 입력, 준비)를 얻는다. 기록 중인 저장이 있으면 저장 막기부터 거절된다.
	bool TakeHolds();
	void ReleaseHolds();
};
