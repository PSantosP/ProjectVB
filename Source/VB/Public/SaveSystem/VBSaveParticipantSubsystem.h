// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakInterfacePtr.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBSaveParticipantSubsystem.generated.h"

/**
 * 이 월드의 저장 참여자 목록. PIE 는 월드 초기화 뒤에 게임 인스턴스를 붙여서 게임 인스턴스가 아니라 월드에 둔다.
 * 월드 서브시스템 참여자는 Initialize 에서 InitializeDependency<UVBSaveParticipantSubsystem>() 로 이것을 먼저 세운다.
 */
UCLASS()
class VB_API UVBSaveParticipantSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 참여자의 월드에서 목록을 찾아 등록, 해제한다. 월드가 없으면(에디터 미리보기 등) 아무것도 안 한다.
	static void Register(IVBSaveParticipant* Participant);
	static void Unregister(IVBSaveParticipant* Participant);

	// 살아 있는 참여자만.
	TArray<IVBSaveParticipant*> GetParticipants();

	// 참여자 모두에게 세이브를 읽히고 OnSaveApplied 를 한 번 방송한다. 돌려주는 수는 적용이 아니라 건넨 수다.
	//   중단, 재진입이면 완료 없이 0 이다.
	int32 ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context);
	int32 ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context, TFunctionRef<bool()> IsCurrent);

	// ReadAll 이 참여자를 읽히는 동안 참. 읽기 순서가 정해져 있지 않아 다른 구역을 보는 판단은 OnSaveApplied 까지 미룬다.
	bool IsApplyingSave() const { return bApplyingSave; }

	FSimpleMulticastDelegate OnSaveApplied;

	// 읽기의 세 번째 단계(OnSaveApplied 수신자가 모두 끝난 뒤). 상한이 정해진 뒤 현재값(체력, 보호막)을 복원한다.
	//   1 ReadAll = 정체, 2 OnSaveApplied = 구역 사이 판정과 효과, 3 OnSaveFinalized = 현재값.
	FSimpleMulticastDelegate OnSaveFinalized;

private:
	TArray<TWeakInterfacePtr<IVBSaveParticipant>> Participants;

	bool bApplyingSave = false;
	// 완료 수신자는 적용 밖에서 판단하지만, 이 회차 안에서 또 읽기를 시작할 수는 없다.
	bool bReadCycleActive = false;

	static UVBSaveParticipantSubsystem* FindFor(IVBSaveParticipant* Participant);
};
