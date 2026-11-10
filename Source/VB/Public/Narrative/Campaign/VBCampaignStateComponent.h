// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBCampaignStateComponent.generated.h"

class UVBSaveGame;

/**
 * 회차를 관리한다 - 몇 회차인지, 앞 회차의 결말은 무엇이었는지. AVBGameState 에 붙고 쓰기는 서버만.
 * 앞 회차 기록은 읽기만 하며 이번 회차 사실(UVBStoryStateComponent)과 섞지 않는다.
 * 회차 이름이 비어 있으면 처음 읽을 때 1회차로 붙이고 활성 세이브에도 바로 적는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBCampaignStateComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBCampaignStateComponent();

	// 지금 월드의 GameState 에 붙은 것. 없으면 nullptr(메뉴 월드 등).
	static UVBCampaignStateComponent* Get(const UObject* WorldContextObject);

	const FVBCampaignSaveData& GetState() const { return State; }
	int32 GetRunIndex() const { return State.RunIndex; }
	// 앞 회차가 끝낸 결말. 1회차는 비어 있다(IsSet 거짓).
	const FVBClearSummary& GetPrevious() const { return State.Parent; }

	// 서버 전용. 복제 상태와 메모리의 활성 세이브만 고치고 슬롯 파일은 쓰지 않는다.
	bool RecordClear(const FVBClearSummary& Summary);

	//~ IVBSaveParticipant - 회차 구역(UVBSaveGame::Campaign)
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	// 등록과 첫 읽기는 BeginPlay 가 아니라 여기 - 배치된 적의 존재 조건이 서버 BeginPlay 에서 앞 회차를 본다.
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	FVBCampaignSaveData State;

	// 세이브의 회차를 받아들인다. 이름이 비었으면(옛 슬롯, 새 게임) 이미 붙인 이름을 지키거나 1회차로 새로 붙인다.
	void Adopt(const FVBCampaignSaveData& Saved, const TCHAR* From);
	bool HasWriteAuthority(const TCHAR* What) const;
};
