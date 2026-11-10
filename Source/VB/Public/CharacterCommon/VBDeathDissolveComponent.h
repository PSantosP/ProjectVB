// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h" // FTSTicker::FDelegateHandle
#include "Engine/EngineTypes.h" // FTimerHandle
#include "VBDeathDissolveComponent.generated.h"

class UNiagaraComponent;
class USkeletalMeshComponent;
class UVBDeathDissolveConfig;

/**
 * 시체가 가루로 사라지는 연출의 실행자. 사망 정리 시각에 맞춰 메시 복제 파티클을 띄우고 원본 메시를 숨긴다.
 * 수치는 UVBEnemyConfig 가 관리하므로 인자로 받는다. 틱 없이 코어 티커 예약 한 번으로 끝낸다. 머티리얼은 안 건드린다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBDeathDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDeathDissolveComponent();

	// 정리 시각에 맞춰 끝나도록 디졸브 시작을 역산해 예약한다(SecondsUntilCleanup = 액터 제거까지 남은 초).
	// 남은 시간을 받아 뺄셈을 여기 한 곳에 둔다. Config 가 null 이거나 길이가 0 이하면 아무것도 안 한다.
	void ScheduleDissolve(float SecondsUntilCleanup, UVBDeathDissolveConfig* Config);

	// UVBDeathDissolveSettings::ResolveConfig 로 전달만 한다. 새 호출자는 Settings 쪽을 부른다.
	// 남은 호출자 AVBEnemyBase::PreviewDeathDissolve 를 Settings 쪽으로 옮기면 이 함수를 지운다.
	static UVBDeathDissolveConfig* ResolveConfig(UVBDeathDissolveConfig* Explicit);

	// 진행 중인 디졸브를 취소하고 메시를 되돌린다(시작 예약, 숨기기 예약, 날아가던 파편 셋 다 거둔다).
	// 시험 재생 전용 - 실제 사망 경로는 부르지 않는다.
	void ResetDissolve();

private:
	// 예약이 만료되면 여기서 실제로 심는다. 즉시 시작인 경우 ScheduleDissolve 가 직접 부른다.
	void BeginDissolve();

	// 소유자를 ACharacter 로만 보고 GetMesh() 를 돌려준다.
	USkeletalMeshComponent* ResolveTargetMesh() const;

	// 파편 파티클을 띄우고 표면 샘플링용 메시를 꽂는다. 설정이 비어 있으면 아무것도 하지 않는다.
	void SpawnDebris(USkeletalMeshComponent* TargetMesh);


	// 죽는 메시에서 파편용 기본색 텍스처를 찾는다. 못 찾으면 null(재질 기본값).
	// 슬롯 0 이나 파라미터 이름이 아니라 삼각형이 가장 많은 섹션의 재질에서 찾는다.
	static UTexture* ResolveDebrisTexture(const USkeletalMeshComponent* TargetMesh);

	// 시작을 기다리는 동안 설정을 붙들어 둔다. DebrisEffect 참조가 들어 있어 GC 대상이다.
	UPROPERTY()
	TObjectPtr<UVBDeathDissolveConfig> PendingConfig;

	// 잘림을 반영한 확정 길이(실초). 설정값이 남은 시간보다 길면 남은 시간으로 줄인다.
	float PendingDuration = 0.0f;

	// 숨긴 메시. 되돌리기가 다시 보이게 하려고 든다. 수명은 잡지 않는다.
	TWeakObjectPtr<USkeletalMeshComponent> HiddenMesh;

	// 띄운 파편. 월드가 Outer 라 소유하지 않고 지켜보기만 한다(ResetDissolve 가 거둔다).
	TWeakObjectPtr<UNiagaraComponent> SpawnedDebris;

	// 파편이 올라온 뒤 메시를 숨기는 예약. 에디터 월드에서도 돌도록 코어 티커를 쓴다.
	FTSTicker::FDelegateHandle HideTickerHandle;

	FTimerHandle StartTimerHandle;
};
