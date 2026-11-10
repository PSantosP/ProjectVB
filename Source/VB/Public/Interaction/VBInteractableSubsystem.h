// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SaveSystem/VBSavedWorldSubsystem.h"
#include "VBInteractableSubsystem.generated.h"

class UVBInteractableComponent;

/**
 * 월드의 상호작용 대상 목록(고르는 것은 UVBInteractorComponent). 레벨당 수십 개라 겹침 질의 대신 목록을 순회한다.
 * 서버, 클라가 각자 BeginPlay 등록으로 같은 목록을 세워 복제가 필요 없다. 파괴된 대상은 GetInteractables 가 걷어낸다.
 */
UCLASS()
class VB_API UVBInteractableSubsystem : public UVBSavedWorldSubsystem
{
	GENERATED_BODY()

public:

	//~ IVBSaveParticipant - 문, 스위치 구역(Progress.InteractableStates). 아래 CollectSavedStates / ApplySavedStates 를 부른다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	void Register(UVBInteractableComponent* Interactable);
	void Unregister(const UVBInteractableComponent* Interactable);

	// 살아 있는 항목만 돌려준다.
	const TArray<TWeakObjectPtr<UVBInteractableComponent>>& GetInteractables();

	// 저장: 떠 있는 대상의 켜짐 상태를 덮어쓴다. 스트리밍으로 내려간 대상의 기존 값은 남긴다(지우면 닫힌 채 돌아온다).
	void CollectSavedStates(TMap<FName, bool>& InOutStates);

	// 로드: 떠 있는 대상에는 애니 없이 적용하고, 전부를 대기 목록에 둬 나중에 스트리밍되는 대상은 Register 에서 받는다. 서버 전용.
	void ApplySavedStates(const TMap<FName, bool>& States);

	// 자리 저장. KnownTransforms 하나가 진실이다 - 로드 때 교체, 등록 때 적용, 등록 해제, 저장 때 갱신.
	// 원자리로 돌아온 대상은 맵에서 뺀다(안 빼면 옛 자리가 남는다).
	void CollectSavedTransforms(TMap<FName, FTransform>& OutTransforms);
	void ApplySavedTransforms(const TMap<FName, FTransform>& Transforms);
	const TMap<FName, FTransform>& GetKnownTransforms() const { return KnownTransforms; }

private:
	TArray<TWeakObjectPtr<UVBInteractableComponent>> Interactables;

	TMap<FName, FTransform> KnownTransforms;

	// 이 월드에서 세이브를 읽은 적이 있는가. 두 번 읽는 길은 없다(불러오기는 늘 OpenLevel) - 생기면 경고한다.
	bool bReadSaveOnce = false;

	void RefreshKnownTransform(const UVBInteractableComponent& Interactable);
	void ApplyKnownTransform(UVBInteractableComponent& Interactable) const;

	// 로드한 세이브의 상태. 대상이 등록될 때 꺼내 적용한다. 새 게임이면 비어 있다.
	TMap<FName, bool> PendingSavedStates;

	void ApplyPendingState(UVBInteractableComponent* Interactable) const;
};
