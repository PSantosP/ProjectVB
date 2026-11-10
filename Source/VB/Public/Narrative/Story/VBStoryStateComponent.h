// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Narrative/Story/VBStoryState.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBStoryStateComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FVBOnStoryStateChanged);

/**
 * 세계 사실을 관리한다. AVBGameState 에 붙고 쓰기는 서버만 한다(클라이언트 호출은 경고 후 무시).
 * 바꾸는 길은 규칙의 효과(FVBEffect_SetFact, AddCounter) 하나로 둔다 - 기능 코드가 직접 쓰지 않는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBStoryStateComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBStoryStateComponent();

	// 지금 월드의 GameState 에 붙은 것. 없으면 nullptr(메뉴 월드 등).
	static UVBStoryStateComponent* Get(const UObject* WorldContextObject);

	const FVBStoryState& GetState() const { return State; }
	bool HasFact(FGameplayTag Fact) const { return State.HasFact(Fact); }
	int32 GetCounter(FGameplayTag Counter) const { return State.GetCounter(Counter); }
	int32 GetSaveReadSerial() const { return SaveReadSerial; }

	// 서버에서만 바꾼다. 바뀌었으면 OnStoryStateChanged.
	void SetFact(FGameplayTag Fact, bool bOn);
	void AddToCounter(FGameplayTag Counter, int32 Delta);
	// 그 값으로 정한다. 같은 값이면 아무것도 안 바꾸고 알리지도 않는다(같은 요청, 불러온 뒤 재평가가 상태를 움직이지 않게).
	void SetCounter(FGameplayTag Counter, int32 Value);

	// 서버에서는 바꾼 직후, 클라에서는 복제가 도착했을 때. 표시(퀘스트 목표, 대화 선택지 잠금)가 다시 그릴 때 듣는다.
	FVBOnStoryStateChanged OnStoryStateChanged;

	//~ IVBSaveParticipant - 사실, 카운터 구역(FVBProgressSaveData::Story)
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	// 등록은 BeginPlay 가 아니라 여기 - Continue 적용이 월드 BeginPlay 보다 먼저 올 수 있다(IVBSaveParticipant 주석).
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FVBStoryState State;

	// 세이브를 읽을 때마다 +1. 클라 표시가 불러온 사실과 새로 켜진 사실을 가르는 데 쓴다.
	// 서버의 OnSaveApplied 는 클라에 안 오므로 복제되는 수가 필요하다. OnRep 없이 State 의 OnRep 에서 같이 읽는다.
	UPROPERTY(Replicated)
	int32 SaveReadSerial = 0;

	UFUNCTION()
	void OnRep_State();

	// 소유 액터가 없으면(단위 시험) 쓴다 - 복제할 곳이 없으니 서버와 갈릴 일도 없다.
	bool HasWriteAuthority(const TCHAR* What) const;
};
