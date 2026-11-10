#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"
#include "Narrative/Story/VBRuleTypes.h" // FVBOnDeferredEffectClosed
#include "Subsystems/WorldSubsystem.h"
#include "VBCinematicPlaybackTypes.h"
#include "VBCinematicDirectorSubsystem.generated.h"

class APawn;
class APlayerController;
class APlayerState;
class UVBCinematicDefinition;
class UVBCinematicPlaybackSubsystem;
class UVBWeaponVisualComponent;
class UVBUIManagerSubsystem;
class UInputAction;

// 반드시 거치는 게임 정책 - 실제 저장, 입력 보호, 그다음 확인된 게임플레이 복구.
class VB_API FVBCinematicGameLease
{
public:
	virtual ~FVBCinematicGameLease() = default;
	virtual bool IsSaveHeld() const = 0;
	virtual bool BindSkipInput(const UInputAction* Action)
	{
		return false;
	}
	virtual bool ConsumeSkipRequest(float HoldSeconds)
	{
		return false;
	}
	virtual void RestoreGameplay() = 0;
	virtual bool IsGameplayRestored() const = 0;
	// 여러 번 불러도 같다. false 는 정리일 뿐 전환 중 저장 보류를 풀어도 된다는 뜻이 아니다.
	virtual void Finish(bool bSucceeded) = 0;
};

DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<FVBCinematicGameLease>, FVBCinematicAcquireSafety,
	APlayerController*, FGuid);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBGameCinematicHandoffEnded,
	const FVBCinematicEndResult&, bool);

enum class EVBGameCinematicStartStatus : uint8
{
	Accepted, Busy, InvalidContext, SafetyUnavailable, CoreRejected
};
VB_API const TCHAR* LexToString(EVBGameCinematicStartStatus Status);

struct FVBGameCinematicStartResult
{
	EVBGameCinematicStartStatus Status = EVBGameCinematicStartStatus::InvalidContext;
	FVBCinematicStartResult Core;
};

UCLASS()
class VB_API UVBCinematicDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	// 특별한 장면의 정책(새 게임 첫 장면) - 자기 시도에만 임대를 준다. 먼저 묻는다.
	bool InstallSafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider);
	void RemoveSafetyProvider(UObject* Owner);
	// 진행 중 장면의 정책(UVBCinematicInPlayPolicy) - 위가 이 요청에 임대를 주지 않으면 묻는다.
	bool InstallInPlaySafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider);
	void RemoveInPlaySafetyProvider(UObject* Owner);
	FVBGameCinematicStartResult RequestPlay(UVBCinematicDefinition* Definition, APlayerState* Instigator,
		FName PlayerBindingTag, const TArray<FInstancedStruct>& OnCompletedEffects, AActor* Counterpart = nullptr,
		FVBOnDeferredEffectClosed OnClosed = FVBOnDeferredEffectClosed(),
		const TArray<FInstancedStruct>& OnHandoffEffects = {});
	bool HasCompletedFor(const APawn* Pawn, const UVBCinematicDefinition* Definition) const;
	// 요청 하나를 받아 처리 중인가(받아들이는 중부터 끝날 때까지). 읽기만 한다 - 다른 시스템이 "지금 장면 중인가" 를 물을 때.
	bool IsBusy() const { return bProcessing || RequestId.IsValid(); }
	bool SkipActive();
	bool CancelActive();
	FVBGameCinematicHandoffEnded OnHandoffEnded;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBCinematicHandoffTestAccess;
#endif
	void HandleSessionEnded(const FVBCinematicEndResult& Result);
	bool SkipPlayback();
	bool TickHandoff(float DeltaTime);
	bool IsContextCurrent() const;
	// bSucceeded = 재생, 복귀가 끝났다(임대를 그렇게 끝낸다). 알리는 성공은 완료 효과까지 해냈을 때만이다.
	void CloseRequest(bool bSucceeded, bool bEffectsLanded = true);
	// 임대를 놓고 알림이 끝난 뒤에도 같은 플레이어와 실행인지 다시 확인한다.
	bool PrepareHandoffContext(FVBRuleContext& Context) const;
	void ReleaseRequestData();
	void Stop();

#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBWeaponPresentationTestAccess;
	friend class FVBCinematicHUDTestAccess;
#endif
	bool AcquireWeaponPresentation(APawn& Pawn, const UVBCinematicDefinition& Definition);
	bool AcquireHUDPresentation(APawn& Pawn, const UVBCinematicDefinition& Definition);
	TWeakObjectPtr<UVBUIManagerSubsystem> HUDPresentationOwner;
	FGuid HUDPresentationToken;
	TWeakObjectPtr<UVBWeaponVisualComponent> WeaponPresentationOwner;
	FGuid WeaponPresentationToken;

	UPROPERTY(Transient)
	TObjectPtr<UVBCinematicDefinition> ActiveDefinition;
	UPROPERTY(Transient)
	TArray<FInstancedStruct> CompletionEffects;
	UPROPERTY(Transient)
	TArray<FInstancedStruct> HandoffEffects;

	struct FCompletionReceipt { TWeakObjectPtr<APawn> Pawn; FGuid AttemptId; };
	TMap<TWeakObjectPtr<UVBCinematicDefinition>, FCompletionReceipt> Receipts;
	// 임대를 준 쪽(위 두 정책 중 하나)이 살아 있는가 - 받기 전에는 어느 쪽이든 있는가.
	bool HasLeaseOwner() const;
	TSharedPtr<FVBCinematicGameLease> AcquireLease(APlayerController* PC, FGuid Attempt);
	TWeakObjectPtr<UObject> ProviderOwner;
	FVBCinematicAcquireSafety AcquireSafety;
	TWeakObjectPtr<UObject> InPlayProviderOwner;
	FVBCinematicAcquireSafety InPlayAcquireSafety;
	TWeakObjectPtr<UObject> LeaseOwner;
	TSharedPtr<FVBCinematicGameLease> Lease;
	TWeakObjectPtr<UVBCinematicPlaybackSubsystem> Playback;
	TWeakObjectPtr<APawn> ActivePawn;
	TWeakObjectPtr<APlayerController> ActiveController;
	TWeakObjectPtr<APlayerState> ActiveInstigator;
	TWeakObjectPtr<AActor> ActiveCounterpart;
	FGuid RequestId;
	FGuid AttemptId;
	FGuid SessionId;
	TOptional<FVBCinematicEndResult> EndResult;
	// 받아들인 요청의 결과를 기다리는 쪽 - 요청을 닫을 때 한 번 부르고 비운다. 거절된 요청은 들지 않는다(부른 쪽이 거절을 안다).
	FVBOnDeferredEffectClosed ActiveOnClosed;
	FDelegateHandle EndHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bHadCounterpart = false;
	bool bRestoreRequested = false;
	bool bAdmitted = false;
	bool bRestorationConfirmed = false;
	bool bProcessing = false;
	bool bStopping = true;
};
