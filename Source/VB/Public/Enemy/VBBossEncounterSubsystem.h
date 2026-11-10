// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBBossEncounterSubsystem.generated.h"

class AVBBossBase;

/**
 * 지금 싸우고 있는 보스 목록. AVBBossBase 가 올리고 HUD 보스 바가 읽는다 - 둘이 서로를 모르게 한다.
 * 복제 없이 기계마다 따로 선다. 약참조라 파괴된 보스는 GetEngagedBoss 가 걷어낸다.
 */
UCLASS()
class VB_API UVBBossEncounterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 같은 값이면 아무것도 안 한다. 바뀌면 OnEngagedBossChanged 를 한 번 알린다.
	void SetEngaged(AVBBossBase* Boss, bool bEngaged);

	// 가장 나중에 싸움을 시작한 보스(파괴된 것은 걷어낸다 - 죽음은 생산자가 bEngaged 를 내려 뺀다). 없으면 nullptr.
	AVBBossBase* GetEngagedBoss();

	// 보여 줄 보스가 바뀌었다(nullptr = 없음).
	DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnEngagedBossChanged, AVBBossBase* /*Boss*/);
	FVBOnEngagedBossChanged OnEngagedBossChanged;

private:
	// 시작한 순서. 마지막이 보여 줄 보스다.
	TArray<TWeakObjectPtr<AVBBossBase>> Engaged;

	TWeakObjectPtr<AVBBossBase> Announced;
	void AnnounceIfChanged();
};
