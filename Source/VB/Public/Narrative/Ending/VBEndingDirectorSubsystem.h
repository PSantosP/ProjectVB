#pragma once

#include "CoreMinimal.h"
#include "Narrative/Ending/VBEndingCatalog.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBEndingDirectorSubsystem.generated.h"

class APlayerState;
class UVBCampaignStateComponent;
class UVBCinematicDirectorSubsystem;
class UVBStoryStateComponent;
struct FVBCinematicEndResult;

// 서버가 가진 피날레 시도. 나중의 선택 UI/RPC 는 호출자 소유를 따로 검증해야 한다.
UCLASS()
class VB_API UVBEndingDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	bool BeginFinale(APlayerState* Instigator);
	bool ChooseEnding(FGuid Attempt, FGameplayTag Ending);
	bool CancelFinale(FGuid Attempt);
	FGuid GetAttemptId() const { return AttemptId; }
	const TArray<FVBEndingDefinition>& GetOfferedEndings() const { return FrozenGroup; }

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	// 수신 계약 시험용 fixture 다 - 재생 진입, 복원은 시네마틱 담당 클래스 쪽에서 시험한다.
	friend class FVBEndingDirectorReceiptTest;
#endif
	enum class EPhase : uint8 { Idle, Offered, Playing, Completed };
	bool CanWrite() const;
	bool IsContextCurrent() const;
	bool CompleteAttempt();
	void HandleCinematicEnded(const FVBCinematicEndResult& Result, bool bSucceeded);
	void ResetAttempt();
	void Stop();

	// 보인 내용, 시네마틱까지 줄을 복사해 두어 카탈로그를 고쳐도 이미 제시한 선택이 바뀌지 않는다.
	UPROPERTY(Transient)
	TArray<FVBEndingDefinition> FrozenGroup;

	TWeakObjectPtr<APlayerState> ActiveInstigator;
	TWeakObjectPtr<UVBCampaignStateComponent> Campaign;
	TWeakObjectPtr<UVBStoryStateComponent> Story;
	TWeakObjectPtr<UVBCinematicDirectorSubsystem> Cinematic;
	FDelegateHandle CinematicHandle;
	FGuid AttemptId;
	FGuid RunId;
	FGuid SaveAttemptId;
	FGuid PlaybackSessionId;
	int32 StoryReadSerial = 0;
	int32 SelectedIndex = INDEX_NONE;
	EPhase Phase = EPhase::Idle;
	bool bStopping = true;
};
