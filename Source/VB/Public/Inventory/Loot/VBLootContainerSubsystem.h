// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSavedWorldSubsystem.h"
#include "VBLootContainerSubsystem.generated.h"

/**
 * 서랍장 남은 몫을 관리한다(서랍장 키 -> 남은 줄). 스트리밍에도 남도록 월드 서브시스템에 둔다. 서버 전용, 복제 안 함.
 * 기록 없음 = 아직 안 굴림, 빈 기록 = 다 털림 - 일회용 잠금이 이 차이를 쓰므로 빈 기록도 저장한다.
 * NOTE: 불러오기가 플레이어 입장마다 돌아 MP 에서 기록이 다시 읽힌다.
 */
UCLASS()
class VB_API UVBLootContainerSubsystem : public UVBSavedWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBLootContainerSubsystem* Get(const UObject* WorldContextObject);


	// 기록이 없으면(아직 안 굴림) nullptr. 빈 배열 = 다 털림.
	const TArray<FVBSavedItem>* Find(FName Key) const;

	// 서버에서만. 굴린 결과, 준 뒤 남은 줄을 적는다(빈 배열도 적는다 - 다 털림).
	void SetRemaining(FName Key, const TArray<FVBSavedItem>& Remaining);

	// 불러오기가 기록을 바꿨다. 서랍장이 읽기 순서와 무관하게 잠금을 다시 본다.
	FSimpleMulticastDelegate OnContentsLoaded;

	//~ IVBSaveParticipant
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

private:
	bool HasWriteAuthority() const;

	// 태그, 이름뿐이라 GC 가 볼 것이 없다 - UPROPERTY 가 아니어도 된다.
	TMap<FName, TArray<FVBSavedItem>> Remaining;
};
