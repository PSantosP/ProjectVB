// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Interaction/VBPhysicalDoor.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "VBPowerPanel.generated.h"

class APlayerState;
class AVBPowerSwitch;

// 회로 하나. 전기가 들어오면 등이 켜지고, 문이 있으면 열린다.
USTRUCT(BlueprintType)
struct VB_API FVBPowerCircuit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Power")
	FName Id;

	// 이 회로를 따라 켜지는 등(빛 컴포넌트를 가진 액터). 켜고 끄기는 제어함만 한다.
	// Movable 빛이어야 한다(구운 빛은 보이기를 꺼도 밝기가 남는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Power")
	TArray<TObjectPtr<AActor>> Lights;

	// 이 회로가 여는 물리 문(없어도 된다). 한 제어함에 문 회로는 하나다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Power")
	TObjectPtr<AVBPhysicalDoor> Door;
};

// 허용되는 배분 하나 = 전기가 들어간 회로들. 배분표의 0 번이 처음 상태다(카운터에 열쇠가 없음 = 0 = 처음).
USTRUCT(BlueprintType)
struct VB_API FVBPowerAllocation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Power")
	TArray<FName> Powered;
};

/**
 * 전력 제어함 - 배분을 관리한다. 저장되는 것은 AllocationCounter(배분표 번호)와 OpenedFact(문 완전 열림)뿐이고 나머지는 여기서 다시 구성한다.
 * AVBPowerSwitch 누름 -> RequestToggle. 공급 초과, 이동 중, 완료 뒤는 거절하고 그 플레이어에게 까닭을 보인다.
 * 문은 작업 번호로만 추적하고, 막힘은 열기/닫기 실패로 따로 붙들어 같은 회로 스위치를 다시 누르면 재시도한다.
 */
UCLASS()
class VB_API AVBPowerPanel : public AActor
{
	GENERATED_BODY()

public:
	AVBPowerPanel();

	// 서버에서만. 스위치가 부른다 - 그 회로를 뒤집어 달라는 요청.
	void RequestToggle(AVBPowerSwitch& Switch, AActor* Interactor);

	// 지금 배분 번호(배분표 범위 안으로 읽은 값). 결함이면 0.
	int32 GetAllocationIndex() const;
	bool IsOpened() const;
	bool HasFault() const { return !FaultReason.IsEmpty(); }
	const FString& GetFaultReason() const { return FaultReason; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Power")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	TArray<FVBPowerCircuit> Circuits;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	TArray<FVBPowerAllocation> Allocations;

	// 동시에 전기가 들어갈 수 있는 회로 수.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power", meta=(ClampMin="1"))
	int32 Supply = 1;

	// 이 제어함의 회로 스위치들(각 스위치의 Panel 이 이 제어함이어야 한다).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	TArray<TObjectPtr<AVBPowerSwitch>> Switches;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power", meta=(Categories="Story"))
	FGameplayTag AllocationCounter;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power", meta=(Categories="Story"))
	FGameplayTag OpenedFact;

	// 문이 처음으로 끝까지 열렸을 때 한 번. 불러온 완료 상태에서는 다시 내지 않는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnOpenedEffects;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText OverSupplyText;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText DoorMovingText;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText CompletedText;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText OpenBlockedText;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText CloseBlockedText;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FText DoorUnavailableText;

	// 시작 때 문, 스위치, 사실 저장소가 준비되기를 기다리는 상한(초). 넘으면 결함.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power", meta=(ClampMin="0.1"))
	float StartupTimeoutSeconds = 5.0f;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBPowerPanelTestAccess;
#endif
	enum class EDoorPhase : uint8 { None, Open, Close };

	// 배치 검사. 결함이면 이유.
	FString ValidateSetup() const;
	// 서버: 문, 스위치, 사실 저장소가 다 시작했나. 다 됐으면 시작하고 true.
	bool TryStart();
	void HandleStartupTick();
	void HandleStoryChanged();
	void HandleDoorFinished(uint32 Serial, EVBDoorMoveResult Result);

	// 서버: 카운터, 사실에서 스위치, 문을 맞춘다. 문 요청은 붙든 실패가 없고 할 일이 있을 때만 낸다.
	void Reconcile();
	// 모든 기계: 카운터, 사실에서 등을 맞춘다.
	void ApplyLights() const;

	const FVBPowerCircuit* FindDoorCircuit() const;
	bool IsPoweredIn(int32 AllocationIndex, FName CircuitId) const;
	int32 FindAllocation(const TArray<FName>& Powered) const;
	// 카운터 값(검사 전).
	int32 ReadCounter() const;
	void WriteCounter(int32 Index) const;
	void StartDoor(bool bOpen);
	// 서버: 열림 사실을 켜고 완료 효과를 한 번 낸다(이미 켜져 있으면 아무것도 안 한다).
	void Latch();
	void Refuse(AActor* Interactor, const FText& Text, const TCHAR* Why) const;
	// 시작했고 결함이 없고 묶인 사실 저장소가 있다 - 없는 저장소를 배분 0, 닫힘으로 읽어 장치를 움직이지 않게.
	bool IsReady() const { return bStarted && !HasFault() && StoryBinding.GetStory(); }
	APlayerState* RequesterState() const;

	bool bStarted = false;
	double StartupDeadline = 0.0;
	FTimerHandle StartupTimer;
	FVBStoryStateBinding StoryBinding;
	FDelegateHandle DoorHandle;
	FString FaultReason;

	uint32 DoorSerial = 0;
	EDoorPhase DoorPhase = EDoorPhase::None;
	bool bOpenFailed = false;
	bool bCloseFailed = false;
	// 지금 누르고 있는 사람(RequestToggle 동안만), 지금 문 요청을 낸 사람(그 결과를 보일 곳).
	TWeakObjectPtr<AActor> PressRequester;
	TWeakObjectPtr<AActor> DoorRequester;
};
