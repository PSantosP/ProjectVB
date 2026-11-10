#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBCinematicInPlayPolicy.generated.h"

class APlayerController;
class FVBCinematicGameLease;
class FVBInPlayCinematicLease;
class UVBCinematicDirectorSubsystem;

// 진행 중 장면(영역, 상호작용, 퀘스트가 거는 시네마틱)이 지금 카메라를 가져가도 되는지 판단한다.
// 요청마다 임대(FVBInPlayCinematicLease)를 하나 만든다. 새 게임 첫 장면은 UVBCinematicStartupPolicy 가 맡는다.
UCLASS()
class VB_API UVBCinematicInPlayPolicy : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	TSharedPtr<FVBCinematicGameLease> AcquireForController(APlayerController* Controller, FGuid AttemptId);
	bool TickRestore(float DeltaTime);
	void Stop();

	TWeakObjectPtr<UVBCinematicDirectorSubsystem> Director;
	TSharedPtr<FVBInPlayCinematicLease> ActiveLease;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bStopping = true;
};
